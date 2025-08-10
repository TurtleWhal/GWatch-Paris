/**
 * @file watchscreen_gen.c
 * @description Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/
#include "watchscreen_gen.h"
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

lv_obj_t * watchscreen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t basescreen;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&basescreen);
        lv_style_set_bg_color(&basescreen, lv_color_hex(0x000000));
        lv_style_set_flex_main_place(&basescreen, LV_FLEX_ALIGN_CENTER);
        lv_style_set_flex_track_place(&basescreen, LV_FLEX_ALIGN_CENTER);
        lv_style_set_flex_cross_place(&basescreen, LV_FLEX_ALIGN_CENTER);

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_snap_y(lv_obj_0, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLL_ONE, true);
    lv_obj_add_style(lv_obj_0, &basescreen, 0);

    lv_obj_t * timescreen_0 = timescreen_create(lv_obj_0);



    LV_TRACE_OBJ_CREATE("finished");

    lv_obj_set_name(lv_obj_0, "watchscreen");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/