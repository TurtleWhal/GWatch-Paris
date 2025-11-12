#include "ui.hpp"
#include <sys/time.h>

lv_obj_t *canvas;

// LV_DRAW_BUF_DEFINE_STATIC(draw_buf_16bpp, 240, 240, LV_COLOR_FORMAT_RGB565);

void watchface_update()
{
    if (lv_obj_get_scroll_y(ver_layer) > lv_obj_get_y(hor_layer) - 240 && lv_obj_get_scroll_y(ver_layer) < lv_obj_get_y(hor_layer) + 240)
        if (lv_obj_get_scroll_x(hor_layer) > lv_obj_get_x(watchscr) - 240 && lv_obj_get_scroll_x(hor_layer) < lv_obj_get_x(watchscr) + 240)
        {
            // /* Fill the canvas with white color */
            // lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

            // lv_layer_t layer;
            // lv_canvas_init_layer(canvas, &layer);

            // /* Draw a line */
            // lv_draw_line_dsc_t line_dsc;
            // lv_draw_line_dsc_init(&line_dsc);
            // line_dsc.color = lv_color_black();
            // line_dsc.width = 2;

            // /* Define the line points */
            // lv_point_t line_points[] = {{120, 120}, {170, 70}};

            // /* Draw the line to the canvas */
            // lv_draw_line(&layer, &line_dsc);
            analogwatch_update();
        }
}

lv_obj_t *watchface_create(lv_obj_t *parent)
{
    lv_color_t accent = lv_theme_get_color_primary(parent);
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    // lv_obj_t *scr = create_screen(parent);
    // lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    // canvas = lv_canvas_create(parent);

    // LV_DRAW_BUF_INIT_STATIC(draw_buf_16bpp);

    // lv_obj_set_size(canvas, 240, 240);
    // lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *scr = analogwatch_create(parent);

    lv_timer_create([](lv_timer_t *timer)
                    { watchface_update(); }, 33, NULL);

    return scr;
}
