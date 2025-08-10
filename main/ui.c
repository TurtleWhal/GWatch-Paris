#include "ui.h"
#include "lvgl.h"
#include "fonts.h"
#include "esp_timer.h"

const char *second_ticks[] = {"00", "55", "50", "45", "40", "35", "30", "25", "20", "15", "10", "05", NULL};

lv_obj_t *minutescale;
lv_obj_t *secondscale;

lv_obj_t *hour;
lv_obj_t *minute;

void ui_create(void)
{
    lv_color_t accent = lv_color_hex(0xffaa00);
    lv_color_t gray = lv_color_hex(0x888888);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    minutescale = lv_scale_create(scr);

    lv_obj_set_size(minutescale, 224, 224);
    lv_scale_set_mode(minutescale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(minutescale, LV_OPA_0, 0);
    lv_obj_set_style_radius(minutescale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(minutescale, true, 0);
    lv_obj_center(minutescale);
    lv_obj_set_x(minutescale, -120);
    lv_obj_set_style_arc_width(minutescale, 0, 0); // Set the arc width

    lv_scale_set_angle_range(minutescale, 360);
    lv_scale_set_rotation(minutescale, 0);
    lv_scale_set_range(minutescale, 0, 60);

    lv_scale_set_label_show(minutescale, true);

    lv_scale_set_total_tick_count(minutescale, 60);
    lv_scale_set_major_tick_every(minutescale, 5);

    lv_scale_set_text_src(minutescale, second_ticks);

    lv_obj_set_style_text_font(minutescale, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(minutescale, gray, 0);

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(minutescale, 8, LV_PART_INDICATOR);     /* tick length */
    lv_obj_set_style_line_width(minutescale, 3, LV_PART_INDICATOR); /* tick width */

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(minutescale, 4, LV_PART_ITEMS);     /* tick length */
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_ITEMS); /* tick width */

    secondscale = lv_scale_create(scr);

    lv_obj_set_size(secondscale, 296, 296);
    lv_scale_set_mode(secondscale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(secondscale, LV_OPA_0, 0);
    lv_obj_set_style_radius(secondscale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(secondscale, true, 0);
    lv_obj_center(secondscale);
    lv_obj_set_x(secondscale, -120);
    lv_obj_set_style_arc_width(secondscale, 0, 0); // Set the arc width

    lv_scale_set_angle_range(secondscale, 360);
    lv_scale_set_rotation(secondscale, 0);
    lv_scale_set_range(secondscale, 0, 60);

    lv_scale_set_label_show(secondscale, true);

    lv_scale_set_total_tick_count(secondscale, 60);
    lv_scale_set_major_tick_every(secondscale, 5);

    lv_scale_set_text_src(secondscale, second_ticks);

    lv_obj_set_style_text_font(secondscale, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(secondscale, accent, 0);

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(secondscale, 8, LV_PART_INDICATOR);     /* tick length */
    lv_obj_set_style_line_width(secondscale, 3, LV_PART_INDICATOR); /* tick width */

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(secondscale, 4, LV_PART_ITEMS);     /* tick length */
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_ITEMS); /* tick width */

    hour = lv_label_create(scr);
    lv_label_set_text(hour, "09");
    lv_obj_set_style_text_color(hour, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(hour, &ProductSansBold_42, 0);
    lv_obj_align(hour, LV_ALIGN_LEFT_MID, 4, 0);

    minute = lv_label_create(scr);
    lv_label_set_text(minute, "17");
    lv_obj_set_style_text_color(minute, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(minute, &ProductSansBold_24, 0);
    lv_obj_align(minute, LV_ALIGN_LEFT_MID, 66, 0);

    lv_obj_t *bound = lv_obj_create(scr);
    lv_obj_set_size(bound, 96, 36);
    lv_obj_set_style_bg_opa(bound, LV_OPA_0, 0);
    lv_obj_set_style_radius(bound, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(bound, accent, 0);
    lv_obj_set_style_border_width(bound, 2, 0);
    lv_obj_align(bound, LV_ALIGN_LEFT_MID, 60, 0);

    lv_screen_load(scr);
}

void ui_update(void)
{
    uint32_t millis = esp_timer_get_time() / 1000;

    uint8_t h = ((millis / 3600000) + 1) % 12;
    uint8_t m = (millis / 60000) % 60;

    h = 9;

    uint16_t mangle = esp_timer_get_time() * (360 / 60) / 60000000;
    uint16_t sangle = esp_timer_get_time() * (360 / 60) / 1000000;

    // Make a writable copy of second_ticks
    static char *minticks[13];
    for (int i = 0; i < 13; ++i)
    {
        minticks[i] = (char *)second_ticks[i];
    }

    // Set the desired tick label to an empty string
    int idx = (0 - (mangle + (360 / 12) / 2) / (360 / 12)) % 12;
    if (idx < 0)
        idx += 12;

    minticks[idx] = "";

    lv_scale_set_text_src(minutescale, (const char **)minticks);

    lv_scale_set_rotation(minutescale, mangle);
    lv_scale_set_rotation(secondscale, sangle);

    lv_label_set_text_fmt(hour, "%02d", h);
    lv_label_set_text_fmt(minute, "%02d", m);
}