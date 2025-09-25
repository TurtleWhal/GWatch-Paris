#include "driver/i2c_master.h"
#include "lvgl.h"

#ifndef DISPLAY_H
#define DISPLAY_H

// Display dimensions
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

#ifdef __cplusplus

void lvgl_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void lvgl_touch_read(lv_indev_t *indev, lv_indev_data_t *touch);

class Display
{
private:
    void backlight_update();

    uint16_t bgval;
    TaskHandle_t backlight_handle = NULL;

    uint16_t old_backlight = 0;
    uint32_t endtime = 0;
    uint32_t starttime = 0;
    float k = 0;
    bool adjust = false;

    lv_display_t *disp;
    TaskHandle_t lv_task_handle = NULL;

public:
    void init(i2c_master_bus_handle_t bus);
    void init_graphics();

    void sleep();
    void wake();

    bool is_touching();

    void set_rotation(lv_display_rotation_t rotation);

    void set_backlight_gradual(int16_t val, uint32_t ms);
    void set_backlight(int16_t val);
    uint16_t get_brightness();
};

#endif // __cplusplus

#endif // DISPLAY_H