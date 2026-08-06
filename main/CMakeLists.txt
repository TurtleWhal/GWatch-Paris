# idf_component_register(
#     SRCS
#         "main.c"
#         "ui.c"
#         "display.c"
#         "wifitime.c"
#         "battery.c"
#         "drivers/gc9a01_driver.h"
#         "drivers/cst816s.c"
#     INCLUDE_DIRS
#         "."
#         "./display"
#         "./hardware"
#         "./system"
#         "./ui"
#     REQUIRES
#         lvgl 
#         fonts 
#         watchui 
#         driver 
#         esp_timer 
#         freertos
#         esp_wifi         # Wi-Fi stack
#         esp_netif        # Network interfaces (station mode)
#         esp_event        # Event loop
#         nvs_flash        # NVS (needed by Wi-Fi calibration)
#         lwip  
#         # adc_battery_estimation
#         esp_adc
# )

file(GLOB_RECURSE C_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)
file(GLOB_RECURSE CPP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
set(SOURCES ${C_SOURCES} ${CPP_SOURCES})

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS
        "."
        "./display"
        "./hardware"
        "./system"
        "./ui"
    REQUIRES
        lvgl
        fonts
        images
        # watchui
        driver
        esp_timer
        freertos
        nvs_flash
        esp_adc
        esp_lcd
        bt
        mbedtls    # for mbedtls_base64_decode in notification image transfer
        spiffs     # /spiffs/config.json filesystem for user settings/schedule/HA
        # v6.0: driver/* headers split out per-peripheral.
        esp_driver_ledc
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
)

# The "storage" partition (SPIFFS, mounted at /spiffs by Settings::init
# — see partitions.csv) is deliberately NOT built or flashed by this
# project. Auto-flashing config.json every `idf.py flash` wiped any
# settings the user changed on-watch through the settings screen; the
# fix is to leave that partition alone entirely and manage config.json
# out-of-band with a separate tool (mkspiffs + esptool, the ESP-IDF VS
# Code "Flash SPIFFS to Device" command, an lv_fs-style uploader, or
# eventually the {t:"cfg-set"} BLE path). The Settings class handles a
# missing /spiffs/config.json gracefully by returning its in-source
# defaults, so a virgin device without config still boots cleanly.

# liblvgl.a references lv_malloc_core / lv_free_core / ... — our
# LV_USE_CUSTOM_MALLOC hooks in system/lv_mem_psram.c that route LVGL's heap
# into PSRAM. Because we depend on lvgl (not the reverse), libmain.a is
# scanned before liblvgl.a at link time, so the linker has already passed our
# definitions when it hits lvgl's references and won't pull the member on its
# own. Mark one of the symbols undefined to force-include the whole object
# (all the hooks live in that one translation unit, so one -u drags them all).
target_link_options(${COMPONENT_LIB} INTERFACE "SHELL:-u lv_malloc_core")
