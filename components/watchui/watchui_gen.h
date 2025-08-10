/**
 * @file watchui_gen.h
 */

#ifndef WATCHUI_GEN_H
#define WATCHUI_GEN_H

#ifndef UI_SUBJECT_STRING_LENGTH
#define UI_SUBJECT_STRING_LENGTH 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

#define ACCENT lv_color_hex(0xffaa00)

#define GRAY lv_color_hex(0x333333)

/**********************
 *      TYPEDEFS
 **********************/



/**********************
 * GLOBAL VARIABLES
 **********************/

/*-------------------
 * Permanent screens
 *------------------*/
extern lv_obj_t * watchscreen;

/*----------------
 * Global styles
 *----------------*/

extern lv_style_t screen;
extern lv_style_t hidden;

/*----------------
 * Fonts
 *----------------*/
extern lv_font_t * GoogleSans_10;
extern lv_font_t * GoogleSans_14;
extern lv_font_t * GoogleSansBold_24;
extern lv_font_t * GoogleSansBold_42;

/*----------------
 * Images
 *----------------*/

/*----------------
 * Subjects
 *----------------*/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

/**
 * Initialize the component library
 */

void watchui_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

/**********************
 *   POST INCLUDES
 **********************/

/*Include all the widget and components of this library*/
#include "components/timescreen_gen.h"
#include "screens/watchscreen_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*WATCHUI_GEN_H*/