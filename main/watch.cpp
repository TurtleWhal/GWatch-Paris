#include "watch.hpp"
#include "driver/gpio.h"

Watch watch;

void Watch::sleep()
{
    if (!this->sleeping)
    {
        //     setBacklightGradual(0, 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        //     vTaskSuspend(lv_task_handle);
        this->sleeping = true;

        esp_pm_lock_release(pm_lock);
    }
}

void Watch::wakeup()
{
    if (sleeping)
    {
        esp_pm_lock_acquire(pm_lock);
        //     vTaskResume(lv_task_handle);
        vTaskDelay(pdMS_TO_TICKS(300)); // wait for the display to refresh
    }

    // // gpio_set_level(2, 1);
    // setBacklight(brightness);

    // this->sleep_time = millis(); // reset sleep timer on touch
    this->sleep_time = esp_timer_get_time() / 1000; // reset sleep timer on touch
    this->sleeping = false;
}

void Watch::pm_init()
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lvgl_lock", &pm_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "lvgl_lock", &pm_lock));

    esp_pm_lock_acquire(pm_lock);
}

void Watch::iic_init()
{
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = IIC_SDA,
        .scl_io_num = IIC_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));
}

void Watch::init()
{
    gpio_install_isr_service(0);

    pm_init();
    iic_init();

    battery_init();

    display.init(i2c_bus);

    display.set_backlight(100);
}

void watch_init()
{
    watch.init();
}