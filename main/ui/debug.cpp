#include "ui.hpp"

lv_obj_t *create_setting(lv_obj_t *parent, const char *name, bool state, lv_event_cb_t event_cb)
{
    lv_obj_t *setting = lv_button_create(parent);
    lv_obj_set_size(setting, 180, 44);
    lv_obj_set_style_bg_color(setting, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(setting, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *label = lv_label_create(setting);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ProductSansRegular_16, 0);

    lv_obj_t *sw = lv_switch_create(setting);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);

    if (event_cb != nullptr)
    {
        lv_obj_add_event_cb(sw, event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(setting, [](lv_event_t *e)
                            { 
                                lv_obj_set_state((lv_obj_t *)lv_event_get_user_data(e), LV_STATE_CHECKED, !(lv_obj_get_state((lv_obj_t *)lv_event_get_user_data(e)) & LV_STATE_CHECKED));
                                lv_obj_send_event((lv_obj_t *)lv_event_get_user_data(e), LV_EVENT_CLICKED, NULL); }, LV_EVENT_CLICKED, sw);

        lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    else
    {
        lv_obj_set_style_opa(setting, LV_OPA_50, 0);
        lv_obj_set_flag(setting, LV_OBJ_FLAG_CLICKABLE, false);
    }

    return setting;
}

lv_obj_t *debugscreen_create()
{
    lv_obj_t *scr = create_screen(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_pad_bottom(scr, 40, 0);

    lv_obj_t *scrlbl = lv_label_create(scr);
    lv_label_set_text(scrlbl, "Debug");
    lv_obj_set_style_text_font(scrlbl, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_color(scrlbl, lv_color_white(), 0);
    // lv_obj_set_style_text_align(scrlbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_pad_top(scrlbl, 12, 0);
    lv_obj_set_style_pad_bottom(scrlbl, 8, 0);

    lv_obj_t *startbutton = create_setting(scr, "Disable Sleep", !watch.system.dosleep, [](lv_event_t *e)
                                           { watch.system.dosleep = !lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); });

    return scr;
}