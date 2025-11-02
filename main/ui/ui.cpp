#include "ui.hpp"

lv_obj_t *main_screen;

lv_obj_t *create_screen()
{
    lv_obj_t *scr = lv_obj_create(main_screen);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(scr, LV_OPA_0, 0);

    lv_obj_set_size(scr, 240, 240);
    lv_obj_set_style_radius(scr, 0, 0);
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
lv_obj_t *appsscreen;

void Display::ui_init()
{
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(main_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_snap_y(main_screen, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(main_screen, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_size(main_screen, 240, 240);
    lv_obj_set_style_radius(main_screen, 0, 0);
    lv_obj_set_style_margin_all(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);

    appsscreen = apps_screen_create();
    // watchscr = analogwatch_create();
    watchscr = rotarywatch_create();

    lv_timer_create([](lv_timer_t *timer)
                    {
                        auto *obj = static_cast<Display *>(lv_timer_get_user_data(timer));
                        obj->ui_update(); },
                    100, this);

    lv_screen_load(main_screen);

    lv_obj_scroll_to_view(watchscr, LV_ANIM_OFF);
}

void Display::ui_update()
{
    if (lv_obj_get_scroll_y(main_screen) == lv_obj_get_y(watchscr))
        rotarywatch_update();
    // analogwatch_update();

    // if (lv_scr_act() == watchscr)
    //     watchscr_update();
    // else
    //     wifiscr_update();
}