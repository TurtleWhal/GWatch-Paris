#include "hal_sdl.h"

static lv_display_t *create_window_with_mouse(int32_t w, int32_t h)
{
    lv_display_t *disp = lv_sdl_window_create(w, h);

    /* The SDL mouse driver routes events to whichever indev is bound to
     * the display whose SDL window has focus, so we need one indev per
     * window. lv_indev_set_display is the binding. */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, disp);

    return disp;
}

lv_display_t *sdl_hal_init(int32_t w, int32_t h)
{
    lv_group_set_default(lv_group_create());

    lv_display_t *disp = create_window_with_mouse(w, h);
    lv_display_set_default(disp);

    /* Mouse-wheel + keyboard indevs only attached to the primary window —
     * sidebar doesn't need scroll-by-wheel or key input. */
    lv_indev_t *mw = lv_sdl_mousewheel_create();
    lv_indev_set_display(mw, disp);
    lv_indev_set_group(mw, lv_group_get_default());

    lv_indev_t *kb = lv_sdl_keyboard_create();
    lv_indev_set_display(kb, disp);
    lv_indev_set_group(kb, lv_group_get_default());

    return disp;
}

lv_display_t *sdl_hal_create_window(int32_t w, int32_t h, const char *title)
{
    lv_display_t *disp = create_window_with_mouse(w, h);
    if (title) lv_sdl_window_set_title(disp, title);
    return disp;
}
