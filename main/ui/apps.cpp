#include "ui.hpp"

lv_obj_t *create_app(lv_obj_t *parent, const char *icon)
{
    lv_obj_t *app = lv_button_create(parent);
    lv_obj_set_size(app, 65, 65);
    lv_obj_set_style_bg_color(app, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(app, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label = lv_label_create(app);
    lv_obj_center(label);
    SET_SYMBOL_28(label, icon);

    return app;
}

lv_obj_t *apps_screen_create()
{
    lv_obj_t *scr = create_screen();
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_t *app1 = create_app(scr, FA_BATTERY);
    lv_obj_t *app2 = create_app(scr, FA_FLASHLIGHT);
    lv_obj_t *app3 = create_app(scr, FA_BLUETOOTH);
    lv_obj_t *app4 = create_app(scr, FA_DONOTDISTURB);
    lv_obj_t *app5 = create_app(scr, FA_STEPS);
    lv_obj_t *app6 = create_app(scr, FA_WIFI);
    lv_obj_t *app7 = create_app(scr, FA_SETTINGS);

    lv_obj_align(app1, LV_ALIGN_CENTER, POLAR(77, -30));
    lv_obj_align(app2, LV_ALIGN_CENTER, POLAR(77, -90));
    lv_obj_align(app3, LV_ALIGN_CENTER, POLAR(77, -150));
    lv_obj_align(app4, LV_ALIGN_CENTER, POLAR(77, 150));
    lv_obj_align(app5, LV_ALIGN_CENTER, POLAR(77, 90));
    lv_obj_align(app6, LV_ALIGN_CENTER, POLAR(77, 30));
    lv_obj_align(app7, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

void apps_screen_update()
{
    // Update logic for apps screen goes here
}