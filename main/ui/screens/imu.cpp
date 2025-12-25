#include "ui.hpp" // includes watch.hpp which includes imu.cpp

lv_obj_t *imudot;
lv_obj_t *maxline;

#define NUM_POINTS 60
#define INTERPOLATION_STEPS 30 // Number of interpolation points between readings

static lv_point_precise_t maxpoints[NUM_POINTS + 1] = {{0, 0}};
float magnitudes[NUM_POINTS];
float maxmagnitude = 1.0;

lv_obj_t *circle;

void imuscreen_update(lv_timer_t *timer)
{
    static bool displayed = false;
    static float prev_x = 0.0f;
    static float prev_y = 0.0f;

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

        // Calculate straight-line distance between previous and current point
        float dx = a.x - prev_x;
        float dy = a.y - prev_y;
        float distance = sqrt(dx * dx + dy * dy);

        // Interpolate along the straight line from prev to current
        int steps = (int)(distance * INTERPOLATION_STEPS);
        if (steps < 1)
            steps = 1;

        for (int step = 0; step <= steps; step++)
        {
            float t = (float)step / (float)steps;

            // Linear interpolation in Cartesian space (straight line)
            float interp_x = prev_x * (1.0f - t) + a.x * t;
            float interp_y = prev_y * (1.0f - t) + a.y * t;

            // Convert to polar coordinates
            float interp_angle = atan2(interp_x, interp_y) + M_PI;
            uint16_t interp_idx = round(interp_angle * NUM_POINTS / M_TWOPI);
            if (interp_idx >= NUM_POINTS)
                interp_idx = 0;

            float interp_magnitude = sqrt(interp_x * interp_x + interp_y * interp_y);

            // Update if this is a new maximum for this index
            if (magnitudes[interp_idx] < interp_magnitude)
            {
                magnitudes[interp_idx] = interp_magnitude;
                maxpoints[interp_idx] = {
                    120 + interp_magnitude * (100 / maxmagnitude) * cosf(-M_PI_2 - (interp_idx * M_TWOPI / NUM_POINTS)),
                    120 + interp_magnitude * (100 / maxmagnitude) * sinf(-M_PI_2 - (interp_idx * M_TWOPI / NUM_POINTS))};

                if (interp_magnitude > maxmagnitude)
                {
                    maxmagnitude = interp_magnitude;

                    for (uint8_t i = 0; i < NUM_POINTS; i++)
                    {
                        maxpoints[i] = {
                            120 + magnitudes[i] * (100 / maxmagnitude) * cosf(-M_PI_2 - (i * M_TWOPI / NUM_POINTS)),
                            120 + magnitudes[i] * (100 / maxmagnitude) * sinf(-M_PI_2 - (i * M_TWOPI / NUM_POINTS))};

                        lv_obj_set_size(circle, 200 / maxmagnitude, 200 / maxmagnitude);
                    }
                }

                if (interp_idx == 0)
                {
                    maxpoints[NUM_POINTS] = maxpoints[0];
                }
            }
        }

        lv_line_set_points_mutable(maxline, maxpoints, NUM_POINTS + 1);

        // Store current values for next iteration
        prev_x = a.x;
        prev_y = a.y;

        lv_obj_align(imudot, LV_ALIGN_CENTER, a.x * (100 / maxmagnitude), a.y * (100 / maxmagnitude));
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
    lv_color_t accent = lv_theme_get_color_primary(parent);
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);

    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    circle = lv_obj_create(scr);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x444444), 0);
    lv_obj_set_size(circle, 200, 200);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_0, 0);
    lv_obj_center(circle);

    lv_obj_add_event_cb(scr, [](lv_event_t *e)
                        {
                            for (uint16_t i = 0; i < NUM_POINTS; i++)
                            {
                                magnitudes[i] = 0.0;
                                maxpoints[i] = {120, 120};
                                maxmagnitude = 1.0;
                            }
                            maxpoints[NUM_POINTS] = {120, 120};
                            
                            lv_obj_set_size(circle, 200, 200); }, LV_EVENT_DOUBLE_CLICKED, NULL);

    lv_obj_send_event(scr, LV_EVENT_DOUBLE_CLICKED, NULL);

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
    lv_obj_set_style_line_color(maxline, accent, 0);
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