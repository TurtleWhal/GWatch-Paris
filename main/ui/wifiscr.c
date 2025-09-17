#include "ui.h"

lv_obj_t *timelbl;

lv_obj_t *wifiscr_create()
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Connecting To Wifi");

    lv_obj_set_style_text_font(title, &ProductSansBold_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    timelbl = lv_label_create(scr);
    lv_label_set_text(timelbl, "Time");

    lv_obj_set_style_text_font(timelbl, &ProductSansRegular_16, 0);
    lv_obj_set_style_text_color(timelbl, lv_color_white(), 0);

    lv_obj_align(timelbl, LV_ALIGN_TOP_MID, 0, 56);

    return scr;
}

void wifiscr_update()
{
    local_datetime_t time = get_local_datetime();

    uint8_t h = time.hour % 12;
    if (h == 0)
        h = 12;

    // lv_label_set_text_fmt(timelbl, "Time: %02i:%02i:%02i", h, time.min, time.sec);
    // lv_label_set_text_fmt(timelbl, "%i", time.sec);

    char buf[20];
    snprintf(buf, sizeof(buf), "Time: %02d:%02d:%02d", h, time.min, time.sec);
    lv_label_set_text(timelbl, buf);
}