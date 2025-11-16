#include "ui.hpp"

lv_obj_t *number;

#define MIN_SHAKE 200
void dice_update(lv_timer_t *t)
{
    if (lv_screen_active() == (lv_obj_t *)lv_timer_get_user_data(t))
    {
        GyroData g = gyro_read();

        if (abs(g.x) > MIN_SHAKE || abs(g.y) > MIN_SHAKE || abs(g.z) > MIN_SHAKE)
        {

            int min_val = 1;
            int max_val = 6;
            int random_num = min_val + (rand() % (max_val - min_val + 1));

            // printf("%i\n", random_num);

            lv_label_set_text_fmt(number, "%i", random_num);
        }
    }
}

void dice_update(lv_event_t *e)
{
    int min_val = 1;
    int max_val = 6;
    int random_num = min_val + (rand() % (max_val - min_val + 1));

    lv_label_set_text_fmt(number, "%i", random_num);
}

lv_obj_t *dice_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_t *dice = lv_obj_create(scr);
    lv_obj_set_style_bg_color(dice, lv_color_white(), 0);
    lv_obj_set_style_border_width(dice, 0, 0);

    lv_obj_set_size(dice, 100, 100);

    number = lv_label_create(dice);
    lv_obj_set_style_text_font(number, &GoogleSansCode_46, 0);
    lv_obj_set_style_text_color(number, lv_color_black(), 0);
    lv_label_set_text(number, "0");

    lv_obj_center(number);

    lv_obj_center(dice);

    lv_timer_create(dice_update, 50, scr);

    // lv_obj_add_event_cb(dice, dice_update, LV_EVENT_PRESSING, NULL);

    return scr;
}