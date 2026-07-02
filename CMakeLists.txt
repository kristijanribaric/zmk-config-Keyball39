zephyr_library()
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

if(CONFIG_PMW3610 AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/pmw3610_peripheral_stubs.c)
endif()

if(CONFIG_ZMK_POINTING AND CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/input_processor_trackball_arrows.c)
endif()
