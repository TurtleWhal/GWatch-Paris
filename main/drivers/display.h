#include "driver/i2c_master.h"

// Display dimensions
#define DISP_HOR_RES 240
#define DISP_VER_RES 240

typedef struct touch_data
{
    uint8_t gestureID; // Gesture ID
    uint8_t points;    // Number of touch points
    uint8_t event;     // Event (0 = Down, 1 = Up, 2 = Contact)
    int x;
    int y;
    uint8_t version;
    uint8_t versionInfo[3];
} touch_data;

void display_init(i2c_master_bus_handle_t bus);

void cst816s_init(i2c_master_bus_handle_t bus);
bool cst816s_available(void);
touch_data cst816s_touch_read(void);