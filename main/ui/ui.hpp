#include "watch.hpp"

#include "lvgl.h"
#include "fonts.h"

#include "math.h"

#define DEG2RAD 0.017453292519943295

// Radius, angle (degrees)
// 0˚ is pointing to the right, angle goes counter-clockwise
#define POLAR(r, t) cosf(DEG2RAD *t) * r, sinf(DEG2RAD *t) * r

extern lv_obj_t *main_screen;

extern lv_obj_t *watchscr;
extern lv_obj_t *appsscreen;

lv_obj_t *create_screen();
lv_obj_t *create_valuearc(lv_obj_t *parent, lv_color_t color, char *symbol);

lv_obj_t *rotarywatch_create();
void rotarywatch_update();

lv_obj_t *analogwatch_create();
void analogwatch_update();

// lv_obj_t *wifiscr_create();
// void wifiscr_update();

lv_obj_t *apps_screen_create();
void apps_screen_update();