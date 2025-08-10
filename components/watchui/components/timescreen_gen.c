/**
 * @file timescreen_gen.c
 * @description Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/
#include "timescreen_gen.h"
#include "ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t *timescreen_create(lv_obj_t *parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t scale_main;
    static lv_style_t scale_indicator;
    static lv_style_t scale_items;

    static bool style_inited = false;

    if (!style_inited)
    {
        lv_style_init(&scale_main);
        lv_style_set_text_color(&scale_main, lv_color_hex(0xffffff));
        lv_style_set_arc_width(&scale_main, 0);
        lv_style_set_text_font(&scale_main, GoogleSans_10);

        lv_style_init(&scale_indicator);
        lv_style_set_line_color(&scale_indicator, lv_color_hex(0xFFFFFF));
        lv_style_set_height(&scale_indicator, 8);
        lv_style_set_line_width(&scale_indicator, 2);

        lv_style_init(&scale_items);
        lv_style_set_line_color(&scale_items, lv_color_hex(0xFFFFFF));
        lv_style_set_height(&scale_items, 4);
        lv_style_set_line_width(&scale_items, 2);

        style_inited = true;
    }

    lv_obj_t *lv_obj_0 = lv_obj_create(parent);
    lv_obj_add_style(lv_obj_0, &screen, 0);

    lv_obj_t *lv_label_0 = lv_label_create(lv_obj_0);
    lv_obj_set_style_text_font(lv_label_0, GoogleSansBold_42, 0);
    lv_label_set_text(lv_label_0, "09");
    lv_obj_set_style_text_color(lv_label_0, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_align(lv_label_0, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(lv_label_0, -6);

    lv_obj_t *lv_obj_1 = lv_obj_create(lv_obj_0);
    lv_obj_set_style_border_color(lv_obj_1, ACCENT, 0);
    lv_obj_set_style_bg_opa(lv_obj_1, 0, 0);
    lv_obj_set_style_border_width(lv_obj_1, 2, 0);
    lv_obj_set_height(lv_obj_1, 36);
    lv_obj_set_style_radius(lv_obj_1, 18, 0);
    lv_obj_set_width(lv_obj_1, 80);
    lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_FLOATING, true);
    lv_obj_set_align(lv_obj_1, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(lv_obj_1, 50);

    lv_obj_t *lv_label_1 = lv_label_create(lv_obj_0);
    lv_obj_set_style_text_font(lv_label_1, GoogleSansBold_24, 0);
    lv_label_set_text(lv_label_1, "17");
    lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_align(lv_label_1, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(lv_label_1, 60);

    lv_obj_t *scale = lv_scale_create(lv_obj_0);
    lv_obj_set_name(scale, "scale");
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(scale, 60);
    lv_scale_set_major_tick_every(scale, 5);
    lv_obj_set_align(scale, LV_ALIGN_CENTER);

    lv_obj_add_style(scale, &scale_main, 0);
    lv_obj_add_style(scale, &scale_items, LV_PART_ITEMS);
    lv_obj_add_style(scale, &scale_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    lv_obj_set_name(lv_obj_0, "timescreen_#");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/