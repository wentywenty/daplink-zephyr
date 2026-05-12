/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtual FAT16 Filesystem Disk Driver for DAPLink drag-and-drop flashing.
 *
 * Presents a small FAT16 volume to the USB host with a README.TXT file.
 * When the host writes a .hex or .bin file and syncs, the content is
 * parsed and programmed to the target MCU via SWD.
 *
 * Disk layout (128KB = 256 sectors of 512 bytes):
 *   Sector 0:      MBR / Boot sector with BPB
 *   Sector 1:      FAT1 start
 *   Sector 3:      FAT2 start (copy of FAT1)
 *   Sector 5:      Root directory start (16 entries = 1 sector)
 *   Sector 6+:     Data area (clusters)
 */

#include <zephyr/kernel.h>
#include <string.h>
#include "vfs_disk.h"
#include "flash_manager.h"
#include "intelhex.h"
#include "target_detect.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(vfs_disk);

/* Disk geometry */
#define SECTOR_SIZE       512U
#define SECTOR_COUNT      64U
#define FAT_SECTORS       1U                  /* 1 sector per FAT (enough for 64 clusters) */
#define FAT_COUNT         2U
#define ROOT_DIR_ENTRIES  16U
#define ROOT_DIR_SECTORS  1U
#define RESERVED_SECTORS  1U

/* Derived layout */
#define FAT1_START        RESERVED_SECTORS                    /* Sector 1 */
#define FAT2_START        (FAT1_START + FAT_SECTORS)          /* Sector 3 */
#define ROOT_DIR_START    (FAT2_START + FAT_SECTORS)          /* Sector 5 */
#define DATA_START        (ROOT_DIR_START + ROOT_DIR_SECTORS) /* Sector 6 */

/* First data cluster number (FAT16 clusters start at 2) */
#define FIRST_CLUSTER     2U

/* File states */
enum file_state {
	FILE_STATE_NONE,
	FILE_STATE_RECEIVING,
	FILE_STATE_FLASHING,
	FILE_STATE_SUCCESS,
	FILE_STATE_ERROR,
};

/* README content */
static const char readme_content[] =
	"DAPLink Drag-and-Drop Flash Programmer\r\n"
	"========================================\r\n\r\n"
	"Drop a .hex or .bin file onto this drive\r\n"
	"to flash the connected target MCU.\r\n\r\n"
	"Supported targets:\r\n"
	"  - STM32F103 (STM32F1)\r\n"
	"  - STM32F407 (STM32F4)\r\n";

/* In-memory disk image (FAT + directory only, data written by host) */
static uint8_t disk_image[SECTOR_COUNT * SECTOR_SIZE];

/* File tracking */
static enum file_state g_file_state;
static char g_filename[13];          /* 8.3 filename */
static uint32_t g_file_size;
static uint32_t g_file_start_sector; /* First data sector of file */
static uint32_t g_transfer_start;    /* First sector written in transfer */
static uint32_t g_transfer_end;      /* Last sector written in transfer */
static bool g_transfer_active;

/* Forward declarations */
static int vfs_disk_init_fn(struct disk_info *disk);
static int vfs_disk_status_fn(struct disk_info *disk);
static int vfs_disk_read_fn(struct disk_info *disk, uint8_t *data_buf,
			    uint32_t start_sector, uint32_t num_sector);
static int vfs_disk_write_fn(struct disk_info *disk, const uint8_t *data_buf,
			     uint32_t start_sector, uint32_t num_sector);
static int vfs_disk_ioctl_fn(struct disk_info *disk, uint8_t cmd, void *buff);

static const struct disk_operations vfs_disk_ops = {
	.init   = vfs_disk_init_fn,
	.status = vfs_disk_status_fn,
	.read   = vfs_disk_read_fn,
	.write  = vfs_disk_write_fn,
	.ioctl  = vfs_disk_ioctl_fn,
};

static struct disk_info vfs_disk_info = {
	.name = "DAPLINK",
	.ops  = &vfs_disk_ops,
};

/*
 * Build the static FAT16 filesystem image.
 */
static void build_filesystem(void)
{
	memset(disk_image, 0, sizeof(disk_image));

	/* === Sector 0: Boot sector / BPB === */
	uint8_t *bs = &disk_image[0];

	bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90; /* JMP short + NOP */
	memcpy(&bs[3], "MSWIN4.1", 8);              /* OEM name */
	/* Bytes per sector */
	bs[11] = (SECTOR_SIZE >> 0) & 0xFF;
	bs[12] = (SECTOR_SIZE >> 8) & 0xFF;
	/* Sectors per cluster */
	bs[13] = 1;
	/* Reserved sectors */
	bs[14] = (RESERVED_SECTORS >> 0) & 0xFF;
	bs[15] = (RESERVED_SECTORS >> 8) & 0xFF;
	/* Number of FATs */
	bs[16] = FAT_COUNT;
	/* Root directory entries */
	bs[17] = (ROOT_DIR_ENTRIES >> 0) & 0xFF;
	bs[18] = (ROOT_DIR_ENTRIES >> 8) & 0xFF;
	/* Total sectors (16-bit) */
	bs[19] = (SECTOR_COUNT >> 0) & 0xFF;
	bs[20] = (SECTOR_COUNT >> 8) & 0xFF;
	/* Media descriptor: fixed disk */
	bs[21] = 0xF8;
	/* Sectors per FAT */
	bs[22] = (FAT_SECTORS >> 0) & 0xFF;
	bs[23] = (FAT_SECTORS >> 8) & 0xFF;
	/* Sectors per track */
	bs[24] = 0x20; bs[25] = 0x00;
	/* Number of heads */
	bs[26] = 0x40; bs[27] = 0x00;
	/* Hidden sectors */
	memset(&bs[28], 0, 4);
	/* Total sectors (32-bit, 0 when 16-bit is used) */
	memset(&bs[32], 0, 4);
	/* Drive number */
	bs[36] = 0x80;
	/* Reserved */
	bs[37] = 0x00;
	/* Extended boot signature */
	bs[38] = 0x29;
	/* Volume serial number */
	bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
	/* Volume label */
	memcpy(&bs[43], "DAPLINK    ", 11);
	/* Filesystem type */
	memcpy(&bs[54], "FAT16   ", 8);
	/* Boot signature */
	bs[510] = 0x55; bs[511] = 0xAA;

	/* === FAT tables === */
	/* FAT entry 0: media descriptor, entry 1: EOC marker */
	uint16_t *fat1 = (uint16_t *)&disk_image[FAT1_START * SECTOR_SIZE];
	uint16_t *fat2 = (uint16_t *)&disk_image[FAT2_START * SECTOR_SIZE];

	fat1[0] = 0xFFF8;  /* Media descriptor */
	fat1[1] = 0xFFFF;  /* EOC marker */
	/* Cluster 2 = README.TXT (1 cluster) */
	fat1[2] = 0xFFFF;  /* EOC - file uses 1 cluster */

	memcpy(fat2, fat1, FAT_SECTORS * SECTOR_SIZE);

	/* === Root directory === */
	uint8_t *root = &disk_image[ROOT_DIR_START * SECTOR_SIZE];

	/* Volume label entry */
	memcpy(&root[0], "DAPLINK    ", 11);
	root[11] = 0x08;  /* ATTR_VOLUME_ID */

	/* README.TXT entry */
	static const char readme_name[] = "README  TXT";
	memcpy(&root[32], readme_name, 11);
	root[32 + 11] = 0x20;  /* ATTR_ARCHIVE */
	/* Cluster number (little-endian) */
	root[32 + 26] = (FIRST_CLUSTER >> 0) & 0xFF;
	root[32 + 27] = (FIRST_CLUSTER >> 8) & 0xFF;
	/* File size */
	{
		uint32_t sz = sizeof(readme_content) - 1;
		root[32 + 28] = (sz >> 0) & 0xFF;
		root[32 + 29] = (sz >> 8) & 0xFF;
		root[32 + 30] = (sz >> 16) & 0xFF;
		root[32 + 31] = (sz >> 24) & 0xFF;
	}

	/* === Data area: README.TXT content in cluster 2 === */
	{
		uint32_t data_offset = DATA_START * SECTOR_SIZE;
		uint32_t content_len = sizeof(readme_content) - 1;

		memcpy(&disk_image[data_offset], readme_content, content_len);
	}
}

/*
 * Convert 8.3 directory entry name to null-terminated string.
 */
static void dir_name_to_str(const uint8_t *entry, char *out)
{
	int i;

	for (i = 0; i < 8 && entry[i] != ' '; i++) {
		out[i] = entry[i];
	}
	if (entry[8] != ' ') {
		out[i++] = '.';
		for (int j = 8; j < 11 && entry[j] != ' '; j++) {
			out[i++] = entry[j];
		}
	}
	out[i] = '\0';
}

/*
 * Check if filename has .bin extension (case-insensitive).
 */
static bool is_bin_file(const char *name)
{
	int len = strlen(name);

	if (len < 4) {
		return false;
	}
	return (name[len - 4] == '.') &&
	       (name[len - 3] == 'b' || name[len - 3] == 'B') &&
	       (name[len - 2] == 'i' || name[len - 2] == 'I') &&
	       (name[len - 1] == 'n' || name[len - 1] == 'N');
}

/*
 * Detect a file write in the root directory.
 * Scans root directory for new or modified file entries.
 */
static void scan_root_directory(void)
{
	uint8_t *root = &disk_image[ROOT_DIR_START * SECTOR_SIZE];

	/* Skip entry 0 (volume label), entry 1 (README.TXT) */
	for (int i = 2; i < ROOT_DIR_ENTRIES; i++) {
		uint8_t *entry = &root[i * 32];

		/* Empty or deleted entry */
		if (entry[0] == 0x00 || entry[0] == 0xE5) {
			continue;
		}

		/* Skip directory, volume label, LFN entries */
		if (entry[11] & 0x08 || entry[11] & 0x10 || entry[11] == 0x0F) {
			continue;
		}

		/* We have a file entry */
		char name[13];

		dir_name_to_str(entry, name);

		if (is_bin_file(name) || intelhex_is_hex_file(name)) {
			strncpy(g_filename, name, sizeof(g_filename) - 1);
			g_filename[sizeof(g_filename) - 1] = '\0';

			/* Get file size */
			g_file_size = (uint32_t)entry[28] |
				      ((uint32_t)entry[29] << 8) |
				      ((uint32_t)entry[30] << 16) |
				      ((uint32_t)entry[31] << 24);

			/* Get starting cluster */
			uint16_t cluster = (uint16_t)entry[26] |
					   ((uint16_t)entry[27] << 8);

			/* Convert cluster to sector */
			g_file_start_sector = DATA_START +
					      (cluster - FIRST_CLUSTER);

			LOG_INF("Detected file: %s (%u bytes, cluster %u)",
				g_filename, g_file_size, cluster);
			g_file_state = FILE_STATE_RECEIVING;
			return;
		}
	}
}

/* Flash write context for HEX callback */
struct flash_write_ctx {
	uint32_t base_addr;
	bool has_error;
};

/*
 * HEX parser callback: write each data block to target flash.
 */
static int hex_data_cb(uint32_t addr, const uint8_t *data,
		       uint32_t size, void *ctx)
{
	struct flash_write_ctx *fctx = (struct flash_write_ctx *)ctx;

	if (fctx->has_error) {
		return -1;
	}

	/* Set base address on first call */
	if (fctx->base_addr == 0) {
		fctx->base_addr = addr;
	}

	int rc = flash_manager_write(addr, data, size);

	if (rc != 0) {
		LOG_ERR("Flash write failed at 0x%08X: %d", addr, rc);
		fctx->has_error = true;
		return rc;
	}
	return 0;
}

/*
 * Execute flash programming from the received file data.
 */
static void do_flash_program(void)
{
	g_file_state = FILE_STATE_FLASHING;

	LOG_INF("Starting flash programming: %s (%u bytes)",
		g_filename, g_file_size);

	/* Auto-detect connected target via SWD IDCODE */
	const target_cfg_t *target = target_detect();

	if (target == NULL) {
		LOG_ERR("Target auto-detection failed - no supported MCU detected");
		g_file_state = FILE_STATE_ERROR;
		return;
	}

	LOG_INF("Target detected: %s %s",
		target->target_vendor ? target->target_vendor : "",
		target->target_part_number ? target->target_part_number : "");

	/* Initialize flash manager */
	int rc = flash_manager_init(target);

	if (rc != 0) {
		LOG_ERR("Flash manager init failed: %d", rc);
		g_file_state = FILE_STATE_ERROR;
		return;
	}

	/* Get file data pointer */
	uint8_t *file_data = &disk_image[g_file_start_sector * SECTOR_SIZE];

	if (intelhex_is_hex_file(g_filename)) {
		/* Parse Intel HEX and program */
		struct flash_write_ctx ctx = { 0, false };

		hex_parse_result_t result = intelhex_parse(
			file_data, g_file_size, hex_data_cb, &ctx);

		if (result < 0 || ctx.has_error) {
			LOG_ERR("HEX parse/program failed: %d", result);
			flash_manager_finish();
			g_file_state = FILE_STATE_ERROR;
			return;
		}
	} else {
		/* Binary file: write directly at flash base */
		rc = flash_manager_write(0x08000000, file_data, g_file_size);
		if (rc != 0) {
			LOG_ERR("Binary flash write failed: %d", rc);
			flash_manager_finish();
			g_file_state = FILE_STATE_ERROR;
			return;
		}
	}

	/* Finish programming and reset target */
	flash_manager_finish();
	g_file_state = FILE_STATE_SUCCESS;
	LOG_INF("Flash programming complete!");
}

/*
 * Update the virtual filesystem to show result status.
 */
static void update_status_file(void)
{
	uint8_t *root = &disk_image[ROOT_DIR_START * SECTOR_SIZE];
	uint8_t *entry = &root[2]; /* Use entry 2 for status file */
	static const char success_name[] = "SUCCESS TXT";
	static const char fail_name[]    = "FAIL    TXT";
	static const char success_msg[]  = "Flash programming successful!\r\n";
	static const char fail_msg[]     = "Flash programming FAILED!\r\nCheck connection and try again.\r\n";

	const char *name;
	const char *msg;
	uint32_t msg_len;

	if (g_file_state == FILE_STATE_SUCCESS) {
		name = success_name;
		msg = success_msg;
		msg_len = sizeof(success_msg) - 1;
	} else {
		name = fail_name;
		msg = fail_msg;
		msg_len = sizeof(fail_msg) - 1;
	}

	/* Clear previous entries (entries 2+) */
	for (int i = 2; i < ROOT_DIR_ENTRIES; i++) {
		memset(&root[i * 32], 0, 32);
	}

	/* Write status file entry */
	memcpy(entry, name, 11);
	entry[11] = 0x20;  /* ATTR_ARCHIVE */
	entry[26] = (FIRST_CLUSTER >> 0) & 0xFF;
	entry[27] = (FIRST_CLUSTER >> 8) & 0xFF;
	entry[28] = (msg_len >> 0) & 0xFF;
	entry[29] = (msg_len >> 8) & 0xFF;

	/* Write status content to cluster 2 */
	uint32_t data_offset = DATA_START * SECTOR_SIZE;

	memset(&disk_image[data_offset], 0, SECTOR_SIZE);
	memcpy(&disk_image[data_offset], msg, msg_len);
}

/* ============ Disk Operations ============ */

static int vfs_disk_init_fn(struct disk_info *disk)
{
	LOG_INF("VFS disk init");
	return 0;
}

static int vfs_disk_status_fn(struct disk_info *disk)
{
	return DISK_STATUS_OK;
}

static int vfs_disk_read_fn(struct disk_info *disk, uint8_t *data_buf,
			    uint32_t start_sector, uint32_t num_sector)
{
	uint32_t offset = start_sector * SECTOR_SIZE;
	uint32_t len = num_sector * SECTOR_SIZE;

	if (offset + len > sizeof(disk_image)) {
		return -EINVAL;
	}

	memcpy(data_buf, &disk_image[offset], len);
	return 0;
}

static int vfs_disk_write_fn(struct disk_info *disk, const uint8_t *data_buf,
			     uint32_t start_sector, uint32_t num_sector)
{
	uint32_t offset = start_sector * SECTOR_SIZE;
	uint32_t len = num_sector * SECTOR_SIZE;

	if (offset + len > sizeof(disk_image)) {
		return -EINVAL;
	}

	memcpy(&disk_image[offset], data_buf, len);

	/* Track transfer range */
	if (!g_transfer_active) {
		g_transfer_start = start_sector;
		g_transfer_end = start_sector + num_sector;
		g_transfer_active = true;
	} else {
		if (start_sector < g_transfer_start) {
			g_transfer_start = start_sector;
		}
		if (start_sector + num_sector > g_transfer_end) {
			g_transfer_end = start_sector + num_sector;
		}
	}

	/* Check if root directory was written */
	if (start_sector <= ROOT_DIR_START &&
	    (start_sector + num_sector) > ROOT_DIR_START) {
		scan_root_directory();
	}

	return 0;
}

static int vfs_disk_ioctl_fn(struct disk_info *disk, uint8_t cmd, void *buff)
{
	switch (cmd) {
	case DISK_IOCTL_GET_SECTOR_COUNT:
		*(uint32_t *)buff = SECTOR_COUNT;
		break;
	case DISK_IOCTL_GET_SECTOR_SIZE:
		*(uint32_t *)buff = SECTOR_SIZE;
		break;
	case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
		*(uint32_t *)buff = SECTOR_SIZE;
		break;
	case DISK_IOCTL_CTRL_SYNC:
		/* Host has finished writing - trigger flash if file received */
		if (g_transfer_active && g_file_state == FILE_STATE_RECEIVING) {
			LOG_INF("Transfer complete, starting flash");
			g_transfer_active = false;
			do_flash_program();
			update_status_file();
		}
		break;
	case DISK_IOCTL_CTRL_INIT:
		break;
	case DISK_IOCTL_CTRL_DEINIT:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int vfs_disk_init(void)
{
	/* Build the initial virtual filesystem */
	build_filesystem();

	g_file_state = FILE_STATE_NONE;
	g_transfer_active = false;

	/* Register disk with Zephyr */
	int rc = disk_access_register(&vfs_disk_info);

	if (rc != 0) {
		LOG_ERR("Failed to register VFS disk: %d", rc);
		return rc;
	}

	LOG_INF("VFS disk registered (%u KB)", (SECTOR_COUNT * SECTOR_SIZE) / 1024);
	return 0;
}
