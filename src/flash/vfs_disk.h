/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtual FAT16 Filesystem Disk Driver for DAPLink drag-and-drop flashing.
 * Presents a virtual disk with FAT16 filesystem to USB host.
 * Detects .hex/.bin file writes and triggers target flash programming.
 */

#ifndef VFS_DISK_H_
#define VFS_DISK_H_

#include <stdint.h>
#include <zephyr/drivers/disk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the VFS disk driver.
 * Must be called before USB MSC is started.
 *
 * @return 0 on success, negative errno on failure
 */
int vfs_disk_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VFS_DISK_H_ */
