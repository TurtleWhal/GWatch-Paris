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
LV_FONT_DECLARE(GoogleSansCode_46);
LV_FONT_DECLARE(SirinStencil_92);
LV_FONT_DECLARE(SirinStencil_32);
LV_FONT_DECLARE(BadeenDisplay_84);

LV_FONT_DECLARE(BadeenDisplay_84);
LV_FONT_DECLARE(FontAwesome_14);
LV_FONT_DECLARE(FontAwesome_16);
LV_FONT_DECLARE(FontAwesome_22);
LV_FONT_DECLARE(FontAwesome_28);
LV_FONT_DECLARE(FontAwesome_32);
LV_FONT_DECLARE(GoogleSansCode_46);
LV_FONT_DECLARE(ProductSansBold_16);
LV_FONT_DECLARE(ProductSansBold_20);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_42);
LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansRegular_16);
LV_FONT_DECLARE(ProductSansRegular_20);
LV_FONT_DECLARE(ProductSansRegular_24);
LV_FONT_DECLARE(SirinStencil_32);
LV_FONT_DECLARE(SirinStencil_92);

#define SET_SYMBOL_y_84(obj, sym) lv_obj_set_style_text_font(obj, &BadeenDisplay_84, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_14(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_16(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_22(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_22, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_28(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_28, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_32(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_de_46(obj, sym) lv_obj_set_style_text_font(obj, &GoogleSansCode_46, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_42(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_42, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_10(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_10, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_14(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL__32(obj, sym) lv_obj_set_style_text_font(obj, &SirinStencil_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL__92(obj, sym) lv_obj_set_style_text_font(obj, &SirinStencil_92, LV_PART_MAIN); lv_label_set_text(obj, sym);

#define FA_STEPS          "" // '', Sizes: [14, 16, 28, 22]
#define FA_BATTERY        "" // '', Sizes: [14, 16, 28]
#define FA_CALENDAR       "" // '', Sizes: [14]
#define FA_LIGHTNING      "" // '', Sizes: [14, 16]
#define FA_WIFI           "" // '', Sizes: [14, 28]
#define FA_CONNECTING     "" // '', Sizes: [14]
#define FA_SETTINGS       "" // '', Sizes: [28, 22]
#define FA_FLASHLIGHT     "" // '', Sizes: [28, 22]
#define FA_BLUETOOTH      "" // '', Sizes: [28]
#define FA_POWEROFF       "" // '', Sizes: [28]
#define FA_DONOTDISTURB   "" // '', Sizes: [28]
#define FA_ROTATE         "" // '', Sizes: [28, 32]
#define FA_METRONOME      "" // '', Sizes: [22]
#define FA_STOPWATCH      "" // '', Sizes: [22]
#define FA_IMU            "" // '', Sizes: [22]
#define FA_BUG            "" // '', Sizes: [22]
#define FA_BRIGHTNESS     "" // '', Sizes: [28]
#define FA_BRIGHTNESS_LOW "" // '', Sizes: [28]
#define FA_PLAY           "" // '', Sizes: [32]
#define FA_PAUSE          "" // '', Sizes: [32]
