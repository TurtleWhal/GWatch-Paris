#include "lvgl.h"

LV_FONT_DECLARE(ProductSansRegular_10);
LV_FONT_DECLARE(ProductSansRegular_12);
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
LV_FONT_DECLARE(ProductSansBold_76);
LV_FONT_DECLARE(ProductSansBold_92);
LV_FONT_DECLARE(GoogleSansCode_46);
LV_FONT_DECLARE(GoogleSansCode_28);
LV_FONT_DECLARE(SirinStencil_92);
LV_FONT_DECLARE(SirinStencil_32);
LV_FONT_DECLARE(BadeenDisplay_84);
LV_FONT_DECLARE(ArsenalBold_14);
LV_FONT_DECLARE(ArsenalBold_48);
LV_FONT_DECLARE(LexendExaSemiBold_14);
LV_FONT_DECLARE(LexendExaSemiBold_48);
LV_FONT_DECLARE(SquadaOneRegular_16);
LV_FONT_DECLARE(SquadaOneRegular_18);
LV_FONT_DECLARE(SquadaOneRegular_24);
LV_FONT_DECLARE(SquadaOneRegular_48);
LV_FONT_DECLARE(NotoEmojiRegular_16);
LV_FONT_DECLARE(NotoEmojiRegular_20);

LV_FONT_DECLARE(FontAwesome_14);
LV_FONT_DECLARE(FontAwesome_16);
LV_FONT_DECLARE(FontAwesome_18);
LV_FONT_DECLARE(FontAwesome_22);
LV_FONT_DECLARE(FontAwesome_24);
LV_FONT_DECLARE(FontAwesome_28);
LV_FONT_DECLARE(FontAwesome_32);
LV_FONT_DECLARE(FontAwesome_48);
LV_FONT_DECLARE(MaterialDesignIcons_16);
LV_FONT_DECLARE(MaterialDesignIcons_22);
LV_FONT_DECLARE(MaterialDesignIcons_28);

#define SET_SYMBOL_14(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_14, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_16(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_18(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_18, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_22(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_22, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_24(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_24, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_28(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_28, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_32(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_32, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_SYMBOL_48(obj, sym) lv_obj_set_style_text_font(obj, &FontAwesome_48, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_MDI_SYMBOL_16(obj, sym) lv_obj_set_style_text_font(obj, &MaterialDesignIcons_16, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_MDI_SYMBOL_22(obj, sym) lv_obj_set_style_text_font(obj, &MaterialDesignIcons_22, LV_PART_MAIN); lv_label_set_text(obj, sym);
#define SET_MDI_SYMBOL_28(obj, sym) lv_obj_set_style_text_font(obj, &MaterialDesignIcons_28, LV_PART_MAIN); lv_label_set_text(obj, sym);

#define FA_STEPS          "" // '', Sizes: [14, 16, 28, 22]
#define FA_BATTERY_FULL   "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_75     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_50     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_25     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_10     "" // '', Sizes: [14, 16, 28]
#define FA_BATTERY_EMPTY  "" // '', Sizes: [14, 16, 22, 28]
#define FA_CALENDAR       "" // '', Sizes: [14, 22, 28]
#define FA_LIGHTNING      "" // '', Sizes: [14, 16, 28]
#define FA_CHARGING       "" // '', Sizes: [14, 16, 28]
#define FA_WIFI           "" // '', Sizes: [14, 28]
#define FA_CONNECTING     "" // '', Sizes: [14]
#define FA_SETTINGS       "" // '', Sizes: [28, 22]
#define FA_FLASHLIGHT     "" // '', Sizes: [28, 22]
#define FA_BLUETOOTH      "" // '', Sizes: [14, 28]
#define FA_POWEROFF       "" // '', Sizes: [28]
#define FA_DONOTDISTURB   "" // '', Sizes: [28]
#define FA_ROTATE         "" // '', Sizes: [28, 32]
#define FA_METRONOME      "" // '', Sizes: [22, 28]
#define FA_STOPWATCH      "" // '', Sizes: [22]
#define FA_TIMER          "" // '', Sizes: [22]
#define FA_ALARM          "" // '', Sizes: [22]
#define FA_IMU            "" // '', Sizes: [22]
#define FA_BUG            "" // '', Sizes: [22]
#define FA_CALCULATOR     "" // '', Sizes: [22, 28]
#define FA_DICE           "" // '', Sizes: [22]
#define FA_BRIGHTNESS     "" // '', Sizes: [16, 28]
#define FA_BRIGHTNESS_LOW "" // '', Sizes: [28]
#define FA_DOWN           "" // '', Sizes: [18]
#define FA_PLAY           "" // '', Sizes: [32, 48]
#define FA_PAUSE          "" // '', Sizes: [32, 48]
#define FA_NEXT           "" // '', Sizes: [32]
#define FA_PREVIOUS       "" // '', Sizes: [32]
#define FA_KEYBOARD       "" // '', Sizes: [22]
#define FA_AIRPLANE       "" // '', Sizes: [28]
#define FA_PHONE_VIBRATE  "" // '', Sizes: [28, 48]
#define FA_DROPLET        "" // '', Sizes: [16]
#define FA_SMARTHOME      "" // '', Sizes: [22, 28]
#define FA_RULER          "" // '', Sizes: [22, 28]
#define FA_CHEVRON_UP     "" // '', Sizes: [22]
#define FA_CHEVRON_DOWN   "" // '', Sizes: [22]
#define FA_CHEVRON_RIGHT  "" // '', Sizes: [22]
#define FA_CHEVRON_LEFT   "" // '', Sizes: [22]
#define FA_BACKSPACE      "" // '', Sizes: [24]
#define FA_PLUSMINUS      "" // '', Sizes: [24]
#define FA_PALETTE        "" // '', Sizes: [24]
#define MDI_HOME_ASSISTANT   "\xF3\xB0\x9F\x90" // U+F07D0, Sizes: [22, 28]
#define MDI_FAN              "\xF3\xB0\x88\x90" // U+F0210, Sizes: [22]
#define MDI_WINDOW           "\xF3\xB0\x96\xB1" // U+F05B1, Sizes: [22]
#define MDI_LIGHTBULB        "\xF3\xB0\x8C\xB5" // U+F0335, Sizes: [22]
#define MDI_TRACKLIGHT       "\xF3\xB0\xA4\x94" // U+F0914, Sizes: [22]
#define MDI_THERMOMETER      "\xF3\xB0\x94\x8F" // U+F050F, Sizes: [22]
#define MDI_LIGHTNING        "\xF3\xB0\x89\x81" // U+F0241, Sizes: [22]
#define MDI_WINDOW_SHADE     "\xF3\xB1\xA9\xAB" // U+F1A6B, Sizes: [22]
#define MDI_DESKTOP_PC       "\xF3\xB0\xAA\xAB" // U+F0AAB, Sizes: [22]
#define MDI_SHOE_PRINT       "\xF3\xB0\xB7\xBA" // U+F0DFA, Sizes: [16]
#define MDI_BATTERY_HIGH     "\xF3\xB1\x8A\xA3" // U+F12A3, Sizes: [16]
#define MDI_BATTERY_MEDIUM   "\xF3\xB1\x8A\xA2" // U+F12A2, Sizes: [16]
#define MDI_BATTERY_LOW      "\xF3\xB1\x8A\xA1" // U+F12A1, Sizes: [16]
#define MDI_BATTERY_EMPTY    "\xF3\xB0\x82\x8E" // U+F008E, Sizes: [16]
#define MDI_BATTERY_CHARGING "\xF3\xB0\x82\x84" // U+F0084, Sizes: [16]
#define MDI_ALARM            "\xF3\xB0\x80\xA0" // U+F0020, Sizes: [16]
#define MDI_STOPWATCH        "\xF3\xB0\x94\x9B" // U+F051B, Sizes: [16]
#define MDI_TIMER            "\xF3\xB0\x94\x9F" // U+F051F, Sizes: [16]
