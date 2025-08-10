#include "lvgl.h"

LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_42);

LV_FONT_DECLARE(FontAwesome_32);
LV_FONT_DECLARE(FontAwesome_32);

#define SET_SYMBOL_32(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_32(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_32, LV_PART_MAIN); lv_label_set_text(obj, sym);

#define FA_PLAY "\xEF\x81\x8B" // U+F04B, Sizes: [40, 32]
