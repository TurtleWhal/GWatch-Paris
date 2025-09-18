#include "ui.h"

#include "assets/overlay.c"

LV_IMAGE_DECLARE(overlay);

const char *minute_ticks[] = {"00", "55", "50", "45", "40", "35", "30",
                              "25", "20", "15", "10", "05", NULL};

const char *second_ticks[] = {"00", "55", "50", "45", "40", "35", "30",
                              "25", "20", "15", "10", "05", NULL};

lv_obj_t *minutescale;
lv_obj_t *secondscale;

lv_obj_t *hour;
lv_obj_t *minute;

lv_obj_t *day;
lv_obj_t *month;

lv_obj_t *battery;

lv_obj_t *wifiicon;

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

void arc_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_target_obj(e);
    if (lv_arc_get_value(arc) >= lv_arc_get_max_value(arc))
    {
        esp_restart();
    }
}

lv_obj_t *watchscr_create()
{
    lv_color_t accent = lv_color_hex(0xffaa22);
    lv_color_t gray = lv_color_hex(0x888888);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    /* Minute scale */
    minutescale = lv_scale_create(scr);
    lv_obj_set_size(minutescale, 164, 164);
    lv_scale_set_mode(minutescale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(minutescale, LV_OPA_0, 0);
    lv_obj_set_style_radius(minutescale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(minutescale, true, 0);
    lv_obj_align(minutescale, LV_ALIGN_LEFT_MID, 30 - (164 / 2), 0);
    lv_obj_set_style_arc_width(minutescale, 0, 0);

    lv_scale_set_angle_range(minutescale, 360);
    lv_scale_set_rotation(minutescale, 0);
    lv_scale_set_range(minutescale, 0, 60);
    lv_scale_set_label_show(minutescale, true);
    lv_scale_set_total_tick_count(minutescale, 60);
    lv_scale_set_major_tick_every(minutescale, 5);
    lv_scale_set_text_src(minutescale, minute_ticks);

    lv_obj_set_style_text_font(minutescale, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(minutescale, gray, 0);

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(minutescale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(minutescale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_ITEMS);

    /* Second scale */
    secondscale = lv_scale_create(scr);
    lv_obj_set_size(secondscale, 234, 234);
    lv_scale_set_mode(secondscale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(secondscale, LV_OPA_0, 0);
    lv_obj_set_style_radius(secondscale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(secondscale, true, 0);
    lv_obj_align(secondscale, LV_ALIGN_LEFT_MID, 30 - (234 / 2), 0);
    lv_obj_set_style_arc_width(secondscale, 0, 0);

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
    lv_obj_set_style_length(secondscale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(secondscale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_ITEMS);

    /* Hour + minute text */
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

    /* Steps */
    lv_obj_t *steps = create_valuearc(scr, accent, FA_STEPS);
    lv_obj_align(steps, LV_ALIGN_RIGHT_MID, -40, -68);
    lv_arc_set_range(steps, 0, 6500);
    lv_arc_set_value(steps, 1234);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(steps, "text"), "%d", 1234);

    lv_obj_add_event_cb(steps, arc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Date */
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

    /* Battery */
    battery = create_valuearc(scr, accent, FA_BATTERY);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -40, 68);
    lv_arc_set_range(battery, 2800, 4200);
    lv_arc_set_value(battery, 3700);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"), "%d", 3700);

    wifiicon = lv_label_create(scr);
    // lv_obj_set_pos(wifiicon, POLAR(120, 90));
    lv_obj_align(wifiicon, LV_ALIGN_CENTER, POLAR(110, 90));

    lv_obj_set_style_text_color(wifiicon, lv_color_white(), 0);
    SET_SYMBOL_14(wifiicon, FA_WIFI);

    lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);

    return scr;
}

void watchscr_update()
{
    struct TimeInfo t = sysinfo.time;

    /* Rotate seconds smoothly */
    uint16_t sangle = (t.ms * 360 / 1000) / 60 + (t.sec * 6);
    lv_scale_set_rotation(secondscale, sangle);

    if (t.sec != last_sec)
    {
        last_sec = t.sec;

        uint16_t mangle = t.sec * (360 / 60) / 60 + t.min * (360 / 60);
        lv_scale_set_rotation(minutescale, mangle);

        for (int i = 0; i < 13; ++i)
        {
            minute_ticks[i] = (char *)second_ticks[i];
        }

        int idx = (0 - (mangle + (360 / 12) / 2) / (360 / 12)) % 12;
        if (idx < 0)
            idx += 12;

        minute_ticks[idx] = "";
    }

    /* Update minute/hour only when changed */
    if (t.min != last_min)
    {
        last_min = t.min;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", last_min);
        lv_label_set_text(minute, buf);
    }

    if (t.hour != last_hour)
    {
        last_hour = t.hour % 12;
        if (last_hour == 0)
            last_hour = 12;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", last_hour);
        lv_label_set_text(hour, buf);
    }

    /* Update date once a day */
    if (t.day != last_day)
    {
        last_day = t.day;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", last_day);
        lv_label_set_text(day, buf);
    }
    if (t.month != last_month)
    {
        last_month = t.month;
        lv_label_set_text(month, months[last_month - 1]);
    }

    /* Battery every 10 seconds */
    // uint32_t now_ms = esp_timer_get_time() / 1000;
    // if (now_ms - last_battery_check > 10000)
    // {
    //     last_battery_check = now_ms;
    //     last_battery_mv = battery_get_mV();
    //     lv_arc_set_value(battery, last_battery_mv);
    //     lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"),
    //                           "%ld", last_battery_mv);
    // }

    lv_arc_set_value(battery, sysinfo.bat.voltage);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"),
                          "%d", sysinfo.bat.voltage);

    if (sysinfo.wifi.connected)
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    SET_SYMBOL_14(lv_obj_get_child_by_name(battery, "icon"), sysinfo.bat.charging ? FA_LIGHTNING : FA_BATTERY);
}