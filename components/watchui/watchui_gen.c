/**
 * @file watchui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "watchui_gen.h"

#if LV_USE_XML
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/*----------------
 * Translations
 *----------------*/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/
lv_obj_t * watchscreen;

/*----------------
 * Global styles
 *----------------*/
lv_style_t screen;
lv_style_t hidden;

/*----------------
 * Fonts
 *----------------*/
lv_font_t * GoogleSans_10;
extern lv_font_t GoogleSans_10_data;
lv_font_t * GoogleSans_14;
extern lv_font_t GoogleSans_14_data;
lv_font_t * GoogleSansBold_24;
extern lv_font_t GoogleSansBold_24_data;
lv_font_t * GoogleSansBold_42;
extern lv_font_t GoogleSansBold_42_data;

/*----------------
 * Images
 *----------------*/

/*----------------
 * Subjects
 *----------------*/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void watchui_init_gen(const char * asset_path)
{
    char buf[256];

    /*----------------
     * Global styles
     *----------------*/
    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&screen);
        lv_style_set_width(&screen, 240);
        lv_style_set_height(&screen, 240);
        lv_style_set_bg_opa(&screen, 255);
        lv_style_set_bg_color(&screen, lv_color_hex(0x000000));
        lv_style_set_radius(&screen, 120);
        lv_style_set_border_opa(&screen, 0);

        lv_style_init(&hidden);
        lv_style_set_opa(&hidden, 0);

        style_inited = true;
    }

    /*----------------
     * Fonts
     *----------------*/
    /* get font 'GoogleSans_10' from a C array */
    GoogleSans_10 = &GoogleSans_10_data;
    /* get font 'GoogleSans_14' from a C array */
    GoogleSans_14 = &GoogleSans_14_data;
    /* get font 'GoogleSansBold_24' from a C array */
    GoogleSansBold_24 = &GoogleSansBold_24_data;
    /* get font 'GoogleSansBold_42' from a C array */
    GoogleSansBold_42 = &GoogleSansBold_42_data;

    /*----------------
     * Images
     *----------------*/


    /*----------------
     * Subjects
     *----------------*/

    /*----------------
     * Translations
     *----------------*/


#if LV_USE_XML
    /*Register widgets*/

    /* Register fonts */
    lv_xml_register_font(NULL, "GoogleSans_10", GoogleSans_10);
    lv_xml_register_font(NULL, "GoogleSans_14", GoogleSans_14);
    lv_xml_register_font(NULL, "GoogleSansBold_24", GoogleSansBold_24);
    lv_xml_register_font(NULL, "GoogleSansBold_42", GoogleSansBold_42);

    /* Register subjects */

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if LV_USE_XML && !defined(LV_EDITOR_PREVIEW)

    /* Register images */
#endif

#if LV_USE_XML == 0
    /*--------------------
    *  Permanent screens
    *-------------------*/

    /*If XML is enabled it's assumed that the permanent screens are created
     *manaully from XML using lv_xml_create()*/

    watchscreen = watchscreen_create();
#endif
}

/* callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/