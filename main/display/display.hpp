#include "driver/i2c_master.h"

// Display dimensions
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

// void cst816s_init(i2c_master_bus_handle_t bus);
// bool cst816s_available(void);
// touch_data cst816s_touch_read(void);

#ifdef __cplusplus

class Display
{
private:
    void backlight_update();

    uint16_t bgval;
    TaskHandle_t backlightHandle = NULL;

    uint16_t oldBacklight = 0;
    uint32_t endtime = 0;
    uint32_t starttime = 0;
    float k = 0;
    bool adjust = false;

public:
    void init(i2c_master_bus_handle_t bus);
    void sleep();
    void wake();

    void set_backlight_gradual(int16_t val, uint32_t ms);
    void set_backlight(int16_t val);
    uint16_t get_brightness();
};

#endif // __cplusplus