#include "system.h"

#include "ui.h"

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
lv_obj_t *wifiscr;

void ui_init()
{
    watchscr = watchscr_create();
    wifiscr = wifiscr_create();

    lv_screen_load(watchscr);
}

void ui_update()
{
    watchscr_update();
    // if (lv_scr_act() == watchscr)
    //     watchscr_update();
    // else
    //     wifiscr_update();
}