#include "lvgl.h"

LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansRegular_16);
LV_FONT_DECLARE(ProductSansRegular_20);
LV_FONT_DECLARE(ProductSansRegular_24);
LV_FONT_DECLARE(ProductSansBold_16);
LV_FONT_DECLARE(ProductSansBold_20);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_30);
LV_FONT_DECLARE(ProductSansBold_36);
LV_FONT_DECLARE(ProductSansBold_42);
LV_FONT_DECLARE(ProductSansBold_96);
LV_FONT_DECLARE(GoogleSansCode_46);
LV_FONT_DECLARE(GoogleSansCode_28);
LV_FONT_DECLARE(SirinStencil_92);
LV_FONT_DECLARE(SirinStencil_32);
LV_FONT_DECLARE(BadeenDisplay_84);
LV_FONT_DECLARE(ArsenalBold_14);
LV_FONT_DECLARE(ArsenalBold_48);
LV_FONT_DECLARE(LexendExaSemiBold_14);
LV_FONT_DECLARE(LexendExaSemiBold_48);
LV_FONT_DECLARE(NotoEmojiRegular_16);
LV_FONT_DECLARE(NotoEmojiRegular_20);

LV_FONT_DECLARE(ArsenalBold_14);
LV_FONT_DECLARE(ArsenalBold_48);
LV_FONT_DECLARE(BadeenDisplay_84);
LV_FONT_DECLARE(FontAwesome_14);
LV_FONT_DECLARE(FontAwesome_16);
LV_FONT_DECLARE(FontAwesome_18);
LV_FONT_DECLARE(FontAwesome_22);
LV_FONT_DECLARE(FontAwesome_28);
LV_FONT_DECLARE(FontAwesome_32);
LV_FONT_DECLARE(FontAwesome_48);
LV_FONT_DECLARE(GoogleSansCode_28);
LV_FONT_DECLARE(GoogleSansCode_46);
LV_FONT_DECLARE(LexendExaSemiBold_14);
LV_FONT_DECLARE(LexendExaSemiBold_48);
LV_FONT_DECLARE(NotoEmojiRegular_16);
LV_FONT_DECLARE(NotoEmojiRegular_20);
LV_FONT_DECLARE(ProductSansBold_16);
LV_FONT_DECLARE(ProductSansBold_20);
LV_FONT_DECLARE(ProductSansBold_24);
LV_FONT_DECLARE(ProductSansBold_30);
LV_FONT_DECLARE(ProductSansBold_36);
LV_FONT_DECLARE(ProductSansBold_42);
LV_FONT_DECLARE(ProductSansBold_96);
LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_14);
LV_FONT_DECLARE(ProductSansRegular_16);
LV_FONT_DECLARE(ProductSansRegular_20);
LV_FONT_DECLARE(ProductSansRegular_24);
LV_FONT_DECLARE(SirinStencil_32);
LV_FONT_DECLARE(SirinStencil_92);

#define SET_SYMBOL_14(obj, sym) lv_obj_set_style_text_font(obj, &ArsenalBold_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_48(obj, sym) lv_obj_set_style_text_font(obj, &ArsenalBold_48, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_y_84(obj, sym) lv_obj_set_style_text_font(obj, &BadeenDisplay_84, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_14(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_16(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_18(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_18, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_22(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_22, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_28(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_28, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_32(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_48(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_48, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_de_28(obj, sym) lv_obj_set_style_text_font(obj, &GoogleSansCode_28, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_de_46(obj, sym) lv_obj_set_style_text_font(obj, &GoogleSansCode_46, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_iBold_14(obj, sym) lv_obj_set_style_text_font(obj, &LexendExaSemiBold_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_iBold_48(obj, sym) lv_obj_set_style_text_font(obj, &LexendExaSemiBold_48, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_ular_16(obj, sym) lv_obj_set_style_text_font(obj, &NotoEmojiRegular_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_ular_20(obj, sym) lv_obj_set_style_text_font(obj, &NotoEmojiRegular_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_30(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_30, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_36(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_36, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_42(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_42, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_old_96(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansBold_96, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_10(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_10, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_14(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_16(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_20(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_20, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_egular_24(obj, sym) lv_obj_set_style_text_font(obj, &ProductSansRegular_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL__32(obj, sym) lv_obj_set_style_text_font(obj, &SirinStencil_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL__92(obj, sym) lv_obj_set_style_text_font(obj, &SirinStencil_92, LV_PART_MAIN); lv_label_set_text(obj, sym);

#define FA_STEPS          "" // '', Sizes: [14, 16, 28, 22]
#define FA_BATTERY_FULL   "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_75     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_50     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_25     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_10     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_EMPTY  "" // '', Sizes: [14, 16, 28]
#define FA_CALENDAR       "" // '', Sizes: [14, 22]
#define FA_LIGHTNING      "" // '', Sizes: [14, 16]
#define FA_CHARGING       "" // '', Sizes: [14, 16]
#define FA_WIFI           "" // '', Sizes: [14, 28]
#define FA_CONNECTING     "" // '', Sizes: [14]
#define FA_SETTINGS       "" // '', Sizes: [28, 22]
#define FA_FLASHLIGHT     "" // '', Sizes: [28, 22]
#define FA_BLUETOOTH      "" // '', Sizes: [14, 28]
#define FA_POWEROFF       "" // '', Sizes: [28]
#define FA_DONOTDISTURB   "" // '', Sizes: [28]
#define FA_ROTATE         "" // '', Sizes: [28, 32]
#define FA_METRONOME      "" // '', Sizes: [22]
#define FA_STOPWATCH      "" // '', Sizes: [22]
#define FA_TIMER          "" // '', Sizes: [22]
#define FA_ALARM          "" // '', Sizes: [22]
#define FA_IMU            "" // '', Sizes: [22]
#define FA_BUG            "" // '', Sizes: [22]
#define FA_CALCULATOR     "" // '', Sizes: [22]
#define FA_DICE           "" // '', Sizes: [22]
#define FA_BRIGHTNESS     "" // '', Sizes: [28]
#define FA_BRIGHTNESS_LOW "" // '', Sizes: [28]
#define FA_DOWN           "" // '', Sizes: [18]
#define FA_PLAY           "" // '', Sizes: [32, 48]
#define FA_PAUSE          "" // '', Sizes: [32, 48]
#define FA_NEXT           "" // '', Sizes: [32]
#define FA_PREVIOUS       "" // '', Sizes: [32]
#define FA_KEYBOARD       "" // '', Sizes: [22]
