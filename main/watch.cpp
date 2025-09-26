#include "watch.hpp"
#include "driver/gpio.h"

Watch watch;

/** Update power management and sleep logic */
void Watch::pm_update()
{
    while (true)
    {
        // ESP_LOGI("loop", "CPU Freq: %d", esp_clk_cpu_freq());

        if (!watch.sleeping) // if awake
        {
            if (esp_timer_get_time() / 1000 - sleep_time > 5000)
            {
                sleep();
            }
        }
        else
        {
            if (display.is_touching())
                wakeup();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/** Enter sleep mode */
void Watch::sleep()
{
    if (!this->sleeping)
    {
        display.set_backlight_gradual(0, 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display.sleep();

        this->sleeping = true;

        esp_pm_lock_release(pm_freq_lock);
        esp_pm_lock_release(pm_sleep_lock);

        esp_sleep_enable_gpio_wakeup();
        // esp_light_sleep_start();

        // wakeup();
    }
}

/** Exit sleep mode */
void Watch::wakeup()
{
    if (sleeping)
    {
        esp_pm_lock_acquire(pm_freq_lock);
        esp_pm_lock_acquire(pm_sleep_lock);

        display.wake();

        lv_obj_invalidate(lv_screen_active()); // Mark screen dirty
        lv_task_handler();                     // Process drawing immediately

        // vTaskDelay(pdMS_TO_TICKS(300)); // wait for the display to refresh
    }

    display.set_backlight(100);

    this->sleep_time = esp_timer_get_time() / 1000; // reset sleep timer on touch
    this->sleeping = false;
}

/** Initialise power management */
void Watch::pm_init()
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "freq_lock", &pm_freq_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "sleep_lock", &pm_sleep_lock));

    esp_pm_lock_acquire(pm_freq_lock);
    esp_pm_lock_acquire(pm_sleep_lock);

    xTaskCreatePinnedToCore([](void *pvParameters)
                            {
                                auto *obj = static_cast<Watch *>(pvParameters);
                                obj->pm_update(); },
                            "pm", 1024 * 4, this, 0, NULL, 0);
}

/** Initialise I²C */
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

/** Initialise all watch subsystems */
void Watch::init()
{
    gpio_install_isr_service(0);

    pm_init();
    iic_init();

    battery_init();

    display.init(i2c_bus);

    lv_obj_invalidate(lv_screen_active()); // Mark screen dirty
    lv_task_handler();                     // Process drawing immediately

    display.set_backlight(100);

    wifi.init();

    wifi.connect();
}

/** Wrapper for C -> C++ shenanigans */
void watch_init()
{
    watch.init();
}