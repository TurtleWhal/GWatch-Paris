#include "ui.hpp" // includes watch.hpp which includes imu.cpp

lv_obj_t *imudot;
lv_obj_t *maxline;

#define NUM_POINTS 60
static lv_point_precise_t maxpoints[NUM_POINTS + 1] = {{0, 0}};
float magnitudes[NUM_POINTS];

// void imuscreen_update(lv_timer_t *timer)
// {
//     static bool displayed = false;
//
//     lv_obj_t *scr = (lv_obj_t *)lv_timer_get_user_data(timer);
//     lv_obj_t *parent = lv_obj_get_parent(scr);
//
//     if (lv_obj_get_scroll_x(parent) == lv_obj_get_x(scr))
//     {
//         if (!displayed)
//         {
//             displayed = true;
//             watch.system.sleeptime = 60000;
//         }
//
//         Acceleration a = imu_read();
//
//         switch (lv_display_get_rotation(NULL))
//         {
//         case LV_DISP_ROTATION_0:
//             // add when useful
//             break;
//         case LV_DISP_ROTATION_90:
//             a.x *= 1;
//             a.y *= 1;
//             break;
//         case LV_DISP_ROTATION_180:
//             // add when useful
//             break;
//         case LV_DISP_ROTATION_270:
//             a.x *= -1;
//             a.y *= -1;
//             break;
//         }
//
//         float angle = atan2(a.x, a.y) + M_PI; // add 180˚ (0.5π radians) to make it 0˚ - 360˚ instead of -180˚ - 180˚
//         uint16_t idx = round(angle * NUM_POINTS / M_TWOPI);
//
//         float magnitude = sqrt(a.x * a.x + a.y * a.y);
//
//         // printf("x: %f, y: %f, z: %f\n", a.x, a.y, a.z);
//         // printf("Magnitude: %f, Angle: %f, idx: %d\n", magnitude, angle, idx);
//
//         static uint16_t prev_idx = 0;
//
//         if (magnitudes[idx] < magnitude)
//         {
//             magnitudes[idx] = magnitude;
//             maxpoints[idx] = {
//                 120 + magnitude * 100 * cosf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS)),
//                 120 + magnitude * 100 * sinf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS))};
//
//             // --- Interpolate between previous and current index ---
//             int diff = (int)idx - (int)prev_idx;
//             if (abs(diff) > 1 && abs(diff) < NUM_POINTS / 2)
//             {
//                 int step = (diff > 0) ? 1 : -1;
//                 for (int i = prev_idx + step; i != idx; i += step)
//                 {
//                     float t = (float)(i - prev_idx) / (float)(idx - prev_idx);
//                     float interp_mag = magnitudes[prev_idx] * (1.0f - t) + magnitude * t;
//                     magnitudes[i] = interp_mag;
//                     maxpoints[i] = {
//                         120 + interp_mag * 100 * cosf(-M_PI_2 - (i * M_TWOPI / NUM_POINTS)),
//                         120 + interp_mag * 100 * sinf(-M_PI_2 - (i * M_TWOPI / NUM_POINTS))};
//                 }
//             }
//
//             if (idx == 0)
//                 maxpoints[NUM_POINTS] = maxpoints[0];
//
//             lv_line_set_points_mutable(maxline, maxpoints, NUM_POINTS + 1);
//         }
//         prev_idx = idx;
//
//         lv_obj_align(imudot, LV_ALIGN_CENTER, a.x * 100, a.y * 100);
//         lv_obj_set_size(imudot, 4 + abs(a.z) * 16, 4 + abs(a.z) * 16);
//     }
//     else if (displayed)
//     {
//         displayed = false;
//         watch.system.sleeptime = DEFAULT_SLEEP_TIME;
//     }
// }

void imuscreen_update(lv_timer_t *timer)
{
    static bool displayed = false;
    lv_obj_t *scr = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_obj_t *parent = lv_obj_get_parent(scr);
    if (lv_obj_get_scroll_x(parent) == lv_obj_get_x(scr))
    {
        if (!displayed)
        {
            displayed = true;
            watch.system.sleeptime = 60000;
        }

        Acceleration a = accel_read();

        switch (lv_display_get_rotation(NULL))
        {
        case LV_DISP_ROTATION_0:
            // add when useful
            break;
        case LV_DISP_ROTATION_90:
            a.x *= 1;
            a.y *= 1;
            break;
        case LV_DISP_ROTATION_180:
            // add when useful
            break;
        case LV_DISP_ROTATION_270:
            a.x *= -1;
            a.y *= -1;
            break;
        }

        float angle = atan2(a.x, a.y) + M_PI; // add 180˚ (0.5π radians) to make it 0˚ - 360˚ instead of -180˚ - 180˚
        uint16_t idx = round(angle * NUM_POINTS / M_TWOPI);
        float magnitude = sqrt(a.x * a.x + a.y * a.y);

        // printf("x: %f, y: %f, z: %f\n", a.x, a.y, a.z);
        // printf("Magnitude: %f, Angle: %f, idx: %d\n", magnitude, angle, idx);

        if (magnitudes[idx] < magnitude)
        {
            // printf("New Max: Magnitude: %f, idx: %d\n", magnitude, idx);
            magnitudes[idx] = magnitude;
            maxpoints[idx] = {120 + magnitude * 100 * cosf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS)), 120 + magnitude * 100 * sinf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS))};

            if (idx == 0)
            {
                maxpoints[NUM_POINTS] = {120 + magnitude * 100 * cosf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS)), 120 + magnitude * 100 * sinf(-M_PI_2 - (idx * M_TWOPI / NUM_POINTS))};
            }

            lv_line_set_points_mutable(maxline, maxpoints, NUM_POINTS + 1);
        }

        lv_obj_align(imudot, LV_ALIGN_CENTER, a.x * 100, a.y * 100);
        lv_obj_set_size(imudot, 4 + abs(a.z) * 16, 4 + abs(a.z) * 16);
    }
    else if (displayed)
    {
        displayed = false;
        watch.system.sleeptime = DEFAULT_SLEEP_TIME;
    }
}

lv_obj_t *imu_screen_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_add_event_cb(scr, [](lv_event_t *e)
                        {
                            for (uint16_t i = 0; i < NUM_POINTS; i++)
                            {
                                magnitudes[i] = 0.0;
                                maxpoints[i] = {120, 120};
                            }

                            maxpoints[NUM_POINTS] = {120, 120}; }, LV_EVENT_DOUBLE_CLICKED, NULL);

    lv_obj_send_event(scr, LV_EVENT_DOUBLE_CLICKED, NULL);

    // lv_obj_t *scrlbl = lv_label_create(scr);
    // lv_label_set_text(scrlbl, "Accelerometer");
    // lv_obj_set_style_text_font(scrlbl, &ProductSansRegular_20, 0);
    // lv_obj_set_style_text_color(scrlbl, lv_color_white(), 0);
    // lv_obj_align(scrlbl, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *circle = lv_obj_create(scr);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x444444), 0);
    lv_obj_set_size(circle, 200, 200);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_0, 0);
    lv_obj_center(circle);

    static lv_point_precise_t x_points[] = {{10, 120}, {230, 120}};
    lv_obj_t *xline = lv_line_create(scr);
    lv_line_set_points(xline, x_points, 2);
    lv_obj_set_style_line_width(xline, 2, 0);
    lv_obj_set_style_line_color(xline, lv_color_hex(0x444444), 0);

    static lv_point_precise_t y_points[] = {{120, 10}, {120, 230}};
    lv_obj_t *yline = lv_line_create(scr);
    lv_line_set_points(yline, y_points, 2);
    lv_obj_set_style_line_width(yline, 2, 0);
    lv_obj_set_style_line_color(yline, lv_color_hex(0x444444), 0);

    maxline = lv_line_create(scr);
    lv_line_set_points_mutable(maxline, maxpoints, NUM_POINTS + 1);
    lv_obj_set_style_line_color(maxline, lv_color_hex(0x888888), 0);
    lv_obj_set_style_line_width(maxline, 4, 0);
    lv_obj_set_style_line_rounded(maxline, true, 0);

    imudot = lv_obj_create(scr);
    lv_obj_set_style_border_width(imudot, 0, 0);
    lv_obj_set_size(imudot, 20, 20);
    lv_obj_set_style_radius(imudot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scroll_dir(imudot, LV_DIR_NONE);
    lv_obj_set_style_bg_color(imudot, lv_color_white(), 0);

    lv_obj_center(imudot);

    lv_obj_add_flag(circle, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(xline, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(yline, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(maxline, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(imudot, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_timer_create(imuscreen_update, 50, scr); // 20 times per second

    return scr;
}