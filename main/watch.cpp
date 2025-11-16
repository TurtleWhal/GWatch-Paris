#include "watch.hpp"
#include "ui.hpp"

#include <sys/time.h>
#include "driver/gpio.h"

#define BACKLIGHT_FADE_MS 2000

Watch watch;

uint16_t prevBrightness = 100;

/** Update power management and sleep logic */
void Watch::pm_update()
{
    while (true)
    {
        if (!this->sleeping) // if awake
        {
            if (this->system.dosleep && esp_timer_get_time() / 1000 - this->sleep_time > watch.system.sleeptime && !this->goingtosleep)
            {
                sleep();
            }
        }
        else
        {
            if (display.is_touching())
            {
                wakeup();
            }
            else if (abs(gyro_read().x) > 450)
            {
                wakeup();
                this->sleep_time = (esp_timer_get_time() / 1000) - 10000;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/** Enter sleep mode */
void Watch::sleep() //! DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF LOGIC THAT ONLY WORKS THIS WAY
{
    if (!this->sleeping)
    {
        ESP_LOGI("pm", "SLEEP");

        goingtosleep = true;

        display.set_wakeup_touch(true);

        prevBrightness = display.get_brightness();

        display.set_backlight_gradual(0, BACKLIGHT_FADE_MS);
        vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_FADE_MS));

        // make sure the display hasn't been touched while the display was fading off
        if (goingtosleep)
        {
            display.sleep();

            // enable touchscreen to wake the device
            esp_sleep_enable_gpio_wakeup();
            gpio_wakeup_enable(GPIO_NUM_5, GPIO_INTR_LOW_LEVEL);

            esp_pm_lock_release(pm_freq_lock);
            esp_pm_lock_release(pm_sleep_lock);

            this->sleeping = true;
        }
    }
}

/** Exit sleep mode */
void Watch::wakeup() //! DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF LOGIC THAT ONLY WORKS THIS WAY
{
    static bool wakeup_in_progress = false; // Add this guard

    if (goingtosleep && !this->sleeping)
    {
        display.set_backlight(prevBrightness);
    }

    goingtosleep = false;

    if (this->sleeping && !wakeup_in_progress)
    { // Check the guard
        ESP_LOGI("pm", "WAKEUP");

        wakeup_in_progress = true; // Set guard

        this->sleeping = false;

        esp_pm_lock_acquire(pm_freq_lock);
        esp_pm_lock_acquire(pm_sleep_lock);

        if (lv_screen_active() == main_screen)
            lv_obj_scroll_to_view_recursive(hor_layer, LV_ANIM_OFF);

        display.wake();

        display.set_rotation(lv_display_get_rotation(NULL));

        // update display before turning on backlight
        // display.refresh();

        display.set_backlight(prevBrightness);

        wakeup_in_progress = false; // Clear guard
    }

    this->sleeping = false;

    this->sleep_time = esp_timer_get_time() / 1000;
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
                            "pm", 1024 * 6, this, 0, &pm_task, 0);
}

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

/** Initialise I²C */
void Watch::iic_init()
{

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

void Watch::i2c_scan()
{
    ESP_LOGI("scan", "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        uint8_t data = 0;
        esp_err_t ret = i2c_master_probe(i2c_bus, addr, 100);
        if (ret == ESP_OK)
        {
            ESP_LOGI("scan", "Found device at 0x%02X", addr);
        }
    }
    ESP_LOGI("scan", "I2C scan complete.");
}

/** Initialise all watch subsystems */
void Watch::init()
{
    display.init_memory();

    gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

    setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();

    pm_init();
    iic_init();

    haptic_init();

    battery_init();

    imu_init(i2c_bus);

    display.init(i2c_bus);

    // display.refresh();

    display.set_backlight(100);

    wifi.init();

    haptic_play(false, 80, 80, 80, 80, 80, 0); // vibrate 3 times

    // i2c_scan();
}

/** Wrapper for C -> C++ shenanigans */
void watch_init()
{
    watch.init();
}