#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#include "fonts.h"

#ifndef DISPLAY_H
#define DISPLAY_H

// Display dimensions
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

#ifdef __cplusplus

class Display
{
private:
    void backlight_update();
    void init_graphics();

    void ui_init();

    uint16_t bgval;
    TaskHandle_t backlight_handle = NULL;

    uint16_t old_backlight = 0;
    uint32_t endtime = 0;
    uint32_t starttime = 0;
    float k = 0;
    bool adjust = false;

    lv_display_t *disp;

    // Owned by esp_lvgl_port; we keep the handles so we can drive sleep/rotation
    // directly without going back through it.
    esp_lcd_panel_io_handle_t  panel_io = NULL;
    esp_lcd_panel_handle_t     panel    = NULL;
    esp_lcd_panel_io_handle_t  tp_io    = NULL;
    esp_lcd_touch_handle_t     touch    = NULL;

    bool suspended = false;

public:
    void init(i2c_master_bus_handle_t bus);
    void init_memory();

    void sleep();
    void wake();

    void refresh();

    bool is_touching();
    void reset_touch();

    void set_rotation(lv_display_rotation_t rotation);

    void set_backlight_gradual(int16_t val, uint32_t ms);
    void set_backlight(int16_t val, bool stopgrad = true);
    uint16_t get_brightness();

    void set_wakeup_touch(bool enable);

    // Toggle the CST816S between active polling (DisAutoSleep=1, used while
    // the watch is awake — needed to keep is_touching() polls from NACKing)
    // and its auto-sleep mode (DisAutoSleep=0, used during light sleep so
    // the INT line stops pulsing periodically and GPIO5 becomes a clean
    // wake source).
    void set_touch_active(bool active);

    void lvgl_done();
};

#endif // __cplusplus

#endif // DISPLAY_H
