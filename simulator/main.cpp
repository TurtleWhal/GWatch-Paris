// Simulator entry point. Initialises LVGL + SDL, then drives the watch's
// real UI by calling Display::ui_init from the stubbed Watch class.
//
// The full main/ui/** tree compiles unchanged — every ESP-IDF and FreeRTOS
// header it pulls in is shimmed under simulator/shim/include/, and the
// subsystem classes (Watch, Display, BLE, Settings, IMU, motor, battery)
// have no-op host implementations in simulator/src/stubs.cpp.

#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "hal_sdl.h"

#include "watch.hpp"

extern "C" void sim_nvs_load_from_disk(void);

// Black mask on the LVGL top layer with a centered anti-aliased circular
// cutout. Makes the rectangular SDL window look like the watch's round
// 240×240 LCD. Lives in the simulator translation unit (not the firmware)
// because on-device the panel already physically clips to a circle.
static lv_color32_t g_mask_buf[240 * 240];

static void install_circle_mask(void)
{
    constexpr int  W      = 240;
    constexpr int  H      = 240;
    constexpr float CX    = (W - 1) * 0.5f;
    constexpr float CY    = (H - 1) * 0.5f;
    constexpr float R     = 120.0f;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = x - CX;
            float dy = y - CY;
            float d  = sqrtf(dx * dx + dy * dy);

            /* Linear 1-pixel feather across the rim so the edge doesn't
             * step-stair. Outside R fully opaque, inside R-1 fully
             * transparent, blend in between. */
            uint8_t a;
            if (d >= R)            a = 255;
            else if (d <= R - 1.f) a = 0;
            else                   a = (uint8_t)((d - (R - 1.f)) * 255.0f);

            g_mask_buf[y * W + x] = lv_color32_t{0, 0, 0, a};
        }
    }

    lv_obj_t *canvas = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas, g_mask_buf, W, H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();
    sdl_hal_init(240, 240);

    /* Load NVS contents from disk so settings (theme color, sleep
     * timeout, watchface index, …) survive across simulator launches. */
    sim_nvs_load_from_disk();

    /* Mirror of watch_init(): bring up our stubbed subsystems and call
     * Display::ui_init, which builds the screen tree from main/ui. */
    watch.init();

    /* Round-LCD mask. After ui_init so it lands on top of the screen
     * tree the watch built (lv_layer_top renders above everything else). */
    install_circle_mask();

    while (1) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms == LV_NO_TIMER_READY)
            sleep_ms = LV_DEF_REFR_PERIOD;
        usleep(sleep_ms * 1000);
    }

    return 0;
}
