#include "ui.hpp"

// File-local so the press handler can keep appending without re-binding
// pointers each draw_create call. Reserved up-front so push_back doesn't
// realloc out from under LVGL's mutable-points pointer mid-frame —
// lv_line_set_points_mutable stores the caller's pointer by reference,
// so a vector reallocation would invalidate the cached pointer between
// frames if we didn't pre-size or re-bind on every press.
static std::vector<lv_point_precise_t> points;

lv_obj_t *draw_create(lv_obj_t *parent)
{
    points.reserve(2048);

    lv_obj_t *scr = lv_obj_create(NULL);
    // lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_t *line = lv_line_create(scr);
    lv_obj_set_size(line, 240, 240);
    lv_line_set_points_mutable(line, points.data(), points.size());
    lv_obj_add_style(line, &accent_line_style, 0);
    lv_obj_set_style_line_width(line, 6, 0);
    lv_obj_set_style_line_rounded(line, true, 0);

    // PRESSING fires on the screen (lv_line is a draw-only widget and
    // doesn't have a real hit area, so events attached to it never fire).
    // The line is passed through user_data so the handler can re-bind
    // the points pointer after each push_back in case the vector grew
    // its backing storage.
    lv_obj_add_event_cb(scr, [](lv_event_t *e){
        // points.clear();

        lv_point_t current;
        lv_indev_get_point(lv_indev_active(), &current);
        points.push_back(lv_point_to_precise(&current));
        lv_obj_t *line = (lv_obj_t *)lv_event_get_user_data(e);
        lv_line_set_points_mutable(line, points.data(), points.size());
        lv_obj_invalidate(line);
    }, LV_EVENT_PRESSED, line);

    lv_obj_add_event_cb(scr, [](lv_event_t *e){
        lv_point_t current;
        lv_indev_get_point(lv_indev_active(), &current);
        points.push_back(lv_point_to_precise(&current));
        lv_obj_t *line = (lv_obj_t *)lv_event_get_user_data(e);
        lv_line_set_points_mutable(line, points.data(), points.size());
        lv_obj_invalidate(line);
    }, LV_EVENT_PRESSING, line);

    return scr;
}