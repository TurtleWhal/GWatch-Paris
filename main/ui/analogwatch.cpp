#include "ui.hpp"
#include <sys/time.h>
#include <math.h>
#include <stdio.h>

#define DEG2RAD (M_PI / 180.0f)

const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char *wdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

lv_obj_t *secondhand;
lv_obj_t *minutehand;
lv_obj_t *hourhand;
lv_obj_t *time_label;

lv_obj_t *datelabel;

lv_obj_t *baticon;
lv_obj_t *battery;
lv_obj_t *steps;

lv_obj_t *wifiicon;

static lv_point_precise_t second_hand_points[] = {
    {120, 120},
    {220, 120}};

static lv_point_precise_t minute_hand_points[] = {
    {120, 120},
    {220, 120}};

// Outline points for hollow hour hand (forms a thick line outline with rounded ends)
#define HOUR_HAND_SEGMENTS 7 // Number of segments for each rounded end
static lv_point_precise_t hour_hand_outline[HOUR_HAND_SEGMENTS * 2 + 3];

lv_obj_t *analogwatch_create(lv_obj_t *parent)
{
    lv_color_t accent = lv_theme_get_color_primary(parent);
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    datelabel = lv_label_create(scr);
    lv_obj_set_style_text_font(datelabel, &ProductSansBold_20, 0);
    lv_obj_align(datelabel, LV_ALIGN_CENTER, 0, -48);
    lv_obj_set_style_text_color(datelabel, accent, 0);

    // Center digital time label
    time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(time_label, &BadeenDisplay_84, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x444444), 0);
    lv_obj_center(time_label);

    lv_obj_t *infobox = lv_obj_create(scr);

    lv_obj_set_style_bg_opa(infobox, 0, 0);
    lv_obj_set_size(infobox, 240, 96);
    lv_obj_align(infobox, LV_ALIGN_CENTER, 0, 48);
    lv_obj_set_scroll_dir(infobox, LV_DIR_NONE);
    lv_obj_set_style_border_width(infobox, 0, 0);
    lv_obj_set_style_pad_all(infobox, 0, 0);

    lv_obj_set_flex_flow(infobox, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_track_place(infobox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(infobox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(infobox, LV_FLEX_ALIGN_CENTER, 0);

    wifiicon = lv_label_create(infobox);
    SET_SYMBOL_14(wifiicon, FA_WIFI);

    lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    baticon = lv_label_create(infobox);
    SET_SYMBOL_16(baticon, FA_BATTERY);

    battery = lv_label_create(infobox);
    lv_obj_set_style_text_font(battery, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(battery, "%1.3fV", 1.234);

    lv_obj_t *stepicon = lv_label_create(infobox);
    SET_SYMBOL_16(stepicon, FA_STEPS);

    steps = lv_label_create(infobox);
    lv_obj_set_style_text_font(steps, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(steps, "%d", 5678);

    // lv_obj_t *scale = lv_scale_create(scr);
    // lv_obj_set_size(scale, 240, 240);
    // lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    // lv_obj_set_style_bg_opa(scale, LV_OPA_0, 0);
    // lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
    // lv_obj_set_style_clip_corner(scale, true, 0);
    // lv_obj_align(scale, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_arc_width(scale, 0, 0);

    // lv_scale_set_angle_range(scale, 360);
    // lv_scale_set_rotation(scale, 0);
    // lv_scale_set_range(scale, 0, 60);
    // lv_scale_set_label_show(scale, false);
    // lv_scale_set_total_tick_count(scale, 60);
    // lv_scale_set_major_tick_every(scale, 5);
    // // lv_scale_set_text_src(scale, minute_ticks);

    // lv_obj_set_style_text_font(scale, &ProductSansRegular_14, 0);
    // lv_obj_set_style_text_color(scale, gray, 0);

    // lv_obj_set_style_line_color(scale, gray, LV_PART_INDICATOR);
    // lv_obj_set_style_length(scale, 8, LV_PART_INDICATOR);
    // lv_obj_set_style_line_width(scale, 2, LV_PART_INDICATOR);

    // lv_obj_set_style_line_color(scale, gray, LV_PART_ITEMS);
    // lv_obj_set_style_length(scale, 4, LV_PART_ITEMS);
    // lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);

    // Hour hand as hollow outline
    hourhand = lv_line_create(scr);
    lv_line_set_points(hourhand, hour_hand_outline, HOUR_HAND_SEGMENTS * 2 + 3);
    lv_obj_set_size(hourhand, 240, 240);
    lv_obj_set_style_line_color(hourhand, accent, 0);
    lv_obj_set_style_line_width(hourhand, 2, 0);
    lv_obj_set_style_line_rounded(hourhand, true, 0);

    // Minute hand
    minutehand = lv_line_create(scr);
    lv_line_set_points(minutehand, minute_hand_points, 2);
    lv_obj_set_size(minutehand, 240, 240);
    lv_obj_set_style_line_color(minutehand, lv_color_white(), 0);
    lv_obj_set_style_line_width(minutehand, 5, 0);
    lv_obj_set_style_line_rounded(minutehand, true, 0);

    // Second hand
    secondhand = lv_line_create(scr);
    lv_line_set_points(secondhand, second_hand_points, 2);
    lv_obj_set_size(secondhand, 240, 240);
    lv_obj_set_style_line_color(secondhand, gray, 0);
    lv_obj_set_style_line_width(secondhand, 2, 0);
    lv_obj_set_style_line_rounded(secondhand, true, 0);

    // Center cap
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_set_size(c, 5, 5);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, gray, 0);
    lv_obj_center(c);

    return scr;
}

void analogwatch_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm t;
    localtime_r(&tv.tv_sec, &t);
    int ms = tv.tv_usec / 1000;

    // Calculate angles (0° = 12 o'clock, increasing clockwise)
    float sangle = (t.tm_sec + ms / 1000.0f) * 6.0f;
    float mangle = (t.tm_min + t.tm_sec / 60.0f) * 6.0f;
    float hangle = ((t.tm_hour % 12) + t.tm_min / 60.0f) * 30.0f;

    // Convert angles so 0° is at 12 o'clock (subtract 90°)
    float srad = (sangle - 90.0f) * DEG2RAD;
    float mrad = (mangle - 90.0f) * DEG2RAD;
    float hrad = (hangle - 90.0f) * DEG2RAD;

    // Update second hand continuously
    second_hand_points[1].x = 120.5f + cosf(srad) * 100.0f;
    second_hand_points[1].y = 120.5f + sinf(srad) * 100.0f;
    lv_line_set_points(secondhand, second_hand_points, 2);

    // Update minute and hour when needed
    if (t.tm_sec != last_sec)
    {
        last_sec = t.tm_sec;

        minute_hand_points[1].x = 120.5f + cosf(mrad) * 95.0;
        minute_hand_points[1].y = 120.5f + sinf(mrad) * 95.0;
        lv_line_set_points(minutehand, minute_hand_points, 2);

        // Calculate hour hand outline (thick line outline with rounded ends)
        float thickness = 9.0f; // Half of 18px width
        float length = 65.0f;

        // Perpendicular angle for thickness
        float perp_angle = hrad + M_PI / 2.0f;
        float perp_x = cosf(perp_angle) * thickness;
        float perp_y = sinf(perp_angle) * thickness;

        // End point of the line
        float end_x = 120.5f + cosf(hrad) * length;
        float end_y = 120.5f + sinf(hrad) * length;

        int idx = 0;

        // Start with bottom edge going toward the end
        hour_hand_outline[idx].x = 120.5f - perp_x;
        hour_hand_outline[idx].y = 120.5f - perp_y;
        idx++;

        hour_hand_outline[idx].x = end_x - perp_x;
        hour_hand_outline[idx].y = end_y - perp_y;
        idx++;

        // Rounded end cap at the tip (semicircle from bottom to top around the end)
        for (int i = 1; i < HOUR_HAND_SEGMENTS; i++)
        {
            float cap_angle = perp_angle + M_PI + (M_PI * i / HOUR_HAND_SEGMENTS);
            hour_hand_outline[idx].x = end_x + cosf(cap_angle) * thickness;
            hour_hand_outline[idx].y = end_y + sinf(cap_angle) * thickness;
            idx++;
        }

        // Top edge going back to start
        hour_hand_outline[idx].x = end_x + perp_x;
        hour_hand_outline[idx].y = end_y + perp_y;
        idx++;

        hour_hand_outline[idx].x = 120.5f + perp_x;
        hour_hand_outline[idx].y = 120.5f + perp_y;
        idx++;

        // Rounded end cap at the center (semicircle from top to bottom around the center)
        for (int i = 1; i < HOUR_HAND_SEGMENTS; i++)
        {
            float cap_angle = perp_angle + (M_PI * i / HOUR_HAND_SEGMENTS);
            hour_hand_outline[idx].x = 120.5f + cosf(cap_angle) * thickness;
            hour_hand_outline[idx].y = 120.5f + sinf(cap_angle) * thickness;
            idx++;
        }

        // Close the loop by returning to the starting point
        hour_hand_outline[idx].x = 120.5f - perp_x;
        hour_hand_outline[idx].y = 120.5f - perp_y;
        idx++;

        lv_line_set_points(hourhand, hour_hand_outline, idx);

        // Update label (digital time)
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour, t.tm_min);
        lv_label_set_text(time_label, buf);
    }

    // Update once per day
    if (t.tm_mday != last_day)
    {
        last_day = t.tm_mday;

        lv_label_set_text_fmt(datelabel, "%s %s %02d", wdays[t.tm_wday], months[t.tm_mon], t.tm_mday);
    }

    if ((t.tm_mon + 1) != last_month)
        last_month = t.tm_mon + 1;

    lv_label_set_text_fmt(battery, "%1.3fV", watch.battery.voltage / 1000.0);

    lv_label_set_text_fmt(steps, "%ld", watch.imu.steps);

    switch (watch.wifi.status)
    {
    case WIFI_CONNECTED:
        SET_SYMBOL_14(wifiicon, FA_WIFI);
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECTING:
        SET_SYMBOL_14(wifiicon, FA_CONNECTING);
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_DISCONNECTED:
        lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    }

    SET_SYMBOL_16(baticon,
                  watch.battery.charging ? FA_LIGHTNING : FA_BATTERY);
}