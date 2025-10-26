#include "watch.hpp"

#include "esp_timer.h"
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "time.h"
#include <sys/time.h>
#include <esp_task_wdt.h>

lv_obj_t *timelbl;
lv_obj_t *wifilbl;

/** Custom tick function for LVGL */
static uint32_t lvgl_tick_get_cb(void)
{
    return esp_timer_get_time() / 1000;
}

void lv_task(void *args)
{
    ESP_LOGI("lv_task", "started");

    while (true)
    {
        uint32_t d = lv_timer_handler();
        watch.display.lvgl_done();

        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

/** Initialise LVGL */
void Display::init_graphics()
{
    lv_init();

    // Set custom tick function
    lv_tick_set_cb(lvgl_tick_get_cb);

    // Create display
    disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    // Set flush callback
    lv_display_set_flush_cb(disp, lvgl_flush);

    lv_display_set_default(disp);

    set_rotation(LV_DISPLAY_ROTATION_90);

    lv_indev_t *indev = lv_indev_create();           /* Create input device connected to Default Display. */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /* Touch pad is a pointer-like device. */
    lv_indev_set_read_cb(indev, lvgl_touch_read);    /* Set driver function. */

    // ESP_LOGI("graphics", "Available DMA Memory Before: %dkB", heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1000);

    // Allocate draw buffers
    size_t buf_size = LCD_WIDTH * (240 / 1.5) * sizeof(lv_color_t); // 240 * 240 * 2 = 115,200 Bytes
    // size_t buf_size = LCD_WIDTH * (240) * sizeof(lv_color_t); // 240 * 240 * 2 = 115,200 Bytes
    // void *buf1 = heap_caps_aligned_alloc(8, buf_size, MALLOC_CAP_DMA);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    // void *buf2 = NULL;

    // ESP_LOGI("graphics", "Available DMA Memory After: %dkB", heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1000);

    if (buf1)
    {
        // lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else
    {
        ESP_LOGE("graphics", "Failed to allocate LVGL draw buffers");
    }

    ui_init();

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        1024 * 8,
        NULL,
        1,
        &lv_task_handle,
        1);
}

/** Force update and redraw graphics */
void Display::refresh()
{
    ui_update();

    // for now no need to redraw entire screen
    // lv_obj_invalidate(lv_screen_active()); // Mark screen dirty
    lv_task_handler(); // Process drawing immediately
}