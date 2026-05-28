#include "hal_sdl.h"

lv_display_t *sdl_hal_init(int32_t w, int32_t h)
{
    lv_group_set_default(lv_group_create());

    lv_display_t *disp = lv_sdl_window_create(w, h);

    /* Mouse acts as the touch input. The watch's CST816S indev hands
     * single-point clicks/drags to LVGL; the SDL mouse driver matches
     * that semantic exactly. */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, disp);
    lv_indev_set_group(mouse, lv_group_get_default());

    lv_indev_t *mousewheel = lv_sdl_mousewheel_create();
    lv_indev_set_display(mousewheel, disp);
    lv_indev_set_group(mousewheel, lv_group_get_default());

    lv_indev_t *kb = lv_sdl_keyboard_create();
    lv_indev_set_display(kb, disp);
    lv_indev_set_group(kb, lv_group_get_default());

    return disp;
}
