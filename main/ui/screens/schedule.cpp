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

    // Sync from config.json — schedule.enabled is the source of truth
    // now. Re-read on each screen build so a `{t:"cfg-set"}` push over
    // BLE (or a hand-edit of the sim's config.json between opens)
    // reflects on the toggle.
    watch.schedule.useSchedule =
        watch.settings.readBool("schedule", "enabled", true);

    create_setting(scr, "Use Schedule", watch.schedule.useSchedule, [](lv_event_t *e)
                   { watch.schedule.useSchedule = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
                     watch.settings.writeBool("schedule", "enabled", watch.schedule.useSchedule); });

    lv_obj_t *dropdown = lv_dropdown_create(scr);
    lv_obj_set_size(dropdown, 200, 44);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(dropdown, 0, 0);
    lv_obj_set_style_radius(dropdown, LV_RADIUS_CIRCLE, 0);

    // Dropdown options come straight from the loaded schedule names
    // instead of a hardcoded "O Day\nE Day\n…" string. Build once at
    // screen-create time — reloading config.json requires a reboot
    // for now, so the dropdown doesn't need to update live.
    {
        std::string opts;
        for (int i = 0; i < watch.schedule.scheduleCount(); i++) {
            if (i) opts += "\n";
            opts += watch.schedule.scheduleName(i);
        }
        if (opts.empty()) opts = "(none)";  // dropdown needs at least one line
        lv_dropdown_set_options(dropdown, opts.c_str());
    }

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