#include "lvgl.h"

LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansRegular_16);
LV_FONT_DECLARE(ProductSansRegular_20);
LV_FONT_DECLARE(ProductSansRegular_24);
LV_FONT_DECLARE(ProductSansBold_16);
LV_FONT_DECLARE(ProductSansBold_20);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_42);

LV_FONT_DECLARE(FontAwesome_14);
LV_FONT_DECLARE(ProductSansBold_16);
LV_FONT_DECLARE(ProductSansBold_20);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_42);
LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansRegular_16);
LV_FONT_DECLARE(ProductSansRegular_20);
LV_FONT_DECLARE(ProductSansRegular_24);

#define SET_SYMBOL_14(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_42(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_42, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_10(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_10, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_14(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_24, LV_PART_MAIN); lv_label_set_text(obj, sym);

#define FA_STEPS      "" // '', Sizes: [14]
#define FA_BATTERY    "" // '', Sizes: [14]
#define FA_CALENDAR   "" // '', Sizes: [14]
#define FA_LIGHTNING  "" // '', Sizes: [14]
#define FA_WIFI       "" // '', Sizes: [14]
#define FA_CONNECTING "" // '', Sizes: [14]
