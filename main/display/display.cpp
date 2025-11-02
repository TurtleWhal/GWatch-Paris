#include "drivers/gc9a01_driver.h"
#include "drivers/cst816s_driver.h"

#include "driver/ledc.h"

#include "watch.hpp"
#include <cmath>

#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // 13-bit: 0 - 8191
#define LEDC_FREQUENCY 5000             // 5 kHz PWM frequency

/** Get current backlight brightness
 * @returns Current backlight value (0-100)
 */
uint16_t Display::get_brightness() { return bgval; }

/** Fade on backlight
 * @param val Backlight target brightness (0-100)
 * @param ms Time to take to get to brightness in milliseconds
 */
void Display::set_backlight_gradual(int16_t val, uint32_t ms)
{
    int32_t tempbk;
    old_backlight = bgval;
    tempbk = (val - old_backlight);
    endtime = (esp_timer_get_time() / 1000) + ms;
    starttime = esp_timer_get_time() / 1000;

    k = (float)((tempbk * 1.0) / ms);

    // DEBUGF("Backlight_Gradually Val:%d,ms:%d,k:%f\n", val, ms, k);
    // Log.verboseln("Backlight Gradual | Val: %d | Length: %d | Step: %f/ms", val, ms, k);

    adjust = true;
    vTaskResume(backlight_handle);
}

/** Set backlight brightness
 * @param val Backlight brightness
 * @param stopgrad default true, cancels any current gradual backlight adjustment
 */
void Display::set_backlight(int16_t val, bool stopgrad)
{
    if (stopgrad)
        adjust = false;

    uint32_t duty = 0;

    if (val < 100)
    {
        duty = (uint32_t)((val * val * 0.8192) + 0.5); // same math as Arduino
        bgval = val;
    }
    else
    {
        duty = 0x1FFF; // 8191 max for 13-bit
        bgval = 100;
    }

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/** Update task for gradual backlight */
void Display::backlight_update()
{
    while (1) // in its own thread so its fine
    {
        static uint32_t Millis;
        // static uint16_t count;
        static uint32_t duration;

        if (adjust)
        {
            uint32_t mils = esp_timer_get_time() / 1000;
            uint32_t time_ms = 1;

            if (mils - Millis > time_ms)
            {
                duration = mils - starttime;
                set_backlight((uint16_t)((k * duration) + old_backlight), false);
                // ESP_LOGI("backlight", "K: %f duration: %d old: %d current: %d\n", k, duration, old_backlight, bgval);

                if (Millis > endtime)
                {
                    adjust = false;
                }

                Millis = mils;
            }
        }
        else
        {
            vTaskSuspend(backlight_handle);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/** Set the rotation of the display
 * @param rotation LVGL rotation value (ex: LV_DISPLAY_ROTATION_90)
 */
void Display::set_rotation(lv_display_rotation_t rotation)
{
    lv_display_set_rotation(disp, rotation); // lvgl rotation is counter-clockwise
    gc9a01_setRotation((4 - lv_display_get_rotation(disp)) % 4);
}

/** Display flush callback for LVGL */
void lvgl_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    // uint32_t size = w * h;

#ifdef ENV_WAVESHARE
    gc9a01_startWrite();
    gc9a01_pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)px_map);
    gc9a01_endWrite();
#endif // ENV_WAVESHARE

    lv_display_flush_ready(disp);
}

bool wakeup_touch = false;

/** Touch read callback for LVGL */
void lvgl_touch_read(lv_indev_t *indev, lv_indev_data_t *touch)
{
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;
    static bool touching = false;
    static int64_t touch_start_ms = 0;
    static bool reboot_scheduled = false;

#ifdef ENV_WAVESHARE
    if (cst816s_available())
    {
        watch.wakeup();
        touch_data data = cst816s_touch_read();

        if (!touching)
        {
            ESP_LOGI("cst816s", "Screen Touched at: %d, %d", data.x, data.y);
            touching = true;
            touch_start_ms = esp_timer_get_time() / 1000;
            reboot_scheduled = false;
        }
        else
        {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (!reboot_scheduled && (now_ms - touch_start_ms) >= 30000)
            {
                ESP_LOGW("cst816s", "Screen held >30s, rebooting");
                reboot_scheduled = true;
                esp_restart();
            }
        }

        last_x = data.x;
        last_y = data.y;

        if (!wakeup_touch)
        {
            touch->state = LV_INDEV_STATE_PR;
        }
    }
    else
    {
        if (touching)
        {
            wakeup_touch = false;
        }

        touching = false;
        touch_start_ms = 0;
        reboot_scheduled = false;
        touch->state = LV_INDEV_STATE_REL;
    }

    touch->point.x = last_x;
    touch->point.y = last_y;
#endif // ENV_WAVESHARE
}

/** Check if the screen is being touched
 * @returns whether the screen is being touched or not
 */
bool Display::is_touching()
{
#ifdef ENV_WAVESHARE
    return cst816s_available();
    // return cst816s_available_soft();
#endif // ENV_WAVESHARE
}

void Display::reset_touch()
{
#ifdef ENV_WAVESHARE
    cst816s_read_touch();
#endif // ENV_WAVESHARE
}

/** Initialise the LCD and Touchscreen
 * @param bus master I²C bus reference (for touchscreen)
 */
void Display::init(i2c_master_bus_handle_t bus)
{
    esp_err_t ret = gc9a01_begin();
    if (ret != ESP_OK)
    {
        ESP_LOGE("APP", "Failed to initialize display");
        return;
    }

    // Enable byte swapping if needed
    gc9a01_setSwapBytes(true);

    cst816s_init(bus);

    this->init_graphics();

    //* Configure Backlight *//

    // Configure timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    // Configure channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LCD_BLK,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0, // start with backlight off
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);

    xTaskCreatePinnedToCore([](void *pvParameters)
                            {
                                auto *obj = static_cast<Display *>(pvParameters);
                                obj->backlight_update(); },
                            "backlight", 1024 * 4, this, 2, &backlight_handle, 0);
}

/** Put the dispay to sleep and stop graphics */
void Display::sleep()
{
    goingtosleep = true;
}

/** Runs after every lvgl loop */
void Display::lvgl_done()
{
    if (!goingtosleep)
        return;

    goingtosleep = false;

    // give time for any pending SPI
    vTaskDelay(pdMS_TO_TICKS(30));
    spi_device_acquire_bus(gc9a01.spi, portMAX_DELAY);
    spi_device_release_bus(gc9a01.spi);

    // put the display in low power mode
    gc9a01_sleep();
    gc9a01_displayOff();
    gc9a01_cleanup();

    // stop the lvgl task at the end since this function is called in lvgl task context
    vTaskSuspend(lv_task_handle);
}

/** Wake up the display and graphics */
void Display::wake()
{
    /** Reinitalize the display */
    gc9a01_begin();
    gc9a01_setSwapBytes(true);
    gc9a01_setRotation(3);

    vTaskResume(lv_task_handle);
}

/** Set to ignore the first touch to avoid accidental clicks */
void Display::set_wakeup_touch(bool enable)
{
    wakeup_touch = enable;
}