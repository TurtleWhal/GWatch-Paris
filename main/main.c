#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <driver/i2c_master.h>
// #include <esp_pm.h>
#include "display.h"
#include "ui.h"
#include "fonts.h"
#include "wifitime.h"
#include "battery.h"
#include "driver/gpio.h"

static const char *TAG = "main";

#define I2C_MASTER_SCL_IO 7
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

// esp_pm_lock_handle_t pm_lock;

TaskHandle_t lv_task_handle = NULL;

void lv_task(void *args)
{
    while (true)
    {
        ui_update();
        uint32_t d = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

void app_main(void)
{
    // esp_pm_config_t pm_config = {
    //     .max_freq_mhz = 240,
    //     .min_freq_mhz = 10,
    //     .light_sleep_enable = true,
    // };
    // ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lvgl_lock", &pm_lock));

    // esp_pm_lock_acquire(pm_lock);

    gpio_install_isr_service(0);

    i2c_master_bus_handle_t i2c_bus;
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    battery_init();

    display_init(i2c_bus);

    // Create demo UI
    ui_create();

    set_timezone("PST8PDT,M3.2.0/2,M11.1.0/2");

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        8192,
        NULL,
        0,
        &lv_task_handle,
        1);

    wifi_init();
}