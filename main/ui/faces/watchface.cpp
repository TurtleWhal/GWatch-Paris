#include "ui.hpp"
#include <sys/time.h>

// Registry of watch faces the user can pick between. Index 0 is the boot
// default; the selected index is persisted in NVS under "watchface". Add a
// new face by writing a `*_create` / `*_update` pair (signatures must match
// the existing ones) and dropping an entry here.
typedef struct
{
    const char *name;
    lv_obj_t *(*create)(lv_obj_t *);
    void (*update)();
} watchface_def_t;

static const watchface_def_t WATCHFACES[] = {
    {"Analog", analogwatch_create, analogwatch_update},
    {"Rotary", rotarywatch_create, rotarywatch_update},
    {"Dive", divewatch_create, divewatch_update},
    {"Time", timescreen_create, timescreen_update},
    {"Words", wordwatch_create, wordwatch_update},
};
static constexpr uint8_t WATCHFACE_N =
    sizeof(WATCHFACES) / sizeof(WATCHFACES[0]);

static uint8_t g_active_face_idx = 0;

uint8_t watchface_count() { return WATCHFACE_N; }

const char *watchface_name_at(uint8_t idx)
{
    if (idx >= WATCHFACE_N)
        idx = 0;
    return WATCHFACES[idx].name;
}

uint8_t watchface_active_idx() { return g_active_face_idx; }

// Build the face obj for whatever index NVS says is active. Caller owns the
// returned obj (it's placed under `parent`). Also caches the index in
// `g_active_face_idx` so watchface_update() can dispatch to the right
// `*_update` without re-reading NVS every frame.
static lv_obj_t *build_active_face(lv_obj_t *parent)
{
    uint8_t idx = watch.settings.readUint8("watchface", 0);
    if (idx >= WATCHFACE_N)
        idx = 0;
    g_active_face_idx = idx;
    return WATCHFACES[idx].create(parent);
}

void watchface_update()
{
    if (lv_obj_get_scroll_y(ver_layer) > lv_obj_get_y(hor_layer) - 240 && lv_obj_get_scroll_y(ver_layer) < lv_obj_get_y(hor_layer) + 240)
        if (lv_obj_get_scroll_x(hor_layer) > lv_obj_get_x(watchscr) - 240 && lv_obj_get_scroll_x(hor_layer) < lv_obj_get_x(watchscr) + 240)
        {
            WATCHFACES[g_active_face_idx].update();
        }
}

lv_obj_t *watchface_create(lv_obj_t *parent)
{
    lv_obj_t *scr = build_active_face(parent);

    // Tick timer is created once even if watchface_create is called again
    // (it isn't currently, but watchface_set_active() rebuilds the face by
    // calling build_active_face directly, not this function — keeping the
    // guard makes the contract explicit in case that changes).
    static bool timer_created = false;
    if (!timer_created)
    {
        // Match LV_DEF_REFR_PERIOD (16 ms) so the sweep-second faces get a
        // fresh hand position for every refresh cycle instead of every
        // other one — a 33 ms tick against a 16 ms refresh makes the sweep
        // visibly step at half rate.
        lv_timer_create([](lv_timer_t *timer)
                        { watchface_update(); }, 16, NULL);
        timer_created = true;
    }

    return scr;
}

// Persist a new face index and swap the live face obj. Safe to call at any
// time — the new obj replaces `watchface` under the same parent, and the
// scroll-driven visibility code in ui.cpp picks it up on the next scroll
// event. No-ops if idx is out of range or already active.
void watchface_set_active(uint8_t idx)
{
    if (idx >= WATCHFACE_N || idx == g_active_face_idx)
        return;

    watch.settings.writeUint8("watchface", idx);

    lv_obj_t *parent = lv_obj_get_parent(watchface);
    lv_obj_delete(watchface);
    watchface = build_active_face(parent);

    // At boot, watchface is the first child of main_screen so ver_layer /
    // hor_layer (created after it in ui_init) render on top. Re-creating
    // here appends the new obj at the END of main_screen's child list,
    // which would put it in front of those layers — that traps touches on
    // the face, breaks the scroll-driven visibility gate, and stalls
    // watchface_update. Drop it back to the bottom to restore z-order.
    lv_obj_move_background(watchface);
}
