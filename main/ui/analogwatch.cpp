#include "ui.hpp"
#include <sys/time.h>

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

lv_obj_t *secondhand;
lv_obj_t *minutehand;
lv_obj_t *hourhand;

static lv_point_precise_t second_hand_points[] = {
    {120, 120},
    {220, 120}};

static lv_point_precise_t minute_hand_points[] = {
    {120, 120},
    {220, 120}};

static lv_point_precise_t hour_hand_points[] = {
    {120, 120},
    {190, 120}};

lv_obj_t *analogwatch_create()
{
    lv_color_t accent = lv_color_hex(0xffaa22);
    lv_color_t gray = lv_color_hex(0x888888);

    lv_obj_t *scr = create_screen();
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    hourhand = lv_line_create(scr);
    lv_line_set_points(hourhand, hour_hand_points, 2);

    lv_obj_set_style_line_color(hourhand, lv_color_white(), 0);
    lv_obj_set_style_line_width(hourhand, 10, 0);
    lv_obj_set_style_line_rounded(hourhand, true, 0);

    secondhand = lv_line_create(scr);
    lv_line_set_points(secondhand, second_hand_points, 2);

    lv_obj_set_style_line_color(secondhand, gray, 0);
    lv_obj_set_style_line_width(secondhand, 2, 0);
    lv_obj_set_style_line_rounded(secondhand, true, 0);

    minutehand = lv_line_create(scr);
    lv_line_set_points(minutehand, minute_hand_points, 2);

    lv_obj_set_style_line_color(minutehand, accent, 0);
    lv_obj_set_style_line_width(minutehand, 6, 0);
    lv_obj_set_style_line_rounded(minutehand, true, 0);

    return scr;
}

void analogwatch_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL); // tv_sec (seconds), tv_usec (microseconds)

    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    int ms = tv.tv_usec / 1000; // milliseconds (0–999)

    float sangle = (t.tm_sec + ms / 1000.0f) * 6.0f; // 6 degrees per second

    second_hand_points[1].x = 120 + sinf(DEG2RAD * sangle) * 100;
    second_hand_points[1].y = 120 - cosf(DEG2RAD * sangle) * 100;
    lv_line_set_points(secondhand, second_hand_points, 2);

    if (t.tm_sec != last_sec)
    {
        last_sec = t.tm_sec;

        float mangle = (t.tm_min + t.tm_sec / 60) * 6.0f; // 6 degrees per second

        minute_hand_points[1].x = 120 + sinf(DEG2RAD * mangle) * 90;
        minute_hand_points[1].y = 120 - cosf(DEG2RAD * mangle) * 90;
        lv_line_set_points(minutehand, minute_hand_points, 2);
    }

    /* Update minute/hour only when changed */
    if (t.tm_min != last_min)
    {
        last_min = t.tm_min;

        float hangle = (t.tm_hour % 12 + t.tm_min / 60.0f) * 30.0f; // 30 degrees per hour

        hour_hand_points[1].x = 120 + sinf(DEG2RAD * hangle) * 70;
        hour_hand_points[1].y = 120 - cosf(DEG2RAD * hangle) * 70;
        lv_line_set_points(hourhand, hour_hand_points, 2);
    }

    if (t.tm_hour != last_hour)
    {
        last_hour = t.tm_hour % 12;
        if (last_hour == 0)
            last_hour = 12;
    }

    /* Update date once a day */
    if (t.tm_mday != last_day)
    {
        last_day = t.tm_mday;
    }

    if ((t.tm_mon + 1) != last_month)
    {
        last_month = t.tm_mon + 1;
    }
}
