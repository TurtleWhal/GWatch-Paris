#include "watch.hpp"

#include "esp_timer.h"
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "time.h"
#include <sys/time.h>

lv_obj_t *timelbl;
lv_obj_t *wifilbl;

/** Custom tick function for LVGL */
static uint32_t lvgl_tick_get_cb(void)
{
    return esp_timer_get_time() / 1000;
}

/** Update all ui elements */
void ui_update()
{
    switch (watch.wifi.status)
    {
    case WIFI_CONNECTED:
        lv_label_set_text(wifilbl, "wifi connected");
        break;
    case WIFI_CONNECTING:
        lv_label_set_text(wifilbl, "wifi connecting");
        break;
    case WIFI_DISCONNECTED:
        lv_label_set_text(wifilbl, "wifi not connected");
        break;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
    lv_label_set_text_fmt(timelbl, "ms: %lld", ms);
}

static void ui_update_cb(lv_timer_t *timer)
{
    ui_update();
}

void lv_task(void *args)
{
    while (true)
    {
        lv_timer_handler();
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
    size_t buf_size = LCD_WIDTH * (240 / 2) * sizeof(lv_color_t); // 240 * 240 * 2 = 115,200 Bytes
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

    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);

    timelbl = lv_label_create(scr);
    lv_label_set_text(timelbl, "time");

    lv_obj_t *button = lv_button_create(scr);
    // lv_obj_center(button);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Press Me");

    wifilbl = lv_label_create(scr);
    lv_label_set_text(wifilbl, "wifi");

    lv_screen_load(scr);

    lv_obj_add_event_cb(button, [](lv_event_t *e)
                        { ESP_LOGI("graphics", "button pressed"); }, LV_EVENT_PRESSED, NULL);

    lv_timer_create(ui_update_cb, 1000, NULL); // call every 1s

    xTaskCreatePinnedToCore(
        lv_task,
        "lv_task",
        1024 * 8,
        NULL,
        0,
        &lv_task_handle,
        1);
}

/** Force update and redraw graphics */
void Display::refresh()
{
    ui_update();

    lv_obj_invalidate(lv_screen_active()); // Mark screen dirty
    lv_task_handler();                     // Process drawing immediately
}