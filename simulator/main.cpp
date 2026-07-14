// Simulator entry point. Initialises LVGL + SDL, then drives the watch's
// real UI by calling Display::ui_init from the stubbed Watch class.
//
// The full main/ui/** tree compiles unchanged — every ESP-IDF and FreeRTOS
// header it pulls in is shimmed under simulator/shim/include/, and the
// subsystem classes (Watch, Display, BLE, Settings, IMU, motor, battery)
// have no-op host implementations in simulator/src/stubs.cpp.

#include <SDL.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "lvgl.h"
#include "hal_sdl.h"

#include "watch.hpp"
#include "ble.hpp"

extern "C" void sim_nvs_load_from_disk(void);
extern "C" void sim_controls_install(lv_display_t *sidebar_disp);
extern "C" void sim_load_placeholders(void);

/* Persisted window position. Stored as `x y\n` in a plain text file so
 * it's easy to inspect / wipe. Sidebar position is always derived from
 * the watch's, so only the watch coordinates go on disk. */
static const char *winpos_path()
{
    static char buf[1024];
    const char *home = getenv("HOME");
    snprintf(buf, sizeof(buf), "%s/.gwatch_sim_winpos",
             (home && *home) ? home : ".");
    return buf;
}

static bool load_window_pos(int *x, int *y)
{
    FILE *f = fopen(winpos_path(), "r");
    if (!f) return false;
    bool ok = fscanf(f, "%d %d", x, y) == 2;
    fclose(f);
    return ok;
}

static void save_window_pos(int x, int y)
{
    FILE *f = fopen(winpos_path(), "w");
    if (!f) return;
    fprintf(f, "%d %d\n", x, y);
    fclose(f);
}

/* Watch panel and the simulator's chrome each live in their own SDL window,
 * with their own LVGL display. The watch display is the LVGL default, so
 * `lv_obj_create(NULL)` in the firmware (e.g. popup notification screen)
 * sizes to 240×240 and centers on the round panel — not on the chrome. */
static constexpr int SIM_WATCH_W   = 240;
static constexpr int SIM_WATCH_H   = 240;
static constexpr int SIM_SIDEBAR_W = 140;
static constexpr int SIM_SIDEBAR_H = 240;

/* LVGL's SDL driver only exits the process on SDL_QUIT (Cmd+Q on macOS);
 * clicking the window X fires SDL_WINDOWEVENT_CLOSE per window and just
 * deletes the LVGL display. With detached pthread "tasks" from the
 * FreeRTOS stubs still alive, the process would otherwise spin in the
 * lv_timer_handler loop forever. This watch peeks every SDL event and
 * exits cleanly on any close. */
static int sim_quit_event_watch(void * /*userdata*/, SDL_Event *event)
{
    if (event->type == SDL_QUIT) {
        exit(0);
    }
    if (event->type == SDL_WINDOWEVENT &&
        event->window.event == SDL_WINDOWEVENT_CLOSE) {
        exit(0);
    }
    return 1;
}

/* Ctrl+C from the launching terminal — SDL doesn't translate SIGINT into
 * SDL_QUIT on macOS, so wire it up explicitly. */
static void sim_sigint_handler(int /*sig*/)
{
    _exit(0);
}

/* LVGL log filter. SDL fires mouse events with coords outside the window
 * whenever the cursor leaves the 240×240 area, which trips the indev
 * bounds warning four times per pixel of overhang. We drop those lines
 * (and only those) and pass everything else through to stdout. */
static void log_filter_cb(lv_log_level_t level, const char *buf)
{
    if (strstr(buf, "than hor. res") || strstr(buf, "than ver. res") ||
        strstr(buf, "X is ") || strstr(buf, "Y is ")) {
        return;
    }
    fputs(buf, level >= LV_LOG_LEVEL_WARN ? stderr : stdout);
}

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

            g_mask_buf[y * W + x] = lv_color32_t{60, 60, 60, a};
        }
    }

    lv_obj_t *canvas = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas, g_mask_buf, W, H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
}

#ifdef __EMSCRIPTEN__
/* Browser main-loop callback. Emscripten doesn't allow blocking sleeps
 * on the main thread, so we drive LVGL once per browser frame and let
 * it pick its own sleep. */
static void sim_web_tick(void)
{
    lv_timer_handler();
}
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifndef __EMSCRIPTEN__
    signal(SIGINT,  sim_sigint_handler);
    signal(SIGTERM, sim_sigint_handler);

    /* On macOS, clicking an unfocused SDL window normally just activates
     * it — the click is swallowed by the window manager and the button
     * under the cursor never sees it. This hint tells SDL to deliver
     * the click to the app on the *activating* press, so a single click
     * focuses the sidebar AND fires the button it landed on. Must be
     * set before SDL_Init (i.e. before any LVGL SDL driver call). */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

    lv_init();
    lv_log_register_print_cb(log_filter_cb);
    lv_display_t *watch_disp = sdl_hal_init(SIM_WATCH_W, SIM_WATCH_H);
    (void)watch_disp;

#ifndef __EMSCRIPTEN__
    lv_display_t *sidebar = sdl_hal_create_window(SIM_SIDEBAR_W, SIM_SIDEBAR_H, "sim controls");

    SDL_Window *watch_win   = lv_sdl_window_get_window(watch_disp);
    SDL_Window *sidebar_win = lv_sdl_window_get_window(sidebar);

    /* Position the watch where it was last time the user closed the
     * simulator. Fresh installs fall back to whatever SDL picks by default
     * (usually centered). The sidebar lives directly to the right of the
     * watch — same Y, X offset by the watch width. */
    int wx, wy;
    if (load_window_pos(&wx, &wy)) {
        SDL_SetWindowPosition(watch_win, wx, wy);
    } else {
        SDL_GetWindowPosition(watch_win, &wx, &wy);
    }
    SDL_SetWindowPosition(sidebar_win, wx + SIM_WATCH_W, wy);

    /* The two windows are a pair — pin both above other apps so launching
     * the sim from a VS Code task doesn't bury the watch behind the
     * editor. The user can still drag, minimize, and resize freely. */
    SDL_SetWindowAlwaysOnTop(watch_win,   SDL_TRUE);
    SDL_SetWindowAlwaysOnTop(sidebar_win, SDL_TRUE);
    SDL_RaiseWindow(watch_win);
    SDL_RaiseWindow(sidebar_win);

    /* Must come after the first SDL window is created — SDL doesn't keep
     * watches across SDL_Init/SDL_Quit cycles, so register once the
     * subsystem is up. */
    SDL_AddEventWatch(sim_quit_event_watch, nullptr);
#endif

    /* Load NVS contents from disk so settings (theme color, sleep
     * timeout, watchface index, …) survive across simulator launches. */
    sim_nvs_load_from_disk();

    /* Mirror of watch_init(): bring up our stubbed subsystems and call
     * Display::ui_init, which builds the screen tree from main/ui. */
    watch.init();

    /* Seed weather / music / notifications with the values defined in
     * sim_placeholders.cpp — edit that file to change what the demo
     * shows on each screen. */
    sim_load_placeholders();

#ifndef __EMSCRIPTEN__
    /* Round-LCD mask. After ui_init so it lands on top of the screen
     * tree the watch built (lv_layer_top renders above everything else).
     *
     * Skipped on web: the host page rounds the iframe via CSS
     * `border-radius: 50%`, and stacking the LVGL mask on top of that
     * leaves a 1px sub-pixel sliver of mask gray showing at the circle's
     * edge (the two clip circles can't be guaranteed to align). Letting
     * CSS do all the rounding produces a clean anti-aliased edge. */
    install_circle_mask();
#endif

#ifdef __EMSCRIPTEN__
    /* Web build's sim controls are plain HTML buttons in the page shell
     * around the canvas (see simulator/web/shell.html). They call into
     * the exported `sim_*` C functions in src/sim_controls_web.cpp via
     * Module.ccall, so there's nothing to install LVGL-side. Hand the
     * main loop off to the browser.
     *
     * Third arg MUST be 0 (don't simulate an infinite loop). With
     * simulate_infinite_loop=1 emscripten throws a JS exception to
     * unwind main; ASYNCIFY also unwinds the stack when vTaskDelay
     * hits emscripten_sleep, and the two mechanisms deadlock the
     * runtime after the first render. We return from main normally —
     * emcc's NO_EXIT_RUNTIME default keeps the WASM module + globals
     * alive after that, so the registered tick callback keeps firing. */
    emscripten_set_main_loop(sim_web_tick, 0, 0);
    return 0;
#else
    /* Sim-only side panel with buttons to inject notifications, toggle
     * charging, etc. Builds on the secondary display so it's painted in
     * its own SDL window — never visible inside the watch's frame. */
    sim_controls_install(sidebar);

    /* Record the launch position so a relaunch reopens here even if the
     * user never drags. Subsequent updates in the loop overwrite this. */
    SDL_GetWindowPosition(watch_win, &wx, &wy);
    save_window_pos(wx, wy);

    int last_wx = wx, last_wy = wy;
    while (1) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms == LV_NO_TIMER_READY)
            sleep_ms = LV_DEF_REFR_PERIOD;

        /* Keep the sidebar glued to the watch when the watch is dragged,
         * and persist the position so the next launch opens here. Skip
         * if the watch hasn't moved to avoid stealing focus from the
         * sidebar each tick. */
        int cwx, cwy;
        SDL_GetWindowPosition(watch_win, &cwx, &cwy);
        if (cwx != last_wx || cwy != last_wy) {
            SDL_SetWindowPosition(sidebar_win, cwx + SIM_WATCH_W, cwy);
            save_window_pos(cwx, cwy);
            last_wx = cwx;
            last_wy = cwy;
        }

        usleep(sleep_ms * 1000);
    }

    return 0;
#endif  /* !__EMSCRIPTEN__ */
}
