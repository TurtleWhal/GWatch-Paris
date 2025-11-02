#include "ui.hpp"

void app_press(lv_event_t *e)
{
    watch.vibrate(80);
}

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
        lv_obj_add_event_cb(app, app_press, LV_EVENT_PRESSED, NULL);
    }
    else
    {
        lv_obj_set_style_opa(app, LV_OPA_50, 0);
        lv_obj_set_flag(app, LV_OBJ_FLAG_CLICKABLE, false);
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

    // lv_obj_t *battery = create_app(scr, FA_BATTERY);

    lv_obj_t *flashlight = create_app(scr, FA_FLASHLIGHT, [](lv_event_t *)
                                      {
                                    flashlight_prev = watch.display.get_brightness();

                                    lv_screen_load(flashlight_screen);

                                    watch.display.set_backlight(100);

                                    lv_obj_add_event_cb(flashlight_screen, [](lv_event_t *e)
                                                        {
                                                            watch.display.set_backlight(*(uint16_t *)lv_event_get_user_data(e));
                                                            lv_screen_load(main_screen); }, LV_EVENT_CLICKED, &flashlight_prev); });

    lv_obj_t *settings = create_app(scr, FA_SETTINGS);
    lv_obj_t *metronome = create_app(scr, FA_METRONOME);

    lv_obj_t *donotdisturb = create_app(scr, FA_DONOTDISTURB);
    lv_obj_t *rotate = create_app(scr, FA_ROTATE, [](lv_event_t *)
                                  {
                                      watch.display.set_rotation((lv_display_rotation_t)((lv_display_get_rotation(NULL) + 2) % 4));
                                      lv_obj_scroll_to_view(watchscr, LV_ANIM_ON); });

    lv_obj_t *wifi = create_app(scr, FA_WIFI, [](lv_event_t *)
                                { watch.wifi.connect();
                                lv_obj_scroll_to_view(watchscr, LV_ANIM_ON); });

    lv_obj_t *restart = create_app(scr, FA_POWEROFF, [](lv_event_t *)
                                   { esp_restart(); });

    lv_obj_align(metronome, LV_ALIGN_CENTER, POLAR(77, -30));    // Top Right
    lv_obj_align(flashlight, LV_ALIGN_CENTER, POLAR(77, -90));   // Top
    lv_obj_align(settings, LV_ALIGN_CENTER, POLAR(77, -150));    // Top Left
    lv_obj_align(donotdisturb, LV_ALIGN_CENTER, POLAR(77, 150)); // Bottom Left
    lv_obj_align(wifi, LV_ALIGN_CENTER, POLAR(77, 90));          // Bottom
    lv_obj_align(rotate, LV_ALIGN_CENTER, POLAR(77, 30));        // Bottom Right
    lv_obj_align(restart, LV_ALIGN_CENTER, 0, 0);                // Center

    return scr;
}

void apps_screen_update()
{
    // Update logic for apps screen goes here
}