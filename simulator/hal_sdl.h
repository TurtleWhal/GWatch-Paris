#ifndef SIM_HAL_SDL_H
#define SIM_HAL_SDL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *sdl_hal_init(int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif

#endif
