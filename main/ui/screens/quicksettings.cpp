#include "ui.hpp"

void press(lv_event_t *e)
{
    haptic_play(false, 80, 0);
}

lv_obj_t *create_setting(lv_obj_t *parent, const char *icon, lv_event_cb_t event_cb = nullptr)
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
        lv_obj_add_event_cb(app, press, LV_EVENT_PRESSED, NULL);
    }
    else
    {
        lv_obj_set_style_opa(app, LV_OPA_50, 0);
        lv_obj_set_flag(app, LV_OBJ_FLAG_CLICKABLE, false);
    }

    return app;
}

lv_obj_t *quicksettings_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    // lv_obj_t *settings = create_setting(scr, FA_SETTINGS);
    lv_obj_t *restart = create_setting(scr, FA_POWEROFF, [](lv_event_t *)
                                       { esp_restart(); });

    lv_obj_t *donotdisturb = create_setting(scr, FA_DONOTDISTURB, [](lv_event_t *e)
                                            {
                                                watch.donotdisturb = !watch.donotdisturb;
                                                lv_obj_set_state(lv_event_get_target_obj(e), LV_STATE_CHECKED, watch.donotdisturb); });

    lv_obj_t *rotate = create_setting(scr, FA_ROTATE, [](lv_event_t *)
                                      {
                                      watch.display.set_rotation((lv_display_rotation_t)((lv_display_get_rotation(NULL) + 2) % 4));
                                      lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_ON); });

    lv_obj_t *wifi = create_setting(scr, FA_WIFI, [](lv_event_t *)
                                    { watch.wifi.connect();
        lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_ON); });

#define ARC_RADIUS 77
#define KNOB_THICKNESS 65
#define ARC_THICKNESS 12
// #define INDICATOR_THICKNESS 20
#define INDICATOR_THICKNESS 65

    lv_obj_t *brightness = lv_arc_create(scr);
    lv_obj_set_size(brightness, ARC_RADIUS * 2 + KNOB_THICKNESS + 4, ARC_RADIUS * 2 + KNOB_THICKNESS + 4);
    lv_obj_align(brightness, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_width(brightness, ARC_THICKNESS, 0);
    lv_obj_set_style_arc_width(brightness, INDICATOR_THICKNESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(brightness, lv_color_hex(0x222222), 0);
    // lv_obj_set_style_arc_color(brightness, lv_color_hex(0x222222), LV_PART_INDICATOR);

    lv_obj_set_style_pad_all(brightness, (KNOB_THICKNESS - ARC_THICKNESS) / 2 + 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(brightness, -(INDICATOR_THICKNESS - ARC_THICKNESS) / 2 + 2, LV_PART_INDICATOR);

    lv_obj_set_style_opa(brightness, LV_OPA_0, LV_PART_KNOB);
    // lv_obj_set_style_pad_all(brightness, 10, LV_PART_KNOB);
    // lv_obj_set_style_pad_bottom(brightness, 0, LV_PART_KNOB);

    lv_arc_set_bg_angles(brightness, 270 - 60, 270 + 60);
    lv_arc_set_angles(brightness, 270 - 60, 270 + 60);
    lv_arc_set_range(brightness, 2, 100);
    lv_arc_set_value(brightness, 100);

    lv_obj_set_flag(brightness, LV_OBJ_FLAG_ADV_HITTEST, true); // allow touches through the arc so that buttons can be pressed

    lv_obj_t *knob = lv_obj_create(brightness);
    lv_obj_set_style_border_width(knob, 0, 0);
    lv_obj_set_style_bg_color(knob, lv_theme_get_color_primary(brightness), 0);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(knob, KNOB_THICKNESS, KNOB_THICKNESS);
    lv_obj_set_flag(knob, LV_OBJ_FLAG_CLICKABLE, false);
    lv_obj_set_scroll_dir(knob, LV_DIR_NONE);

    lv_obj_t *brightnessicon = lv_label_create(knob);
    SET_SYMBOL_28(brightnessicon, FA_BRIGHTNESS);
    lv_obj_align(brightnessicon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(brightnessicon, lv_color_white(), 0);

    // lv_arc_align_obj_to_angle(brightness, knob, ARC_THICKNESS / 2 - 2);
    lv_arc_align_obj_to_angle(brightness, knob, INDICATOR_THICKNESS / 2 - 8);

    lv_obj_add_event_cb(brightness, [](lv_event_t *e)
                        {
                            haptic_play(false, 10, 0);

                            lv_obj_t *arc = lv_event_get_target_obj(e);
                            int32_t value = lv_arc_get_value(arc);
                            watch.display.set_backlight(value);

                            if (value > 40)
                            {
                                SET_SYMBOL_28(lv_obj_get_child((lv_obj_t *)lv_event_get_user_data(e), 0), FA_BRIGHTNESS);
                            }
                            else
                            {
                                SET_SYMBOL_28(lv_obj_get_child((lv_obj_t *)lv_event_get_user_data(e), 0), FA_BRIGHTNESS_LOW);
                            }

                            lv_arc_align_obj_to_angle(arc, (lv_obj_t *)lv_event_get_user_data(e), INDICATOR_THICKNESS / 2 - 8);
                            // lv_arc_align_obj_to_angle(arc, (lv_obj_t *)lv_event_get_user_data(e), ARC_THICKNESS / 2 - 2);
                        },
                        LV_EVENT_VALUE_CHANGED, knob);

    // lv_obj_align(metronome, LV_ALIGN_CENTER, POLAR(77, -30));    // Top Right
    // lv_obj_align(flashlight, LV_ALIGN_CENTER, POLAR(77, -90));   // Top
    // lv_obj_align(settings, LV_ALIGN_CENTER, POLAR(77, -150));    // Top Left

    lv_obj_align(donotdisturb, LV_ALIGN_CENTER, POLAR(77, 150)); // Bottom Left
    lv_obj_align(wifi, LV_ALIGN_CENTER, POLAR(77, 90));          // Bottom
    lv_obj_align(rotate, LV_ALIGN_CENTER, POLAR(77, 30));        // Bottom Right
    lv_obj_align(restart, LV_ALIGN_CENTER, 0, 0);                // Center

    return scr;
}