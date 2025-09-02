#include "ui.h"
#include "lvgl.h"
#include "fonts.h"
#include "esp_timer.h"
#include "math.h"
#include "wifitime.h"
#include "battery.h"

#include "overlay.c"

LV_IMAGE_DECLARE(overlay);

#define DEG2RAD 0.017453292519943295

// Returns x, y for a given Radius and Theta
#define POLAR(r, t) cos(DEG2RAD *t) * r, sin(DEG2RAD *t) * r

lv_obj_t *create_valuearc(lv_obj_t *parent, lv_color_t color, char *symbol);

const char *second_ticks[] = {"00", "55", "50", "45", "40", "35", "30", "25", "20", "15", "10", "05", NULL};

lv_obj_t *minutescale;
lv_obj_t *secondscale;

lv_obj_t *hour;
lv_obj_t *minute;

lv_obj_t *day;
lv_obj_t *month;

lv_obj_t *battery;

void ui_create(void)
{
    lv_color_t accent = lv_color_hex(0xffaa22);
    lv_color_t gray = lv_color_hex(0x888888);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    minutescale = lv_scale_create(scr);

    lv_obj_set_size(minutescale, 164, 164);
    lv_scale_set_mode(minutescale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(minutescale, LV_OPA_0, 0);
    lv_obj_set_style_radius(minutescale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(minutescale, true, 0);
    lv_obj_align(minutescale, LV_ALIGN_LEFT_MID, 30 - (164 / 2), 0);
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
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_INDICATOR); /* tick width */

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(minutescale, 4, LV_PART_ITEMS);     /* tick length */
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_ITEMS); /* tick width */

    secondscale = lv_scale_create(scr);

    lv_obj_set_size(secondscale, 234, 234);
    lv_scale_set_mode(secondscale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(secondscale, LV_OPA_0, 0);
    lv_obj_set_style_radius(secondscale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(secondscale, true, 0);
    lv_obj_align(secondscale, LV_ALIGN_LEFT_MID, 30 - (234 / 2), 0);
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
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_INDICATOR); /* tick width */

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(secondscale, 4, LV_PART_ITEMS);     /* tick length */
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_ITEMS); /* tick width */

    // lv_obj_t *edge = lv_image_create(scr);
    // lv_image_set_src(edge, &overlay);
    // lv_obj_center(edge);

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
    lv_obj_set_size(bound, 92, 36);
    lv_obj_set_style_bg_opa(bound, LV_OPA_0, 0);
    lv_obj_set_style_radius(bound, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(bound, accent, 0);
    lv_obj_set_style_border_width(bound, 2, 0);
    lv_obj_align(bound, LV_ALIGN_LEFT_MID, 60, 0);

    lv_obj_t *steps = create_valuearc(scr, accent, FA_STEPS);
    lv_obj_align(steps, LV_ALIGN_RIGHT_MID, -40, -68);

    lv_arc_set_range(steps, 0, 6500);
    lv_arc_set_value(steps, 1234);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(steps, "text"), "%d", 1234);

    // lv_obj_t *date = create_valuearc(scr, accent, FA_CALENDAR);
    // lv_obj_align(date, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t *dateicon = lv_label_create(scr);
    SET_SYMBOL_14(dateicon, FA_CALENDAR);
    lv_obj_align(dateicon, LV_ALIGN_CENTER, 80, -22);
    lv_obj_set_style_text_color(dateicon, accent, 0);

    month = lv_label_create(scr);
    lv_obj_align(month, LV_ALIGN_CENTER, 80, 0);
    lv_obj_set_style_text_font(month, &ProductSansBold_20, 0);
    lv_obj_set_style_text_color(month, lv_color_white(), 0);
    lv_label_set_text(month, "AUG");

    day = lv_label_create(scr);
    lv_obj_align(day, LV_ALIGN_CENTER, 80, 22);
    lv_obj_set_style_text_font(day, &ProductSansBold_16, 0);
    lv_obj_set_style_text_color(day, lv_color_white(), 0);
    lv_label_set_text(day, "11");

    battery = create_valuearc(scr, accent, FA_BATTERY);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -40, 68);

    lv_arc_set_range(battery, 2800, 4200);
    lv_arc_set_value(battery, 68);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"), "%d%%", 68);

    /** Color Test **/
    // lv_obj_t *red = lv_button_create(scr);
    // lv_obj_align(red, LV_ALIGN_CENTER, 0, -60);
    // lv_obj_set_size(red, 120, 60);
    // lv_obj_set_style_bg_color(red, lv_color_hex(0xFF0000), 0);

    // lv_obj_t *green = lv_button_create(scr);
    // lv_obj_align(green, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_size(green, 120, 60);
    // lv_obj_set_style_bg_color(green, lv_color_hex(0x00FF00), 0);

    // lv_obj_t *blue = lv_button_create(scr);
    // lv_obj_align(blue, LV_ALIGN_CENTER, 0, 60);
    // lv_obj_set_size(blue, 120, 60);
    // lv_obj_set_style_bg_color(blue, lv_color_hex(0x0000FF), 0);

    lv_screen_load(scr);
}

void ui_update(void)
{
    local_datetime_t time = get_local_datetime();

    // uint32_t millis = esp_timer_get_time() / 1000;

    // uint8_t h = ((millis / 3600000) + 1) % 12;
    // uint8_t m = (millis / 60000) % 60;

    // h += 9;

    uint8_t h = time.hour;
    uint8_t m = time.min;

    h = h % 12;
    if (h == 0)
        h = 12;

    // Calculate angles for minute and second hands
    // uint16_t mangle = esp_timer_get_time() * (360 / 60) / 60000000;
    // uint16_t sangle = esp_timer_get_time() * (360 / 60) / 1000000;
    uint16_t mangle = time.sec * (360 / 60) / 60 + m * (360 / 60);
    uint16_t sangle = time.ms * (360 / 60) / 1000 + time.sec * (360 / 60);

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

    lv_scale_set_rotation(secondscale, sangle);
    lv_scale_set_rotation(minutescale, mangle);

    lv_label_set_text_fmt(hour, "%02d", h);
    lv_label_set_text_fmt(minute, "%02d", m);

    lv_label_set_text_fmt(day, "%02d", time.day);
    lv_label_set_text(month, months[time.month - 1]);

    uint32_t battery_level = battery_get_mV();
    lv_arc_set_value(battery, battery_level);

    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"), "%ld", battery_level);
}

lv_obj_t *create_valuearc(lv_obj_t *parent, lv_color_t color, char *symbol)
{
    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_set_size(arc, 60, 60);

    // Background Arc
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), 0);
    lv_obj_set_style_arc_width(arc, 8, 0);
    // lv_arc_set_angles(arc, min, max);

    // Indicator Arc
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);

    // Knob
    lv_obj_set_style_opa(arc, LV_OPA_0, LV_PART_KNOB);

    lv_obj_t *icon = lv_label_create(arc);
    SET_SYMBOL_14(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);

    lv_obj_t *value = lv_label_create(arc);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);

    lv_obj_set_style_text_font(value, &ProductSansRegular_14, 0);
    // lv_label_set_text_fmt(value, "%d", 27619);

    lv_obj_set_name(value, "text");

    return arc;
}