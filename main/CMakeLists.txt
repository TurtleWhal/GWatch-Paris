idf_component_register(
    SRCS
    "main.c"
    "ui.c"
    "display.c"
    "drivers/gc9a01_driver.c"
    "drivers/cst816s.c"
    INCLUDE_DIRS
    "."
    "./drivers"
    REQUIRES
    lvgl fonts watchui driver esp_timer freertos
)