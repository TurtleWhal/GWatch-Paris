/*******************************************************************************
 * Size: 10 px
 * Bpp: 4
 * Opts: --size 10 --bpp 4 --format lvgl --font /Users/garrett/Documents/GWatch-Paris/main/fonts/files/ProductSans-Regular.ttf --output /Users/garrett/Documents/GWatch-Paris/main/fonts/generated/ProductSansRegular_10.c --no-compress --symbols 0123456789
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef PRODUCTSANSREGULAR_10
#define PRODUCTSANSREGULAR_10 1
#endif

#if PRODUCTSANSREGULAR_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x4, 0xdd, 0x80, 0x1d, 0x10, 0xa6, 0x68, 0x0,
    0x2c, 0x86, 0x0, 0xe, 0x68, 0x0, 0x2c, 0x1d,
    0x20, 0xa6, 0x4, 0xdd, 0x80,

    /* U+0031 "1" */
    0x5, 0xb6, 0xaf, 0x0, 0xe0, 0xe, 0x0, 0xe0,
    0xe, 0x0, 0xe0,

    /* U+0032 "2" */
    0x1b, 0xdb, 0x14, 0x60, 0x79, 0x0, 0x6, 0x90,
    0x2, 0xe2, 0x2, 0xd3, 0x2, 0xd4, 0x0, 0x9f,
    0xee, 0xa0,

    /* U+0033 "3" */
    0xb, 0xec, 0x13, 0x70, 0x69, 0x0, 0x7, 0x80,
    0xe, 0xf1, 0x0, 0x4, 0xc6, 0x70, 0x4c, 0x1b,
    0xec, 0x30,

    /* U+0034 "4" */
    0x0, 0xb, 0x90, 0x0, 0x5e, 0x90, 0x0, 0xd6,
    0x90, 0x8, 0x65, 0x90, 0x2c, 0x5, 0x90, 0x8e,
    0xde, 0xe6, 0x0, 0x5, 0x90,

    /* U+0035 "5" */
    0xe, 0xdd, 0x80, 0x2b, 0x0, 0x0, 0x4d, 0xcb,
    0x20, 0x16, 0x4, 0xc0, 0x11, 0x0, 0xe0, 0x6a,
    0x3, 0xd0, 0x9, 0xec, 0x30,

    /* U+0036 "6" */
    0x0, 0x3, 0x0, 0x0, 0x96, 0x0, 0x3, 0xa0,
    0x0, 0xd, 0xdc, 0x20, 0x5b, 0x3, 0xd0, 0x85,
    0x0, 0xe0, 0x5a, 0x3, 0xd0, 0x9, 0xec, 0x20,

    /* U+0037 "7" */
    0x8d, 0xdd, 0xc0, 0x0, 0x78, 0x0, 0x1d, 0x10,
    0x9, 0x60, 0x2, 0xd0, 0x0, 0xb5, 0x0, 0x1b,
    0x0, 0x0,

    /* U+0038 "8" */
    0x9, 0xdc, 0x20, 0x4b, 0x4, 0xb0, 0x3b, 0x5,
    0xa0, 0xb, 0xef, 0x30, 0x79, 0x2, 0xd0, 0x78,
    0x2, 0xe0, 0xa, 0xed, 0x40,

    /* U+0039 "9" */
    0x8, 0xdc, 0x30, 0x5a, 0x3, 0xd0, 0x85, 0x0,
    0xe0, 0x5b, 0x4, 0xd0, 0x8, 0xce, 0x50, 0x0,
    0x3b, 0x0, 0x0, 0xd2, 0x0, 0x0, 0x40, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 102, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 21, .adv_w = 61, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 32, .adv_w = 83, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 85, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 94, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 88, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 84, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 152, .adv_w = 87, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 88, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    1, 2,
    1, 3,
    1, 8,
    2, 3,
    2, 6,
    2, 7,
    3, 2,
    4, 2,
    5, 2,
    6, 2,
    6, 8,
    7, 2,
    7, 8,
    8, 1,
    8, 5,
    8, 6,
    8, 7,
    8, 9,
    10, 5,
    10, 7
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -2, -2, -2, 2, 2, 1, -2, -2,
    -2, -3, -2, -5, -2, -2, -11, -2,
    -10, -3, -5, -3
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 20,
    .glyph_ids_size = 0
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
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
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
const lv_font_t ProductSansRegular_10 = {
#else
lv_font_t ProductSansRegular_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -4,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if PRODUCTSANSREGULAR_10*/

