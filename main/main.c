#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <driver/i2c_master.h>
#include <esp_pm.h>
#include "fonts.h"
#include "driver/gpio.h"

#include "system.h"

struct SystemInfo sysinfo;

static const char *TAG = "main";

#define I2C_MASTER_SCL_IO 7
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

esp_pm_lock_handle_t pm_lock;

TaskHandle_t lv_task_handle = NULL;

uint32_t sleep_time = 0;
bool sleeping = false;

bool touching = false;

uint16_t brightness = 100;

uint32_t millis()
{
    return esp_timer_get_time() / 1000;
}

void ui_doubleclick_cb(lv_event_t *e) { esp_restart(); }
void ui_press_cb(lv_event_t *e) { touching = true; }
// void ui_release_cb(lv_event_t *e) { touching = false; }

void lv_task(void *args)
{
    while (true)
    {
        ui_update();
        lv_timer_handler();
        // uint32_t d = lv_timer_handler();
        // ESP_LOGI(TAG, "LVGL delay %d ms", d);
        // vTaskDelay(pdMS_TO_TICKS(d));
    }
}

void update_task(void *args)
{
    while (true)
    {
        // ESP_LOGI("loop", "CPU Freq: %d", esp_clk_cpu_freq());

        if (!sleeping) // if awake
        {

            // if (cst816s_available())
            if (touching)
            {
                sleep_time = millis(); // reset sleep timer on touch
                touching = false;
            }
            else if (millis() - sleep_time > 14000)
            {
                // gpio_set_level(2, 0);
                setBacklightGradual(0, 1000);
                vTaskDelay(pdMS_TO_TICKS(1000));
                vTaskSuspend(lv_task_handle);
                // display_sleep();
                sleeping = true;

                esp_pm_lock_release(pm_lock);
            }
        }
        else
        {
            if (cst816s_available())
            // if (touching)
            {
                esp_pm_lock_acquire(pm_lock);
                // display_wake();
                // vTaskDelay(pdMS_TO_TICKS(50));
                vTaskResume(lv_task_handle);
                // lv_refr_now(lv_display_get_default());
                vTaskDelay(pdMS_TO_TICKS(300)); // wait for the display to refresh
                // gpio_set_level(2, 1);
                setBacklight(brightness);

                sleep_time = millis(); // reset sleep timer on touch
                sleeping = false;
                touching = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI("MEM", "Free heap: %d", esp_get_free_heap_size());
    ESP_LOGI("MEM", "Largest block: %d", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lvgl_lock", &pm_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "lvgl_lock", &pm_lock));

    esp_pm_lock_acquire(pm_lock);

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

    wifi_init();

    ui_init();

    // Create demo UI
    lv_obj_t *scr = lv_screen_active();
    // Define a dummy event callback function
    // lv_obj_add_event_cb(scr, ui_doubleclick_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    // lv_obj_add_event_cb(scr, ui_press_cb, LV_EVENT_PRESSED, NULL);

    set_timezone("PST8PDT,M3.2.0/2,M11.1.0/2"); // Seattle
    // set_timezone("MST7MDT,M3.2.0/2,M11.1.0/2"); // Mountain Time

    sleep_time = millis();

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        1024 * 8,
        NULL,
        0,
        &lv_task_handle,
        1);

    xTaskCreatePinnedToCore(
        update_task,
        "update_task",
        1024 * 4,
        NULL,
        0,
        NULL,
        0);

    vTaskDelay(270);
    setBacklight(100);
}