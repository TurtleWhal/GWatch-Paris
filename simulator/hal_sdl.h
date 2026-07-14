#ifndef SIM_HAL_SDL_H
#define SIM_HAL_SDL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Primary watch window. Creates an SDL window, an LVGL display backing it,
 * and a mouse indev bound to that display. Marks the display as default —
 * `lv_obj_create(NULL)` calls in the firmware will land on this display
 * (so `main_screen`, popup screens, etc. stay 240×240). */
lv_display_t *sdl_hal_init(int32_t w, int32_t h);

/* Secondary SDL window for simulator chrome (sidebar with test buttons).
 * Gets its own LVGL display + mouse indev so clicks on it don't leak into
 * the watch's coordinate space. Does NOT change the default display. */
lv_display_t *sdl_hal_create_window(int32_t w, int32_t h, const char *title);

#ifdef __cplusplus
}
#endif

#endif
