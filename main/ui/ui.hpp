#include "watch.hpp"

#include "lvgl.h"
#include "fonts.h"

#include "math.h"

#define DEG2RAD 0.017453292519943295

// Radius, angle (degrees)
// 0˚ is pointing to the right, angle goes counter-clockwise
#define POLAR(r, t) cosf(DEG2RAD *t) * r, sinf(DEG2RAD *t) * r

extern lv_obj_t *main_screen;
extern lv_obj_t *hor_layer;
extern lv_obj_t *ver_layer;

extern lv_obj_t *watchscr;

lv_obj_t *create_screen(lv_obj_t *parent);
lv_obj_t *create_valuearc(lv_obj_t *parent, lv_color_t color, char *symbol);
lv_obj_t *create_app(lv_obj_t *parent, const char *icon, const char *name, lv_event_cb_t event_cb = nullptr);
lv_obj_t *create_app(lv_obj_t *parent, const char *icon, const char *name, lv_obj_t *screen, bool appsonly = false);
lv_obj_t *create_setting(lv_obj_t *parent, const char *name, bool state, lv_event_cb_t event_cb);

// lv_obj_t *rotarywatch_create(lv_obj_t *parent);
// void rotarywatch_update();

lv_obj_t *analogwatch_create(lv_obj_t *parent);
void analogwatch_update();

// lv_obj_t *timescreen_create(lv_obj_t *parent);
// void timescreen_update();

lv_obj_t *watchface_create(lv_obj_t *parent);

lv_obj_t *quicksettings_create(lv_obj_t *parent);
lv_obj_t *apps_screen_create(lv_obj_t *parent);

lv_obj_t *stopwatch_create(lv_obj_t *parent);
lv_obj_t *timerscr_create(lv_obj_t *parent);
lv_obj_t *imu_screen_create(lv_obj_t *parent);
lv_obj_t *calculator_create(lv_obj_t *parent);
lv_obj_t *dice_create(lv_obj_t *parent);

lv_obj_t *debugscreen_create();