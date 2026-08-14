zephyr_library()
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

if(CONFIG_ZMK_POINTING AND CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/input_processor_trackball_arrows.c)
endif()
