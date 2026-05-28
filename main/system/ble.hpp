#ifndef BLE_HPP
#define BLE_HPP

#include <stdint.h>

#ifdef __cplusplus

#include <cstdlib>
#include <string>
#include <vector>

#include "esp_heap_caps.h"

// std::vector allocator that pulls from PSRAM (SPIRAM). Internal SRAM is
// tight on this build — BT controller alone takes ~80 KB and there's not
// much left for ~4–5 KB notification icon buffers, especially as several
// pile up in the queue. PSRAM has megabytes free. LVGL only reads these
// bytes to blit into its own (internal-RAM) draw buffer, so PSRAM's lack
// of DMA-capability doesn't matter here.
template <typename T>
struct PsramAllocator
{
    using value_type = T;
    PsramAllocator() = default;
    template <typename U> PsramAllocator(const PsramAllocator<U> &) {}
    T *allocate(size_t n)
    {
        void *p = heap_caps_malloc(n * sizeof(T),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        // Exceptions are disabled in this build, so we can't `throw
        // std::bad_alloc()`; abort like the default operator new does
        // under -fno-exceptions. In practice PSRAM has megabytes free
        // and this allocation is just a few KB.
        if (!p)
            std::abort();
        return static_cast<T *>(p);
    }
    void deallocate(T *p, size_t) noexcept { heap_caps_free(p); }
    template <typename U> bool operator==(const PsramAllocator<U> &) const { return true; }
    template <typename U> bool operator!=(const PsramAllocator<U> &) const { return false; }
};

using PsramByteVec = std::vector<uint8_t, PsramAllocator<uint8_t>>;

struct Notification
{
    uint32_t id;
    std::string src;    // app/source name (e.g. "Slack", "Messages")
    std::string title;
    std::string body;
    std::string sender; // for messages, the contact name
    int64_t when_ms;    // esp_timer_get_time() / 1000 at receipt
    bool reply;
    std::vector<std::string> replies;

    // Decoded RGB565 pixel buffer for the notification icon, filled in by
    // a follow-up `notify-img` GB message that carries the raw bitmap.
    // img_w * img_h * 2 == img.size() when present; empty until (and if)
    // the image arrives. The UI renders a placeholder otherwise.
    // Stored in PSRAM via PsramAllocator — see the comment on it above.
    PsramByteVec img;
    uint16_t img_w = 0;
    uint16_t img_h = 0;
};

struct MusicState
{
    std::string state;  // "play" / "pause"
    std::string artist;
    std::string album;
    std::string track;
    int32_t duration_s = 0;
    int32_t position_s = 0;

    // Album art delivered via the image GATT service with image_kind=0x01.
    // album_art_w * album_art_h * 2 == album_art.size() when present; the
    // pixels are raw RGB565 LE in PSRAM, same layout as notification icons.
    PsramByteVec album_art;
    uint16_t album_art_w = 0;
    uint16_t album_art_h = 0;
};

class BLE
{
public:
    void init();

    bool connected() const { return conn_handle != 0xffff; }

    // Stop/start advertising. When disabled, any active connection is
    // dropped and we no longer show up to scanners. The radio stays
    // powered (cheaper than a full nimble teardown, and good enough for
    // a "discoverable" toggle).
    void set_enabled(bool on);
    bool is_enabled() const { return enabled; }

    // Outgoing (watch -> phone). Wraps payload as `\x10GB(<json>)\n`.
    // Safe to call from any task; returns false if no peer is subscribed.
    bool send_gb(const char *json_payload);

    // High-level outgoing helpers. All are safe to call from any task.
    void send_status();                    // battery, charging
    void send_music_control(const char *cmd); // "play","pause","next","previous","volumeup","volumedown"
    void send_notification_action(uint32_t id, const char *action); // "DISMISS","OPEN","REPLY"
    // Send a REPLY action with a body string. cJSON-built so quotes,
    // backslashes, and multi-byte UTF-8 (the reply suggestions list
    // includes things like ":)" and emoji) get escaped correctly.
    void send_notification_reply(uint32_t id, const char *text);
    void send_find_phone(bool on);

    // Capture the current LVGL screen and stream it to the phone as a
    // BMP file via Gadgetbridge's `{t:"file"}` chunked-write protocol.
    // Heavyweight (~170 KB transfer) — call from the BLE rx task on a
    // user-triggered `screenshot` command, not periodically.
    void send_screenshot();

    // Notification queue. Mutated from the BLE task only — UI reads from
    // LVGL task. Callers must hold lvgl_port_lock OR be on the LVGL task
    // when iterating.
    const std::vector<Notification> &notifications() const { return notifs; }
    void clear_notifications();
    void dismiss_notification(uint32_t id, bool send_to_phone = true);

    // Monotonic counter that's bumped on every mutation to the notification
    // list (add, dismiss, image attach). UI uses it to decide whether to
    // re-render — cheaper than diffing the queue every poll.
    uint32_t notifications_version() const { return notifs_version; }

    // Mutable accessor for the image-transfer worker to patch in PNG bytes
    // after the matching notification's text has already been queued.
    // Caller must call bump_version() after the mutation.
    std::vector<Notification> &notifications_mut() { return notifs; }
    void bump_version() { notifs_version++; }

    const MusicState &music() const { return music_state; }

    // Install album art delivered via the image GATT service. Called
    // from ble_rx_task when an image transfer with image_kind=0x01
    // completes. Takes ownership of the pixel buffer.
    void set_album_art(PsramByteVec &&pixels, uint16_t w, uint16_t h)
    {
        music_state.album_art = std::move(pixels);
        music_state.album_art_w = w;
        music_state.album_art_h = h;
    }

    // Two-stage album-art install. The BLE task stages a new image in
    // an internal pending slot (without taking lvgl_port_lock, so it
    // works during light sleep when the LVGL task is suspended); the
    // LVGL task drains the pending slot here on its next music_update
    // tick, where the implicit LVGL lock makes the swap atomic with
    // the descriptor rebind in the UI.
    void post_pending_album_art(PsramByteVec &&pixels, uint16_t w, uint16_t h);
    bool promote_pending_album_art();

    // The fields below are effectively module-private, but exposed for
    // file-scope C callbacks/initialisers in ble.cpp. Don't touch from
    // outside the BLE module.
    uint16_t tx_attr_handle = 0;
    // Battery Level (0x2A19) value handle for the standard Battery
    // Service (0x180F). Filled in by NimBLE during gatts_add_svcs; used
    // by send_status to push BAS notifies so Android's connected-device
    // battery widget tracks the level without the Gadgetbridge channel.
    uint16_t bas_lvl_handle = 0;
    uint16_t conn_handle = 0xffff;
    uint8_t own_addr_type = 0;
    char device_name[20] = {0};

    void start_advertising();
    void handle_line(const char *line, size_t len);
    void handle_gb_json(const char *json, size_t len);

private:
    void push_notification(Notification &&n);

    bool enabled = true;
    std::vector<Notification> notifs;
    uint32_t notifs_version = 0;
    MusicState music_state;
};

extern BLE ble;

#endif // __cplusplus

#endif // BLE_HPP
