// #include <stdio.h>
// #include "esp_log.h"
// #include "esp_timer.h"
// #include "esp_heap_caps.h"

// #include "lvgl.h"
#include "gc9a01_driver.h"
#include "cst816s_driver.h"

#include "driver/ledc.h"

// #include "system/system.h"

#include "watch.hpp"

// #define TAG "DISPLAY"

// static lv_display_t *disp;

#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // 13-bit: 0 - 8191
#define LEDC_FREQUENCY 5000             // 5 kHz PWM frequency

uint16_t Display::get_brightness() { return bgval; }

// val is 0-100
void Display::set_backlight_gradual(int16_t val, uint32_t ms)
{
    int32_t tempbk;
    oldBacklight = bgval;
    tempbk = (val - oldBacklight);
    endtime = (esp_timer_get_time() / 1000) + ms;
    starttime = esp_timer_get_time() / 1000;

    k = (float)((tempbk * 1.0) / ms);

    // DEBUGF("Backlight_Gradually Val:%d,ms:%d,k:%f\n", val, ms, k);
    // Log.verboseln("Backlight Gradual | Val: %d | Length: %d | Step: %f/ms", val, ms, k);

    adjust = true;
    vTaskResume(backlightHandle);
}

// Backlight Pin: 2
void Display::set_backlight(int16_t val)
{
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

void Display::backlight_updata()
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
                set_backlight((uint16_t)((k * duration) + oldBacklight));
                // ESP_LOGI("backlight", "K: %f duration: %d old: %d current: %d\n", k, duration, oldBacklight, bgval);

                if (Millis > endtime)
                {
                    adjust = false;
                }

                Millis = mils;
            }
        }
        else
        {
            vTaskSuspend(backlightHandle);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
// {
//     uint32_t w = (area->x2 - area->x1 + 1);
//     uint32_t h = (area->y2 - area->y1 + 1);
//     // uint32_t size = w * h;

//     // gc9a01_set_addr_window(area->x1, area->y1, area->x2, area->y2);
//     // gc9a01_send_data(px_map, size * 2);

//     // gc9a01_update_full_screen_dma((uint16_t *)px_map);
//     startWrite();
//     // pushImageDMA(70, 70, 100, 100, image_data);
//     // pushImageDMA_fullscreen((uint16_t *)px_map);
//     pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)px_map);
//     endWrite();

//     lv_display_flush_ready(disp);
// }

// void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *touch)
// {
//     static uint16_t last_x = 0;
//     static uint16_t last_y = 0;
//     static bool touching = false;

//     if (cst816s_available())
//     {
//         wakeup();

//         touch_data data = cst816s_touch_read();

//         if (!touching)
//             ESP_LOGI("cst816s", "Screen Touched at: %d, %d", data.x, data.y);

//         touching = true;

//         last_x = data.x;
//         last_y = data.y;
//         touch->state = LV_INDEV_STATE_PR;
//     }
//     else
//     {
//         touching = false;
//         touch->state = LV_INDEV_STATE_REL;
//     }
//     touch->point.x = last_x;
//     touch->point.y = last_y;
// }

// // Custom tick function for LVGL
// static uint32_t custom_tick_get_cb(void)
// {
//     return esp_timer_get_time() / 1000;
// }

// void setRotation(lv_display_rotation_t rotation)
// {
//     lv_display_set_rotation(disp, rotation); // lvgl rotation is counter-clockwise
//     gc9a01_setRotation((4 - lv_display_get_rotation(disp)) % 4);
// }

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

    // // Initialize LVGL
    // lv_init();

    // // Set custom tick function
    // lv_tick_set_cb(custom_tick_get_cb);

    // // Create display
    // disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);

    // // lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // // Set flush callback
    // lv_display_set_flush_cb(disp, disp_flush);

    // lv_display_set_default(disp);

    // setRotation(LV_DISPLAY_ROTATION_90);

    // lv_indev_t *indev = lv_indev_create();           /* Create input device connected to Default Display. */
    // lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /* Touch pad is a pointer-like device. */
    // lv_indev_set_read_cb(indev, lvgl_touch_read_cb); /* Set driver function. */

    // // Allocate draw buffers
    // size_t buf_size = DISP_HOR_RES * 240 * sizeof(lv_color_t);
    // void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    // // void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    // void *buf2 = NULL;

    // if (buf1)
    // {
    //     lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    //     // lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
    // }

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

    // xTaskCreatePinnedToCore([](void *pvParameters)
    //                         {
    //                             auto *obj = static_cast<Display *>(pvParameters);
    //                             obj->backlight_updata(); },
    //                         "backlight", 1024 * 4, NULL, 2, &backlightHandle, 0);

    // xTaskCreatePinnedToCore(backlight_task, "backlight", 1024 * 4, NULL, 2, &backlightHandle, 0);

    set_backlight(100);

    while (true)
    {
        fillScreen(COLOR_RED);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        fillScreen(COLOR_GREEN);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        fillScreen(COLOR_BLUE);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// void display_sleep()
// {
//     // setBrightness(0);
//     vTaskDelay(pdMS_TO_TICKS(100)); // wait for any ongoing transfers
//     gc9a01_sleep();
//     // displayOff();
//     // gc9a01_cleanup();
//     // vTaskDelay(pdMS_TO_TICKS(200)); // wait for any ongoing transfers
// }

// void display_wake()
// {
//     // gc9a01_spi_reinit(); // Re-initialize SPI

//     // gpio_set_level(GC9A01_PIN_RST, 0);
//     // vTaskDelay(pdMS_TO_TICKS(20));
//     // gpio_set_level(GC9A01_PIN_RST, 1);
//     // vTaskDelay(pdMS_TO_TICKS(120));

//     // // 3. Run full init sequence (same as gc9a01_init)
//     // gc9a01_init_registers();

//     gc9a01_wakeup(); // Wake display
//     // displayOn();
//     // setBrightness(255);

//     // begin();
//     // setSwapBytes(true);
//     // gc9a01_setRotation(3);
//     // setRotation(lv_display_get_rotation(disp));
// }