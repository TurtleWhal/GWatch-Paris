/*******************************************************************************
 * Size: 10 px
 * Bpp: 2
 * Opts: --font /fonts/ProductSans-Regular.ttf -o /fonts/GoogleSans_10_data.c --no-compress --size 10 --bpp 2 --format lvgl --symbols 0123456789
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef GOOGLESANS_10_DATA
#define GOOGLESANS_10_DATA 1
#endif

#if GOOGLESANS_10_DATA

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x1f, 0x83, 0x9, 0x60, 0x39, 0x3, 0x60, 0x33,
    0x9, 0x1f, 0x80,

    /* U+0031 "1" */
    0x19, 0xb0, 0xc3, 0xc, 0x30, 0xc0,

    /* U+0032 "2" */
    0x2e, 0x14, 0x60, 0x18, 0xc, 0xc, 0xd, 0xb,
    0xf8,

    /* U+0033 "3" */
    0x2f, 0x4, 0x60, 0x18, 0x3c, 0x1, 0xd4, 0x72,
    0xf0,

    /* U+0034 "4" */
    0x2, 0x80, 0x78, 0xd, 0x82, 0x58, 0x31, 0x8b,
    0xfd, 0x1, 0x80,

    /* U+0035 "5" */
    0x3f, 0x82, 0x0, 0x7e, 0x1, 0x1c, 0x0, 0xc6,
    0xc, 0x2f, 0x0,

    /* U+0036 "6" */
    0x0, 0x0, 0x90, 0x8, 0x3, 0xf0, 0x60, 0xc9,
    0xc, 0x60, 0xc2, 0xf0,

    /* U+0037 "7" */
    0xbf, 0xc0, 0x60, 0x30, 0x24, 0xc, 0x9, 0x2,
    0x0,

    /* U+0038 "8" */
    0x2f, 0x6, 0x18, 0x21, 0x82, 0xf0, 0x60, 0xc6,
    0xc, 0x2f, 0x40,

    /* U+0039 "9" */
    0x2f, 0x6, 0xc, 0x90, 0xc6, 0x1c, 0x2f, 0x40,
    0x20, 0xc, 0x0, 0x40
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 102, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 11, .adv_w = 61, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 17, .adv_w = 83, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 26, .adv_w = 85, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 35, .adv_w = 94, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 46, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 57, .adv_w = 88, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 69, .adv_w = 84, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 87, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 88, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1}
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
    .bpp = 2,
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
const lv_font_t GoogleSans_10_data = {
#else
lv_font_t GoogleSans_10_data = {
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



#endif /*#if GOOGLESANS_10_DATA*/

