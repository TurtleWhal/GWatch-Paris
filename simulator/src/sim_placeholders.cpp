// Placeholder data the simulator injects at startup. On-device these
// values come from Gadgetbridge over BLE; here we hardcode them so the
// weather / music / notifications screens have something to render.
//
// Edit any of the values below, rebuild, refresh. Real images live in
// simulator/placeholders/ — drop a PNG, run the converter script, the
// loader picks the .rgb565 up automatically (procedural fallback if
// missing).
//
// Used by both the native and web builds — wired in from main.cpp.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "ble.hpp"
#include "esp_timer.h"

#ifndef SIM_PLACEHOLDERS_DIR
#define SIM_PLACEHOLDERS_DIR "."
#endif

extern uint32_t popup_seen_latest_id;   // notifications.cpp

// ============================================================================
// EDIT BELOW
// ============================================================================

namespace sim_data {

// ----- Weather --------------------------------------------------------------
// `temp_k` is integer Kelvin. Conversion is °F = (k - 273) * 9/5 + 32.
//   273 = 32°F   283 = 50°F   293 = 68°F   303 = 86°F
constexpr const char *kWeatherLocation    = "Lake Forest Park";
constexpr const char *kWeatherDescription = "Partly Cloudy";
constexpr int32_t     kWeatherTempKelvin  = 297;
constexpr int32_t     kWeatherHighKelvin  = 301;
constexpr int32_t     kWeatherLowKelvin   = 289;
constexpr uint8_t     kWeatherHumidityPct = 64;
constexpr uint8_t     kWeatherUVIndex     = 4;
constexpr uint16_t    kWeatherOWMCode     = 802;   /* "scattered clouds" */
constexpr float       kWeatherWindKph     = 12.0f;
constexpr uint16_t    kWeatherWindDirDeg  = 210;

// ----- Music ----------------------------------------------------------------
constexpr const char *kMusicState     = "play";    /* "play" / "pause" */
constexpr const char *kMusicArtist    = "Daft Punk";
constexpr const char *kMusicAlbum     = "Homework";
constexpr const char *kMusicTrack     = "Around the World";
constexpr int32_t     kMusicDurationS = 429;        /* 7:09 */
constexpr int32_t     kMusicPositionS = 92;         /* 1:32 */

// ----- The single pre-loaded notification -----------------------------------
// Shows up in the notifications screen at startup but does NOT trigger a
// popup. Use the SIM panel's "Notify" button to inject additional ones
// (those DO popup, mimicking a real arrival).
constexpr const char *kNotifSrc    = "garrettjordan.xyz";
constexpr const char *kNotifTitle  = "Hi from Garrett!";
constexpr const char *kNotifBody   = "Welcome to the\nG-Watch Simulator.";
constexpr const char *kNotifSender = "";

// Procedural fallback sizes — used only if the matching .rgb565 file
// isn't in simulator/placeholders/.
constexpr uint16_t kFallbackAlbumArtW  = 240;
constexpr uint16_t kFallbackAlbumArtH  = 240;
constexpr uint16_t kFallbackNotifIconW = 48;
constexpr uint16_t kFallbackNotifIconH = 48;

}  // namespace sim_data

// ============================================================================
// Helpers (rarely need to touch)
// ============================================================================

namespace {

// Pack r/g/b (0..255) into a little-endian RGB565 word.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline void write_rgb565(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

// Load a .rgb565 file (4-byte LE header [w][h] followed by w*h*2 bytes
// of pixel data) into the firmware's PsramByteVec format. Returns true
// on success and fills *out / *w / *h. See placeholders/README.md.
bool load_rgb565_file(const char *fname, PsramByteVec &out,
                      uint16_t &w, uint16_t &h)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", SIM_PLACEHOLDERS_DIR, fname);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint16_t hdr[2];
    if (fread(hdr, sizeof(uint16_t), 2, f) != 2) { fclose(f); return false; }
    w = hdr[0];
    h = hdr[1];

    size_t bytes = (size_t)w * h * 2u;
    out.assign(bytes, 0);
    bool ok = fread(out.data(), 1, bytes, f) == bytes;
    fclose(f);
    if (!ok) { out.clear(); return false; }

    fprintf(stderr, "[sim] loaded %s (%ux%u)\n", path, w, h);
    return true;
}

// Procedural album art: diagonal magenta→cyan gradient. Only runs if
// placeholders/album_art.rgb565 is missing.
void fill_album_art_fallback(PsramByteVec &out, uint16_t &w, uint16_t &h)
{
    w = sim_data::kFallbackAlbumArtW;
    h = sim_data::kFallbackAlbumArtH;
    out.assign((size_t)w * h * 2u, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float t = (x + y) / (float)(w + h);
            uint8_t r = (uint8_t)((1.0f - t) * 220.0f + 30.0f);
            uint8_t g = (uint8_t)( t        * 180.0f + 40.0f);
            uint8_t b = (uint8_t)((0.5f + 0.5f * t) * 255.0f);
            write_rgb565(&out[(y * w + x) * 2], rgb565(r, g, b));
        }
    }
}

// Procedural notification icon: coloured roundel hashed from `src`.
void fill_notif_icon_fallback(PsramByteVec &out, uint16_t &w, uint16_t &h,
                              const char *src)
{
    w = sim_data::kFallbackNotifIconW;
    h = sim_data::kFallbackNotifIconH;
    out.assign((size_t)w * h * 2u, 0);

    uint32_t hsh = 5381;
    for (const char *p = src; p && *p; ++p) hsh = (hsh * 33u) ^ (uint8_t)*p;
    const uint16_t bg = rgb565((hsh) & 0xFF, (hsh >> 8) & 0xFF, (hsh >> 16) & 0xFF);

    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float r  = (w * 0.5f) - 1.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = x - cx, dy = y - cy;
            bool inside = (dx * dx + dy * dy) <= (r * r);
            write_rgb565(&out[(y * w + x) * 2], inside ? bg : 0);
        }
    }
}

}  // namespace

// ============================================================================
// Entry point — called once from main.cpp after watch.init().
// ============================================================================

extern "C" void sim_load_placeholders(void)
{
    /* ---- Weather ---- */
    {
        auto &w = ble.weather_mut();
        w.txt       = sim_data::kWeatherDescription;
        w.loc       = sim_data::kWeatherLocation;
        w.temp_k    = sim_data::kWeatherTempKelvin;
        w.hi_k      = sim_data::kWeatherHighKelvin;
        w.lo_k      = sim_data::kWeatherLowKelvin;
        w.humidity  = sim_data::kWeatherHumidityPct;
        w.uv        = sim_data::kWeatherUVIndex;
        w.code      = sim_data::kWeatherOWMCode;
        w.wind_mps  = sim_data::kWeatherWindKph;
        w.wind_dir  = sim_data::kWeatherWindDirDeg;
        w.version++;
    }

    /* ---- Music ---- */
    {
        auto &m = ble.music_mut();
        m.state       = sim_data::kMusicState;
        m.artist      = sim_data::kMusicArtist;
        m.album       = sim_data::kMusicAlbum;
        m.track       = sim_data::kMusicTrack;
        m.duration_s  = sim_data::kMusicDurationS;
        m.position_s  = sim_data::kMusicPositionS;

        PsramByteVec art;
        uint16_t aw, ah;
        if (!load_rgb565_file("album_art.rgb565", art, aw, ah)) {
            fill_album_art_fallback(art, aw, ah);
        }
        /* Stage via the pending slot, not set_album_art() directly. The
         * music screen clears `music_state.album_art` on first sight of
         * a new track and expects the real image to land via the
         * promote_pending_album_art() path on the next refresh — bypass
         * that and our art gets wiped on first music_update(). */
        ble.post_pending_album_art(std::move(art), aw, ah);
    }

    /* ---- One pre-loaded notification, no popup ---- */
    {
        Notification notif;
        /* Use a low id (1) so any popup-triggering injection from the
         * SIM panel (which uses ids starting at 1000) clears the
         * `latest > popup_seen_latest_id` check and DOES popup. */
        notif.id      = 1;
        notif.src     = sim_data::kNotifSrc;
        notif.title   = sim_data::kNotifTitle;
        notif.body    = sim_data::kNotifBody;
        notif.sender  = sim_data::kNotifSender;
        notif.when_ms = esp_timer_get_time() / 1000;
        notif.reply   = false;

        uint16_t iw, ih;
        if (!load_rgb565_file("notif_icon.rgb565", notif.img, iw, ih)) {
            fill_notif_icon_fallback(notif.img, iw, ih, notif.src.c_str());
        }
        notif.img_w = iw;
        notif.img_h = ih;

        ble.notifications_mut().push_back(std::move(notif));
        ble.bump_version();

        /* Skip the popup for this seeded notification. popup_check_new
         * only fires when an arriving id exceeds this watermark — by
         * advancing it past our id, the next periodic poll treats the
         * notif as "already seen". */
        popup_seen_latest_id = notif.id;
    }
}
