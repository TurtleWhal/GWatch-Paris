#include "ui.hpp"

lv_obj_t *schedule_screen_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    // lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_pad_bottom(scr, 40, 0);

    lv_obj_t *appslabel = lv_label_create(scr);
    lv_label_set_text(appslabel, "Schedule");
    lv_obj_set_style_text_font(appslabel, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_color(appslabel, lv_color_white(), 0);
    // lv_obj_set_style_text_align(appslabel, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_pad_top(appslabel, 12, 0);
    lv_obj_set_style_pad_bottom(appslabel, 8, 0);

    watch.schedule.useSchedule = watch.settings.readUint8("useschedule", 1);

    create_setting(scr, "Use Schedule", watch.schedule.useSchedule, [](lv_event_t *e)
                   { watch.schedule.useSchedule = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); 
                     watch.settings.writeUint8("useschedule", watch.schedule.useSchedule ? 1 : 0); });

    // lv_obj_t *setting = lv_button_create(parent);
    // lv_obj_set_size(setting, 180, 44);
    // lv_obj_set_style_bg_color(setting, lv_color_hex(0x222222), 0);
    // lv_obj_set_style_radius(setting, LV_RADIUS_CIRCLE, 0);

    // lv_obj_t *label = lv_label_create(setting);
    // lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    // lv_label_set_text(label, name);
    // lv_obj_set_style_text_font(label, &ProductSansRegular_16, 0);

    // lv_obj_t *sw = lv_switch_create(setting);
    // lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t *dropdown = lv_dropdown_create(scr);
    lv_obj_set_size(dropdown, 180, 44);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(dropdown, 0, 0);
    lv_obj_set_style_radius(dropdown, LV_RADIUS_CIRCLE, 0);

    lv_dropdown_set_options(dropdown, "O Day\nE Day\nA Day\nY Day\nMorning Assembly");

    lv_obj_set_style_text_font(dropdown, &FontAwesome_18, LV_PART_INDICATOR);

    lv_obj_t *list = lv_dropdown_get_list(dropdown); /* Get list */

    lv_obj_set_size(list, 180, 44 * 1); // height does not appear to do anything to the list size
    lv_obj_set_style_bg_color(list, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 22, 0);

    lv_obj_t *full = lv_label_create(scr);

    lv_obj_set_style_text_font(full, &ProductSansRegular_16, 0);

    lv_obj_add_event_cb(scr, [](lv_event_t *e)
                        { lv_label_set_text((lv_obj_t *)lv_event_get_user_data(e), watch.schedule.getFullSchedule()); }, LV_EVENT_SCREEN_LOADED, full);

    lv_obj_add_event_cb(scr, [](lv_event_t *e)
                        { lv_dropdown_set_selected((lv_obj_t *)lv_event_get_user_data(e), (int)watch.schedule.getSelectedSchedule()); }, LV_EVENT_SCREEN_LOADED, dropdown);

    lv_obj_add_event_cb(dropdown, [](lv_event_t *e)
                        {
                             watch.schedule.setCurrentSchedule((ClassSchedule)lv_dropdown_get_selected(lv_event_get_target_obj(e)));
                             lv_label_set_text((lv_obj_t *)lv_event_get_user_data(e), watch.schedule.getFullSchedule()); }, LV_EVENT_VALUE_CHANGED, full);

    return scr;
}