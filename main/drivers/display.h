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

void display_init(void);

void gc9a01_driver_init(void);

void gc9a01_set_addr_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void gc9a01_send_data(const uint8_t *data, int len);

void gc9a01_sleep(void);
void gc9a01_wake(void);

void cst816s_init(void);
bool cst816s_available(void);
touch_data cst816s_touch_read(void);