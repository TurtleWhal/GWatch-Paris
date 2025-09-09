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

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

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
        watchui 
        driver 
        esp_timer 
        freertos
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_adc
)
