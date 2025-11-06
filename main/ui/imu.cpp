#include "ui.hpp" // includes watch.hpp which includes imu.cpp

lv_obj_t *imudot;
lv_obj_t *maxline;

#define NUM_POINTS 60
static lv_point_precise_t maxpoints[NUM_POINTS + 1] = {{0, 0}};
float magnitudes[NUM_POINTS];

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

            for (uint16_t i = 0; i < NUM_POINTS; i++)
            {
                magnitudes[i] = 0.0;
                maxpoints[i] = {120, 120};
            }

            maxpoints[NUM_POINTS] = {120, 120};
        }

        Acceleration a = imu_read();

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
    }
}

lv_obj_t *imu_screen_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

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

    lv_timer_create(imuscreen_update, 50, scr); // 20 times per second

    return scr;
}