/*
 * DAP Strings Compatibility Stubs
 *
 * Provides string functions called by DAP.c for ID_DAP_Info responses.
 * Replaces DAPLink's dap_strings.h / info.h dependency chain.
 *
 * Signature: uint8_t DAP_GetXxxString(char *str)
 *   - Write string into buffer (including null terminator)
 *   - Return string length including null, or 0 if no string
 */

#ifndef DAP_STRINGS_COMPAT_H_
#define DAP_STRINGS_COMPAT_H_

#include <stdint.h>
#include <string.h>

#define MAX_DAP_STR_LEN 60

/**
 * Copy string to output buffer, return length (including null terminator).
 */
static inline uint8_t return_dap_string(char *dst, const char *src)
{
	uint32_t len = strlen(src) + 1U;
	if (len > MAX_DAP_STR_LEN) {
		len = MAX_DAP_STR_LEN;
	}
	memcpy(dst, src, len);
	return (uint8_t)len;
}

static inline uint8_t DAP_GetVendorString(char *str)
{
	return return_dap_string(str, "ARM");
}

static inline uint8_t DAP_GetProductString(char *str)
{
	return return_dap_string(str, "CMSIS-DAP");
}

static inline uint8_t DAP_GetSerNumString(char *str)
{
	/* TODO: read unique ID from hardware or provisioning */
	(void)str;
	return 0U;
}

static inline uint8_t DAP_GetProductFirmwareVersionString(char *str)
{
	return return_dap_string(str, "1.0");
}

static inline uint8_t DAP_GetTargetDeviceVendorString(char *str)
{
	/* No target board info in Phase 1 */
	(void)str;
	return 0U;
}

static inline uint8_t DAP_GetTargetDeviceNameString(char *str)
{
	(void)str;
	return 0U;
}

static inline uint8_t DAP_GetTargetBoardVendorString(char *str)
{
	(void)str;
	return 0U;
}

static inline uint8_t DAP_GetTargetBoardNameString(char *str)
{
	(void)str;
	return 0U;
}

static inline uint8_t DAP_GetProductURL(char *str)
{
	(void)str;
	return 0U;
}

#endif /* DAP_STRINGS_COMPAT_H_ */
