/*
 * DAP Strings Compatibility Stubs
 *
 * Provides string functions called by DAP.c for ID_DAP_Info responses.
 * Replaces DAPLink's dap_strings.h / info.h dependency chain.
 */

#ifndef DAP_STRINGS_COMPAT_H_
#define DAP_STRINGS_COMPAT_H_

#include <stdint.h>

static inline const char *DAP_GetVendorString(void) { return "ARM"; }
static inline const char *DAP_GetProductString(void) { return "CMSIS-DAP"; }
static inline const char *DAP_GetSerNumString(void) { return NULL; }
static inline const char *DAP_GetProductFirmwareVersionString(void) { return "1.0"; }
static inline const char *DAP_GetTargetDeviceVendorString(void) { return NULL; }
static inline const char *DAP_GetTargetDeviceNameString(void) { return NULL; }
static inline const char *DAP_GetTargetBoardVendorString(void) { return NULL; }
static inline const char *DAP_GetTargetBoardNameString(void) { return NULL; }
static inline const char *DAP_GetProductURL(void) { return NULL; }

#endif /* DAP_STRINGS_COMPAT_H_ */
