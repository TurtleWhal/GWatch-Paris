#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <driver/i2c.h>
#include <esp_pm.h>
#include "display.h"
#include "ui.h"
#include "fonts.h"

static const char *TAG = "main";

#define I2C_MASTER_SCL_IO 7
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

esp_pm_lock_handle_t pm_lock;

void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ};
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

static TimerHandle_t lvgl_timer_handle = NULL;

// Timer callback
static void lvgl_timer_callback(TimerHandle_t xTimer)
{
    // esp_pm_lock_acquire(pm_lock);
    ui_update();
    lv_timer_handler(); // LVGL task processing
    // esp_pm_lock_release(pm_lock);
}

// Create and start the timer (but paused initially)
void lvgl_timer_init()
{
    if (lvgl_timer_handle == NULL)
    {
        lvgl_timer_handle = xTimerCreate(
            "lvgl_timer",
            pdMS_TO_TICKS(10),
            pdTRUE, // Auto-reload
            NULL,
            lvgl_timer_callback);

        if (lvgl_timer_handle == NULL)
        {
            ESP_LOGE(TAG, "Failed to create LVGL timer");
        }
    }
}

// Start (resume) the timer
void lvgl_timer_start()
{
    if (lvgl_timer_handle != NULL)
    {
        xTimerStart(lvgl_timer_handle, 0);
        ESP_LOGI(TAG, "LVGL timer started");
    }
}

// Stop (pause) the timer
void lvgl_timer_stop()
{
    if (lvgl_timer_handle != NULL)
    {
        xTimerStop(lvgl_timer_handle, 0);
        ESP_LOGI(TAG, "LVGL timer stopped");
    }
}

void lv_task(void *args)
{
    while (true)
    {
        ui_update();
        uint32_t d = lv_timer_handler();
        // ESP_LOGI("lv_task", "d: %d", d);
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

    i2c_master_init();

    display_init();

    // Create demo UI
    ui_create();

    // ui_init("");

    // lv_obj_t *scale = lv_obj_get_child_by_name(watchscreen, "scale");

    // Serial.println(scale.valid);

    // static const char *ticks[] = {"0", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", NULL};
    // lv_scale_set_text_src(scale, ticks);

    // lv_scale_set_range(scale, 0, 60);

    // lv_scale_set_angle_range(scale, 360);
    // lv_scale_set_rotation(scale, 90);

    // lv_screen_load(watchscreen);

    // lvgl_timer_init();

    // esp_pm_lock_release(pm_lock);

    // esp_pm_lock_acquire(pm_lock);

    // gc9a01_wake();
    // lvgl_timer_start();

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        4096,
        NULL,
        0,
        NULL,
        1);

    // vTaskDelay(pdMS_TO_TICKS(1000)); // Let FreeRTOS idle

    // lv_task_handler();

    // while (1)
    // {
    //     ui_update();

    //     lv_task_handler();
    //     vTaskDelay(pdMS_TO_TICKS(10));
    // }
}