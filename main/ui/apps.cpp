#include "ui.hpp"

void app_press(lv_event_t *e)
{
    watch.vibrate(80);
}

lv_obj_t *create_app(lv_obj_t *parent, const char *icon, const char *name, lv_event_cb_t event_cb = nullptr)
{
    lv_obj_t *app = lv_button_create(parent);
    lv_obj_set_size(app, 160, 44);
    lv_obj_set_style_bg_color(app, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(app, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *iconlabel = lv_label_create(app);
    lv_obj_align(iconlabel, LV_ALIGN_LEFT_MID, 0, 0);
    SET_SYMBOL_22(iconlabel, icon);

    lv_obj_t *label = lv_label_create(app);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 36, 0);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ProductSansRegular_20, 0);

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
    // lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *appslabel = lv_label_create(scr);
    lv_label_set_text(appslabel, "Apps");
    lv_obj_set_style_text_font(appslabel, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_color(appslabel, lv_color_white(), 0);
    // lv_obj_set_style_text_align(appslabel, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_pad_top(appslabel, 12, 0);
    lv_obj_set_style_pad_bottom(appslabel, 8, 0);

    flashlight_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(flashlight_screen, lv_color_white(), 0);

    create_app(scr, FA_FLASHLIGHT, "Flashlight", [](lv_event_t *)
               {
                                    flashlight_prev = watch.display.get_brightness();

                                    lv_screen_load(flashlight_screen);

                                    watch.display.set_backlight(100);

                                    lv_obj_add_event_cb(flashlight_screen, [](lv_event_t *e)
                                                        {
                                                            watch.display.set_backlight(*(uint16_t *)lv_event_get_user_data(e));
                                                            lv_screen_load(main_screen); }, LV_EVENT_CLICKED, &flashlight_prev); });

    create_app(scr, FA_SETTINGS, "Settings");
    create_app(scr, FA_METRONOME, "Metronome");
    create_app(scr, FA_SETTINGS, "Settings");
    create_app(scr, FA_METRONOME, "Metronome");
    create_app(scr, FA_SETTINGS, "Settings");
    create_app(scr, FA_METRONOME, "Metronome");
    create_app(scr, FA_SETTINGS, "Settings");
    create_app(scr, FA_METRONOME, "Metronome");

    lv_obj_t *spacer = lv_obj_create(scr);
    lv_obj_set_style_opa(spacer, LV_OPA_0, 0);
    lv_obj_set_size(spacer, 0, 20);

    return scr;
}

void apps_screen_update()
{
    // Update logic for apps screen goes here
}