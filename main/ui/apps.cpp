#include "ui.hpp"

lv_obj_t *create_app(lv_obj_t *parent, const char *icon, lv_event_cb_t event_cb = nullptr)
{
    lv_obj_t *app = lv_button_create(parent);
    lv_obj_set_size(app, 65, 65);
    lv_obj_set_style_bg_color(app, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(app, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label = lv_label_create(app);
    lv_obj_center(label);
    SET_SYMBOL_28(label, icon);

    if (event_cb != nullptr)
    {
        lv_obj_add_event_cb(app, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return app;
}

uint16_t flashlight_prev;
lv_obj_t *flashlight_screen;

lv_obj_t *apps_screen_create()
{
    lv_obj_t *scr = create_screen();
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    flashlight_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(flashlight_screen, lv_color_white(), 0);

    lv_obj_t *app1 = create_app(scr, FA_BATTERY); // TOP RIGHT

    lv_obj_t *app2 = create_app(scr, FA_FLASHLIGHT, [](lv_event_t *)
                                {
                                    flashlight_prev = watch.display.get_brightness();

                                    lv_screen_load(flashlight_screen);

                                    watch.display.set_backlight(100);

                                    lv_obj_add_event_cb(flashlight_screen, [](lv_event_t *e)
                                                        {
                                                            watch.display.set_backlight(*(uint16_t *)lv_event_get_user_data(e));
                                                            lv_screen_load(main_screen); }, LV_EVENT_CLICKED, &flashlight_prev); }); // TOP

    lv_obj_t *app3 = create_app(scr, FA_SETTINGS);     // TOP LEFT
    lv_obj_t *app4 = create_app(scr, FA_DONOTDISTURB); // BOTTOM LEFT
    lv_obj_t *app5 = create_app(scr, FA_STEPS);        // BOTTOM

    lv_obj_t *app6 = create_app(scr, FA_WIFI, [](lv_event_t *)
                                { watch.wifi.connect();
                                lv_obj_scroll_to_view(watchscr, LV_ANIM_ON); }); // BOTTOM RIGHT

    lv_obj_t *app7 = create_app(scr, FA_POWEROFF, [](lv_event_t *)
                                { esp_restart(); }); // CENTER

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