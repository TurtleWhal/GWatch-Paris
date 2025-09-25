#include "watch.hpp"

#include "esp_timer.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
// #include "drivers/gc9a01_driver.h"
// #include "drivers/cst816s_driver.h"

/** Custom tick function for LVGL */
static uint32_t lvgl_tick_get_cb(void)
{
    return esp_timer_get_time() / 1000;
}

void lv_task(void *args)
{
    while (true)
    {
        // ui_update();
        lv_timer_handler();
        // uint32_t d = lv_timer_handler();
        // ESP_LOGI(TAG, "LVGL delay %d ms", d);
        // vTaskDelay(pdMS_TO_TICKS(d));
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

    // Allocate draw buffers
    size_t buf_size = LCD_WIDTH * 240 * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    // void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    void *buf2 = NULL;

    if (buf1)
    {
        // lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
    else
    {
        ESP_LOGE("graphics", "Failed to allocate LVGL draw buffers");
    }

    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_t *button = lv_button_create(scr);
    lv_obj_center(button);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Press Me");

    lv_screen_load(scr);

    lv_obj_add_event_cb(button, [](lv_event_t *e)
                        { ESP_LOGI("graphics", "button pressed"); }, LV_EVENT_PRESSED, NULL);

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        1024 * 8,
        NULL,
        0,
        &lv_task_handle,
        1);
}