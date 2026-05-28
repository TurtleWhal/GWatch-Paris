#include "ui.hpp"
#include "images.hpp"

lv_obj_t *location;
lv_obj_t *description;

lv_obj_t *icon;
lv_obj_t *temp;

lv_obj_t *hi;
lv_obj_t *lo;
lv_obj_t *hum;
lv_obj_t *humval;
lv_obj_t *uv;
lv_obj_t *uvval;


// {"t":"weather","v":1,"temp":290,"hi":297,"lo":285,"hum":73,"rain":4,"uv":1,"code":800,"txt":"Clear Sky","wind":4.67,"wdir":321,"loc":"My Location"})

lv_obj_t *weather_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    location = lv_label_create(scr);
    lv_label_set_text(location, "Lake Forest Park");

    lv_obj_align(location, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_font(location, &ProductSansRegular_20, 0);
    lv_obj_set_width(location, 160);
    lv_label_set_long_mode(location, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(location, LV_TEXT_ALIGN_CENTER, 0);

    description = lv_label_create(scr);
    lv_label_set_text(description, "Clear Sky");

    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_font(description, &ProductSansRegular_24, 0);
    lv_obj_set_width(description, 220);
    lv_label_set_long_mode(description, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);

    temp = lv_label_create(scr);
    lv_label_set_text(temp, "Temperature");

    lv_obj_align(temp, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_text_font(temp, &ProductSansBold_96, 0);
    lv_label_set_text(temp, "78°");
    // lv_obj_set_style_text_letter_space(temp, -6, 0); // only if temp >= 100

    icon = lv_image_create(scr);
    lv_obj_set_size(icon, 64, 64);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0); // 0 x offset if temp >= 100
    lv_image_set_src(icon, &IMG_ISOLATED_SCATTERED_TSTORMS_DAY);
    lv_image_set_scale(icon, 256 * 64 / 192);

    hum = lv_arc_create(scr);
    lv_obj_set_size(hum, 232, 232);
    lv_obj_align(hum, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(hum, 45 - 20, 45 + 20);
    lv_arc_set_mode(hum, LV_ARC_MODE_REVERSE);
    lv_obj_set_style_bg_opa(hum, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_width(hum, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hum, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hum, lv_color_hex(0x002244), LV_PART_MAIN);
    lv_obj_set_style_arc_color(hum, lv_color_hex(0x0088FF), LV_PART_INDICATOR);
    lv_obj_set_flag(hum, LV_OBJ_FLAG_CLICKABLE, false);

    lv_arc_set_range(hum, 0, 100);
    lv_arc_set_value(hum, 73);

    lv_obj_t *humlbl = lv_label_create(scr);
    lv_obj_align(humlbl, LV_ALIGN_BOTTOM_RIGHT, -34, -60);
    lv_obj_set_style_text_font(humlbl, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_align(humlbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(humlbl, "Humidity");

    humval = lv_label_create(scr);
    lv_obj_align(humval, LV_ALIGN_BOTTOM_RIGHT, -50, -34);
    lv_obj_set_style_text_font(humval, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_align(humval, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(humval, "73%");

    uv = lv_arc_create(scr);
    lv_obj_set_size(uv, 232, 232);
    lv_obj_align(uv, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(uv, 135 - 20, 135 + 20);
    lv_arc_set_mode(uv, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_bg_opa(uv, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_width(uv, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(uv, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(uv, lv_color_hex(0x440022), LV_PART_MAIN);
    lv_obj_set_style_arc_color(uv, lv_color_hex(0xFF0088), LV_PART_INDICATOR);
    lv_obj_set_flag(uv, LV_OBJ_FLAG_CLICKABLE, false);
    
    lv_arc_set_range(uv, 0, 10);
    lv_arc_set_value(uv, 1);

    lv_obj_t *uvlbl = lv_label_create(scr);
    lv_obj_align(uvlbl, LV_ALIGN_BOTTOM_LEFT, 34, -60);
    lv_obj_set_style_text_font(uvlbl, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_align(uvlbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(uvlbl, "UV Index");

    uvval = lv_label_create(scr);
    lv_obj_align(uvval, LV_ALIGN_BOTTOM_LEFT, 50, -34);
    lv_obj_set_style_text_font(uvval, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_align(uvval, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(uvval, "1");

    return scr;
}
