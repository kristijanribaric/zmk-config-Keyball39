zephyr_library()
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

if(CONFIG_PMW3610_PM AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  set(PMW3610_DRIVER_SOURCE "${ZEPHYR_BASE}/../zmk-pmw3610-driver/src/pmw3610.c")

  if(EXISTS "${PMW3610_DRIVER_SOURCE}")
    file(READ "${PMW3610_DRIVER_SOURCE}" PMW3610_DRIVER_CONTENTS)

    if(NOT PMW3610_DRIVER_CONTENTS MATCHES "KEYBALL39_PMW3610_RESUME_SPI_PM")
      string(REPLACE
        "\n#include <zephyr/drivers/pinctrl.h>"
        ""
        PMW3610_DRIVER_CONTENTS
        "${PMW3610_DRIVER_CONTENTS}"
      )

      set(KEYBALL39_PMW3610_RESUME_SPI_PM_BLOCK
        "        /* KEYBALL39_PMW3610_RESUME_SPI_PM: suspend disconnects the SPI pins manually,\n         * so explicitly resume the SPI controller before talking to the sensor again. */\n        err = pm_device_action_run(config->bus.bus, PM_DEVICE_ACTION_RESUME);\n        if (err && err != -EALREADY && err != -ENOSYS) {\n            LOG_WRN(\"Cannot resume SPI bus before PMW3610 init: %d\", err);\n        }"
      )

      string(REPLACE
        "        // Release SPI data pins to high-Z to prevent back-feeding power through ESD diodes\n        // These pins are defined in the overlay: SCK=P1.13, MOSI/MISO=P0.10\n        {\n            const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));\n            const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));\n\n            if (device_is_ready(gpio0)) {\n                // MOSI/MISO on P0.10\n                gpio_pin_configure(gpio0, 10, GPIO_DISCONNECTED);\n                LOG_INF(\"Released P0.10 (MOSI/MISO) to disconnected\");\n            }\n            if (device_is_ready(gpio1)) {\n                // SCK on P1.13\n                gpio_pin_configure(gpio1, 13, GPIO_DISCONNECTED);\n                LOG_INF(\"Released P1.13 (SCK) to disconnected\");\n            }\n        }"
        "        /* KEYBALL39_PMW3610_SKIP_SPI_DISCONNECT: leave SPI pins under the SPI driver's\n         * pinctrl states. Disconnecting them here can leave the trackball unreachable after wake. */"
        PMW3610_DRIVER_CONTENTS
        "${PMW3610_DRIVER_CONTENTS}"
      )

      string(REPLACE
        "        /* KEYBALL39_PMW3610_RESUME_PINCTRL: suspend disconnects the SPI pins manually,\n         * so re-apply the SPI pinctrl default state before talking to the sensor again. */\n        const struct pinctrl_dev_config *spi_pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(spi1));\n        err = pinctrl_apply_state(spi_pcfg, PINCTRL_STATE_DEFAULT);\n        if (err) {\n            LOG_ERR(\"Cannot re-apply SPI pinctrl default state: %d\", err);\n            return err;\n        }"
        "${KEYBALL39_PMW3610_RESUME_SPI_PM_BLOCK}"
        PMW3610_DRIVER_CONTENTS
        "${PMW3610_DRIVER_CONTENTS}"
      )

      if(NOT PMW3610_DRIVER_CONTENTS MATCHES "KEYBALL39_PMW3610_RESUME_SPI_PM")
        string(REPLACE
          "LOG_INF(\"PMW3610 turning ON - reconfiguring pins and reinitializing\");"
          "LOG_INF(\"PMW3610 turning ON - reconfiguring pins and reinitializing\");\n\n${KEYBALL39_PMW3610_RESUME_SPI_PM_BLOCK}"
          PMW3610_DRIVER_CONTENTS
          "${PMW3610_DRIVER_CONTENTS}"
        )
      endif()

      file(WRITE "${PMW3610_DRIVER_SOURCE}" "${PMW3610_DRIVER_CONTENTS}")
    endif()
  endif()
endif()

if(CONFIG_PMW3610 AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/pmw3610_peripheral_stubs.c)
endif()

if(CONFIG_ZMK_POINTING AND CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/input_processor_trackball_arrows.c)
endif()
