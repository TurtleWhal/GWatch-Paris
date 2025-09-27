#include "watch.hpp"
#include "driver/gpio.h"

#define SLEEP_DELAY 5000
#define BACKLIGHT_FADE_MS 2000

Watch watch;

/** Update power management and sleep logic */
void Watch::pm_update()
{
    while (true)
    {
        if (!watch.sleeping) // if awake
        {
            if (esp_timer_get_time() / 1000 - sleep_time > SLEEP_DELAY)
            {
                sleep();
            }
        }
        else
        {
            if (display.is_touching())
                wakeup();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/** Enter sleep mode */
void Watch::sleep()
{
    if (!sleeping)
    {
        goingtosleep = true;

        display.set_backlight_gradual(0, BACKLIGHT_FADE_MS);
        vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_FADE_MS));

        // make sure the display hasn't been touched while the display was fading off
        if (goingtosleep)
        {
            display.sleep();

            this->sleeping = true;

            esp_sleep_enable_gpio_wakeup();

            esp_pm_lock_release(pm_freq_lock);
            esp_pm_lock_release(pm_sleep_lock);
        }
    }
}

/** Exit sleep mode */
void Watch::wakeup()
{
    goingtosleep = false;

    if (sleeping)
    {
        esp_pm_lock_acquire(pm_freq_lock);
        esp_pm_lock_acquire(pm_sleep_lock);

        display.wake();
        display.refresh();

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

    display.refresh();

    display.set_backlight(100);

    wifi.init();
}

/** Wrapper for C -> C++ shenanigans */
void watch_init()
{
    watch.init();
}