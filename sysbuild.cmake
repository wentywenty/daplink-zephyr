# Black Magic Debug - Zephyr (daplink-zephyr) sysbuild configuration.
#
# Used only for `west build --sysbuild` (see sysbuild.conf). Supplies the
# shared MCUboot partition layout so both the MCUboot image and the
# application can resolve boot_partition / slot0 / slot1 / scratch without
# extra command line arguments.

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/boards/bmd_mcuboot_partitions.overlay"
      CACHE INTERNAL "MCUboot partition layout")

  set(EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/boards/bmd_app_sysbuild.overlay"
      CACHE INTERNAL "Application MCUboot partition layout")
endif()
