#include "ui.hpp"

lv_obj_t *main_screen;
lv_obj_t *ver_layer;

lv_obj_t *create_screen(lv_obj_t *parent)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    lv_obj_set_size(scr, 240, 240);
    lv_obj_set_style_radius(scr, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_margin_all(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    return scr;
}

lv_obj_t *create_valuearc(lv_obj_t *parent, lv_color_t color, char *symbol)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 60, 60);

    /* Background arc */
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), 0);
    lv_obj_set_style_arc_width(arc, 8, 0);

    /* Indicator arc */
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);

    /* Knob invisible */
    lv_obj_set_style_opa(arc, LV_OPA_0, LV_PART_KNOB);

    lv_obj_set_flag(arc, LV_OBJ_FLAG_CLICKABLE, false);

    /* Icon */
    lv_obj_t *icon = lv_label_create(arc);
    SET_SYMBOL_14(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_set_name(icon, "icon");

    /* Value text */
    lv_obj_t *value = lv_label_create(arc);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);
    lv_obj_set_style_text_font(value, &ProductSansRegular_14, 0);
    lv_obj_set_name(value, "text");

    return arc;
}

lv_obj_t *watchscr;

void Display::ui_init()
{
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), 0);

    lv_obj_set_style_margin_all(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);

    lv_obj_set_style_border_width(main_screen, 0, 0);

    rotarywatch_create(main_screen);
    // analogwatch_create(main_screen);

    ver_layer = lv_obj_create(main_screen);
    lv_obj_set_size(ver_layer, 240, 240);
    lv_obj_set_style_bg_opa(ver_layer, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ver_layer, 0, 0);

    lv_obj_set_flex_flow(ver_layer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_snap_y(ver_layer, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(ver_layer, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
    lv_obj_set_scrollbar_mode(ver_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(ver_layer, 0, 0);
    lv_obj_set_style_pad_column(ver_layer, 0, 0);
    lv_obj_set_flag(ver_layer, LV_OBJ_FLAG_SCROLL_ONE, true);

    lv_obj_set_style_margin_all(ver_layer, 0, 0);
    lv_obj_set_style_pad_all(ver_layer, 0, 0);

    lv_obj_t *quicksettings = quicksettings_create();

    lv_obj_add_event_cb(ver_layer, [](lv_event_t *e)
                        {
                            lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);            // apps screen
                            int32_t scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e)); // scroll of screens layer
                            int32_t height = lv_obj_get_height(scr);

                            // Map scroll to 0-50 for v of hsv (even though value is actually 0-100, we want it to be grayish not white)
                            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, scroll * 20 / height), 0); }, LV_EVENT_SCROLL, quicksettings);

    watchscr = lv_obj_create(ver_layer);
    lv_obj_set_size(watchscr, 240, 240);
    lv_obj_set_style_bg_opa(watchscr, LV_OPA_0, 0);
    lv_obj_set_style_border_width(watchscr, 0, 0);
    lv_obj_set_style_radius(watchscr, 0, 0);

    lv_obj_set_style_margin_all(watchscr, 0, 0);
    lv_obj_set_style_pad_all(watchscr, 0, 0);

    lv_obj_t *appsscreen = apps_screen_create();

    lv_obj_add_event_cb(ver_layer, [](lv_event_t *e)
                        {
                            lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);            // apps screen
                            int32_t scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e)); // scroll of screens layer
                            int32_t height = lv_obj_get_height(scr);
                            int32_t pos = lv_obj_get_y(scr);

                            // Map scroll to 0-50 for v of hsv (even though value is actually 0-100, we want it to be grayish not white)
                            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, (pos - scroll) * 20 / height), 0); }, LV_EVENT_SCROLL, appsscreen);

    lv_timer_create([](lv_timer_t *timer)
                    {
                        auto *obj = static_cast<Display *>(lv_timer_get_user_data(timer));
                        obj->ui_update(); },
                    33, this);

    lv_screen_load(main_screen);

    lv_obj_scroll_to_view(watchscr, LV_ANIM_OFF);
}

void Display::ui_update()
{
    // if (lv_obj_get_scroll_y(screens_layer) == lv_obj_get_y(watchscr)) // if screen is centered
    if (lv_obj_get_scroll_y(ver_layer) > lv_obj_get_y(watchscr) - 240 && lv_obj_get_scroll_y(ver_layer) < lv_obj_get_y(watchscr) + 240) // if screen is displayed at all
        rotarywatch_update();
    // analogwatch_update();
}