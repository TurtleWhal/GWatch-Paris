#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "display.h"
#include "lvgl.h"
#include "drivers/gc9a01_driver.h"

#define TAG "DISPLAY"

static lv_display_t *disp;

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    // uint32_t size = w * h;

    // gc9a01_set_addr_window(area->x1, area->y1, area->x2, area->y2);
    // gc9a01_send_data(px_map, size * 2);

    // gc9a01_update_full_screen_dma((uint16_t *)px_map);
    startWrite();
    // pushImageDMA(70, 70, 100, 100, image_data);
    // pushImageDMA_fullscreen((uint16_t *)px_map);
    pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)px_map);
    endWrite();

    lv_display_flush_ready(disp);
}

void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *touch)
{
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;
    static bool touching = false;

    if (cst816s_available())
    {

        touch_data data = cst816s_touch_read();

        if (!touching)
            ESP_LOGI("cst816s", "Screen Touched at: %d, %d", data.x, data.y);

        touching = true;

        last_x = data.x;
        last_y = data.y;
        touch->state = LV_INDEV_STATE_PR;
    }
    else
    {
        touching = false;
        touch->state = LV_INDEV_STATE_REL;
    }
    touch->point.x = last_x;
    touch->point.y = last_y;
}

// Custom tick function for LVGL
static uint32_t custom_tick_get_cb(void)
{
    return esp_timer_get_time() / 1000;
}

void setRotation(lv_display_rotation_t rotation)
{
    lv_display_set_rotation(disp, rotation); // lvgl rotation is counter-clockwise
    gc9a01_setRotation((4 - lv_display_get_rotation(disp)) % 4);
}

void display_init(i2c_master_bus_handle_t bus)
{

    // gc9a01_driver_init();
    esp_err_t ret = begin();
    if (ret != ESP_OK)
    {
        ESP_LOGE("APP", "Failed to initialize display");
        return;
    }

    // Enable byte swapping if needed
    setSwapBytes(true);

    // uint16_t *red = heap_caps_malloc(240 * 240 * sizeof(uint16_t), MALLOC_CAP_DMA);
    // uint16_t *green = heap_caps_malloc(240 * 240 * sizeof(uint16_t), MALLOC_CAP_DMA);
    // uint16_t *blue = heap_caps_malloc(240 * 240 * sizeof(uint16_t), MALLOC_CAP_DMA);
    // if (red && green && blue)
    // {
    //     for (int i = 0; i < 240 * 240; i++)
    //     {
    //         red[i] = COLOR_RED;
    //         green[i] = COLOR_GREEN;
    //         blue[i] = COLOR_BLUE;
    //     }

    // while (true)
    // {
    // startWrite();
    // pushImageDMA_fullscreen(red);
    // endWrite();
    // startWrite();
    // pushImageDMA_fullscreen(green);
    // endWrite();
    // startWrite();
    // pushImageDMA_fullscreen(blue);
    // endWrite();
    //     fillScreen(COLOR_RED);
    //     fillScreen(COLOR_GREEN);
    //     fillScreen(COLOR_BLUE);
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
    // }

    cst816s_init(bus);

    // Initialize LVGL
    lv_init();

    // Set custom tick function
    lv_tick_set_cb(custom_tick_get_cb);

    // Create display
    disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);

    // lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // Set flush callback
    lv_display_set_flush_cb(disp, disp_flush);

    lv_display_set_default(disp);

    setRotation(LV_DISPLAY_ROTATION_90);

    lv_indev_t *indev = lv_indev_create();           /* Create input device connected to Default Display. */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /* Touch pad is a pointer-like device. */
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb); /* Set driver function. */

    // Allocate draw buffers
    size_t buf_size = DISP_HOR_RES * 240 * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    // void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    void *buf2 = NULL;

    if (buf1)
    {
        lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
        // lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
    }
}