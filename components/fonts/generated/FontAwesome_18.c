/*******************************************************************************
 * Size: 18 px
 * Bpp: 4
 * Opts: --size 18 --bpp 4 --format lvgl --output /Users/garrett/Documents/GWatch-Paris/components/fonts/generated/FontAwesome_18.c --no-compress --font /Users/garrett/Documents/GWatch-Paris/components/fonts/FontAwesome6Pro.woff --range 0xF078
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef FONTAWESOME_18
#define FONTAWESOME_18 1
#endif

#if FONTAWESOME_18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F078 "" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10,
    0xdf, 0x40, 0x0, 0x0, 0x0, 0x0, 0x7, 0xf9,
    0xdf, 0xf4, 0x0, 0x0, 0x0, 0x0, 0x7f, 0xf9,
    0x2e, 0xff, 0x40, 0x0, 0x0, 0x7, 0xff, 0xb0,
    0x2, 0xef, 0xf4, 0x0, 0x0, 0x7f, 0xfb, 0x0,
    0x0, 0x2e, 0xff, 0x40, 0x7, 0xff, 0xb0, 0x0,
    0x0, 0x2, 0xef, 0xf3, 0x7f, 0xfb, 0x0, 0x0,
    0x0, 0x0, 0x2e, 0xff, 0xff, 0xb0, 0x0, 0x0,
    0x0, 0x0, 0x2, 0xef, 0xfb, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x2c, 0xa0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 252, .box_w = 16, .box_h = 10, .ofs_x = 0, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 61560, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t FontAwesome_18 = {
#else
lv_font_t FontAwesome_18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = -1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONTAWESOME_18*/

