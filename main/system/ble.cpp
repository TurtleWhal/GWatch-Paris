#include "ble.hpp"
#include "watch.hpp"

#include <sys/time.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"

#include "freertos/queue.h"
#include "freertos/idf_additions.h"

#include "cJSON.h"
#include "mbedtls/base64.h"

extern "C"
{
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
}

static const char *TAG = "ble";

BLE ble;

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);

// Nordic UART Service (Bangle.js / Espruino).
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E   service
// 6E400002-...                           RX  (phone -> watch, write)
// 6E400003-...                           TX  (watch -> phone, notify)
static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t nus_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t nus_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

// RX-side incoming-data queue. The NimBLE host calls our access callback on
// its own task; we copy the bytes into a queue so we don't block the host.
struct RxChunk
{
    uint16_t len;
    uint8_t data[244]; // matches ATT_PREFERRED_MTU - 3
};
static QueueHandle_t rx_queue = nullptr;

// Large scratch buffers used during notify-img processing. Both held in
// PSRAM so they don't tax internal SRAM — the BT controller already takes
// ~80 KB and pushing 16 KB of BSS in here was making BT init OOM at boot.
// Sized for one 48×48 RGB565 icon (~6.2 KB base64, ~4.6 KB raw) with
// comfortable headroom for slightly larger payloads.
static constexpr size_t RX_LINE_BUF_SIZE = 8192;
static constexpr size_t RX_SCRATCH_SIZE = 8192;
static char *s_rx_line = nullptr;
static uint8_t *s_rx_scratch = nullptr;

int ble_nus_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return 0;

    RxChunk chunk;
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, chunk.data, sizeof(chunk.data), &copied);
    if (rc != 0)
        return BLE_ATT_ERR_UNLIKELY;

    chunk.len = copied;
    if (rx_queue)
        xQueueSend(rx_queue, &chunk, 0); // drop on overflow
    return 0;
}

static const struct ble_gatt_svc_def nus_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                // RX: phone -> watch
                .uuid = &nus_rx_uuid.u,
                .access_cb = ble_nus_rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // TX: watch -> phone (notify only)
                .uuid = &nus_tx_uuid.u,
                .access_cb = ble_nus_rx_access, // unused for notify-only
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &ble.tx_attr_handle,
            },
            {0},
        },
    },
    {0},
};

// ---------- Outgoing ----------

bool BLE::send_gb(const char *json_payload)
{
    if (!connected() || tx_attr_handle == 0)
        return false;

    // Frame: \r\n<json>\r\n\x1e. The 0x1E character is ASCII RS (Record
    // Separator), which is what Gadgetbridge's BangleJS UART parser uses
    // as the actual message terminator. Without it, Gadgetbridge logs
    // "unterminated string at position N" right at the message boundary.
    // The leading \r\n flushes any half-line residue from a previous send.
    static const char *PREFIX = "\r\n";
    static const char *SUFFIX = "\r\n\x1e";
    size_t prefix_len = 2;
    size_t suffix_len = 3;
    size_t json_len = strlen(json_payload);
    size_t total = prefix_len + json_len + suffix_len;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf)
        return false;
    memcpy(buf, PREFIX, prefix_len);
    memcpy(buf + prefix_len, json_payload, json_len);
    memcpy(buf + prefix_len + json_len, SUFFIX, suffix_len);

    // Always respect the negotiated MTU — passing oversize data to
    // ble_gatts_notify_custom can result in silent truncation or out-of-
    // order notifies that desync Gadgetbridge's line buffer (it reports
    // "unterminated string at position N" right at our message boundary).
    // BLE minimum MTU is 23, so chunk size is at least 20.
    uint16_t mtu = ble_att_mtu(conn_handle);
    if (mtu < 23)
        mtu = 23;
    size_t chunk = mtu - 3;
    ESP_LOGI(TAG, "TX %zu bytes (mtu=%u chunk=%zu): %.*s",
             total, mtu, chunk, (int)json_len, json_payload);

    size_t off = 0;
    bool ok = true;
    while (off < total)
    {
        size_t n = (total - off > chunk) ? chunk : (total - off);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf + off, n);
        if (!om)
        {
            ok = false;
            break;
        }
        if (ble_gatts_notify_custom(conn_handle, tx_attr_handle, om) != 0)
        {
            ok = false;
            break;
        }
        off += n;
    }

    free(buf);
    return ok;
}

void BLE::send_status()
{
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"status\",\"bat\":%u,\"chg\":%d,\"volt\":%.2f}",
             (unsigned)watch.battery.percent,
             watch.battery.charging ? 1 : 0,
             (double)watch.battery.voltage / 1000.0);
    send_gb(buf);
}

void BLE::send_music_control(const char *cmd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":\"music\",\"n\":\"%s\"}", cmd);
    send_gb(buf);
}

void BLE::send_notification_action(uint32_t id, const char *action)
{
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"notify\",\"id\":%" PRIu32 ",\"n\":\"%s\"}", id, action);
    send_gb(buf);
}

void BLE::send_find_phone(bool on)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"findPhone\",\"n\":%s}", on ? "true" : "false");
    send_gb(buf);
}

// ---------- Notification queue ----------

void BLE::push_notification(Notification &&n)
{
    // De-dupe: replace existing entry with same id. Preserve a previously-
    // attached icon across the replace, since the matching `notify-img`
    // message can land before or after the `notify` text on the same id.
    for (auto &existing : notifs)
    {
        if (existing.id == n.id)
        {
            if (n.img.empty() && !existing.img.empty())
            {
                n.img = std::move(existing.img);
                n.img_w = existing.img_w;
                n.img_h = existing.img_h;
            }
            existing = std::move(n);
            notifs_version++;
            return;
        }
    }
    if (notifs.size() >= 16)
        notifs.erase(notifs.begin());
    notifs.push_back(std::move(n));
    notifs_version++;
}

void BLE::dismiss_notification(uint32_t id, bool send_to_phone)
{
    bool changed = false;
    for (auto it = notifs.begin(); it != notifs.end(); ++it)
    {
        if (it->id == id)
        {
            notifs.erase(it);
            changed = true;
            break;
        }
    }
    if (changed)
        notifs_version++;
    if (send_to_phone)
        send_notification_action(id, "DISMISS");
}

void BLE::clear_notifications()
{
    if (notifs.empty()) return;
    notifs.clear();
    notifs_version++;
}

// ---------- Incoming command parsing ----------

// Optional helper: pull a child field as std::string. Missing → empty.
static std::string json_str(const cJSON *obj, const char *key)
{
    cJSON *f = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(f) && f->valuestring)
        return std::string(f->valuestring);
    return std::string();
}

// Attach a decoded RGB565 icon to the matching notification.
static void attach_notification_image(uint32_t id,
                                      PsramByteVec &&img,
                                      uint16_t w, uint16_t h)
{
    for (auto &n : ble.notifications_mut())
    {
        if (n.id == id)
        {
            n.img = std::move(img);
            n.img_w = w;
            n.img_h = h;
            ble.bump_version();
            ESP_LOGI(TAG, "attached %ux%u icon to notif id=%" PRIu32,
                     (unsigned)w, (unsigned)h, id);
            return;
        }
    }
    ESP_LOGW(TAG, "notify-img for unknown id=%" PRIu32 " — dropping", id);
}

// Translate JavaScript-style \xHH byte escapes into UTF-8 bytes, in
// place. Gadgetbridge ships musicinfo strings with these for non-ASCII
// characters in artist/track names (e.g. "Ti\xebsto") because that's
// what Bangle.js's Espruino parser accepts; cJSON is strict-JSON only
// and bails the moment it sees `\x`. Treating HH as Latin-1 and
// expanding to two-byte UTF-8 is the right move here — that's the
// semantic Gadgetbridge intends and it leaves the JSON otherwise
// untouched. Output is always ≤ input (4 chars in, 1 or 2 bytes out),
// so the rewrite is safe to do in place. Returns the new length.
static size_t fix_js_x_escapes(char *s, size_t len)
{
    auto hex = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    size_t r = 0, w = 0;
    while (r < len)
    {
        if (r + 3 < len && s[r] == '\\' && s[r + 1] == 'x')
        {
            int h1 = hex(s[r + 2]);
            int h2 = hex(s[r + 3]);
            if (h1 >= 0 && h2 >= 0)
            {
                unsigned b = (unsigned)((h1 << 4) | h2);
                if (b < 0x80)
                {
                    s[w++] = (char)b;
                }
                else
                {
                    s[w++] = (char)(0xC0 | (b >> 6));
                    s[w++] = (char)(0x80 | (b & 0x3F));
                }
                r += 4;
                continue;
            }
        }
        s[w++] = s[r++];
    }
    return w;
}

void BLE::handle_gb_json(const char *json, size_t len)
{
    // Rewrite JS-style \xHH escapes in place before parsing. The buffer
    // backing `json` is the static s_rx_line we own — safe to mutate;
    // const_cast just narrows the API obligation.
    len = fix_js_x_escapes(const_cast<char *>(json), len);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root)
    {
        // Log the offending message + cJSON's stop point so we can
        // see whether it's JS-style unquoted keys, a stray escape in a
        // string, a truncation, or something else.
        const char *err = cJSON_GetErrorPtr();
        unsigned off = err ? (unsigned)(err - json) : 0u;
        ESP_LOGW(TAG, "GB() JSON parse failed @%u: %.*s",
                 off, (int)len, json);
        return;
    }

    cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(t) || !t->valuestring)
    {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(t->valuestring, "notify") == 0)
    {
        Notification n;
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        n.id = cJSON_IsNumber(id) ? (uint32_t)id->valuedouble : 0;
        n.src = json_str(root, "src");
        n.title = json_str(root, "title");
        n.body = json_str(root, "body");
        if (n.body.empty())
            n.body = json_str(root, "subject");
        n.sender = json_str(root, "sender");
        n.when_ms = esp_timer_get_time() / 1000;

        ESP_LOGI(TAG, "notify: id=%" PRIu32 " src='%s' title='%s'",
                 n.id, n.src.c_str(), n.title.c_str());

        push_notification(std::move(n));

        // Buzz + wake the watch unless DND. The motor task itself handles
        // its own gating. watch.wakeup() is a no-op when already awake, so
        // this is safe to call unconditionally from the asleep case.
        if (!watch.donotdisturb)
        {
            haptic_play(false, 80, 80, 80, 0);
            watch.wakeup();
        }
    }
    else if (strcmp(t->valuestring, "notify-") == 0)
    {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsNumber(id))
            dismiss_notification((uint32_t)id->valuedouble, /*send_to_phone=*/false);
    }
    else if (strcmp(t->valuestring, "notify-img") == 0)
    {
        // Companion message that lands after a `notify`, carrying a base64-
        // encoded raw RGB565 icon for that id. Wire format of the decoded
        // base64 buffer:
        //   bytes 0..1: width  u16 LE
        //   bytes 2..3: height u16 LE
        //   bytes 4..:  w*h*2 RGB565 LE pixels, row-major, no padding
        // We strip the 4-byte header and store the pixels as-is so the UI
        // can hand them straight to LVGL with LV_COLOR_FORMAT_RGB565 — no
        // allocator, no color conversion.
        //
        // Memory: read the base64 directly off cJSON's valuestring (no
        // std::string copy) and decode into a BSS scratch buffer rather
        // than a heap vector — three concurrent 6 KB allocs (cJSON tree,
        // string copy, decode buffer) used to blow internal SRAM. Only
        // the final pixel buffer is heap-allocated.
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        cJSON *img_node = cJSON_GetObjectItemCaseSensitive(root, "img");
        if (!cJSON_IsNumber(id) || !cJSON_IsString(img_node) ||
            !img_node->valuestring)
        {
            ESP_LOGW(TAG, "notify-img missing id or img");
        }
        else
        {
            uint32_t nid = (uint32_t)id->valuedouble;
            const char *b64 = img_node->valuestring;
            size_t b64_len = strlen(b64);

            // PSRAM scratch (see file-scope s_rx_scratch).
            size_t raw_len = 0;
            int rc = mbedtls_base64_decode(s_rx_scratch, RX_SCRATCH_SIZE, &raw_len,
                                           (const unsigned char *)b64, b64_len);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "notify-img base64 decode rc=%d size=%u",
                         rc, (unsigned)b64_len);
            }
            else if (raw_len < 4)
            {
                ESP_LOGW(TAG, "notify-img payload too short (%u)",
                         (unsigned)raw_len);
            }
            else
            {
                uint16_t w = (uint16_t)s_rx_scratch[0] | ((uint16_t)s_rx_scratch[1] << 8);
                uint16_t h = (uint16_t)s_rx_scratch[2] | ((uint16_t)s_rx_scratch[3] << 8);
                size_t expected = 4 + (size_t)w * h * 2;
                if (w == 0 || h == 0 || raw_len != expected)
                {
                    ESP_LOGW(TAG, "notify-img size mismatch: w=%u h=%u "
                             "raw=%u expected=%u",
                             w, h, (unsigned)raw_len, (unsigned)expected);
                }
                else
                {
                    PsramByteVec pixels(s_rx_scratch + 4, s_rx_scratch + raw_len);
                    attach_notification_image(nid, std::move(pixels), w, h);
                }
            }
        }
    }
    else if (strcmp(t->valuestring, "find") == 0)
    {
        // Phone is asking us to ring (find watch).
        cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "n");
        bool on = cJSON_IsTrue(n);
        if (on)
            haptic_play(false, 200, 100, 200, 100, 200, 100, 200, 0);
    }
    else if (strcmp(t->valuestring, "musicstate") == 0)
    {
        music_state.state = json_str(root, "state");
        cJSON *pos = cJSON_GetObjectItemCaseSensitive(root, "position");
        if (cJSON_IsNumber(pos))
            music_state.position_s = (int32_t)pos->valuedouble;
    }
    else if (strcmp(t->valuestring, "musicinfo") == 0)
    {
        music_state.artist = json_str(root, "artist");
        music_state.album = json_str(root, "album");
        music_state.track = json_str(root, "track");
        cJSON *dur = cJSON_GetObjectItemCaseSensitive(root, "dur");
        if (cJSON_IsNumber(dur))
            music_state.duration_s = (int32_t)dur->valuedouble;
    }
    else if (strcmp(t->valuestring, "is_gps_active") == 0)
    {
        send_gb("{t:\"gps_power\", status: false}");
    } else {
        ESP_LOGW(TAG, "Unhandled gb msg: %s\n", json);
    }

    cJSON_Delete(root);
}

void BLE::handle_line(const char *line, size_t len)
{
    // Skip leading control chars (Bangle.js `\x10` echo-off prefix etc).
    while (len > 0 && (uint8_t)*line < 0x20 && *line != '\t')
    {
        line++;
        len--;
    }
    if (len == 0)
        return;

    // setTime(<unix_seconds>);  optionally followed by setTimeZone etc.
    if (len > 8 && strncmp(line, "setTime(", 8) == 0)
    {
        const char *p = line + 8;
        char *end = nullptr;
        long long ts = strtoll(p, &end, 10);
        if (end != p)
        {
            struct timeval tv = {.tv_sec = (time_t)ts, .tv_usec = 0};
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "time set from phone: %lld", ts);

            // Optional: parse out E.setTimeZone(<offset_hours>) too.
            const char *tz = strstr(line, "setTimeZone(");
            if (tz)
            {
                tz += 12;
                float off = strtof(tz, nullptr);
                int total_min = (int)(off * 60);
                int sign = total_min < 0 ? -1 : 1;
                int abs_min = total_min < 0 ? -total_min : total_min;
                char tzstr[16];
                snprintf(tzstr, sizeof(tzstr), "UTC%c%d:%02d",
                         sign > 0 ? '-' : '+', abs_min / 60, abs_min % 60);
                setenv("TZ", tzstr, 1);
                tzset();
            }
        }
        return;
    }

    // GB({...})
    if (len > 4 && strncmp(line, "GB(", 3) == 0)
    {
        const char *json = line + 3;
        size_t json_len = len - 3;
        if (json_len > 0 && json[json_len - 1] == ')')
            json_len--;
        // Some senders include trailing semicolons.
        while (json_len > 0 && (json[json_len - 1] == ';' || json[json_len - 1] == ' '))
            json_len--;
        handle_gb_json(json, json_len);
        return;
    }

    ESP_LOGD(TAG, "unhandled line: %.*s", (int)len, line);
}

// ---------- ble_rx_task: text / JSON line dispatcher ----------

// Pulls bytes off rx_queue, line-buffers them, dispatches.
//
// Line buffer is allocated in PSRAM (see s_rx_line above). 8 KB fits the
// largest single GB() message (`notify-img` with a base64 RGB565 icon)
// with comfortable headroom.
void ble_rx_task(void *arg)
{
    char *line = s_rx_line;
    size_t lpos = 0;

    RxChunk chunk;
    while (true)
    {
        if (xQueueReceive(rx_queue, &chunk, portMAX_DELAY) != pdTRUE)
            continue;

        for (uint16_t i = 0; i < chunk.len; i++)
        {
            uint8_t c = chunk.data[i];
            if (c == '\n' || c == '\r')
            {
                if (lpos > 0)
                {
                    line[lpos] = 0;
                    ble.handle_line(line, lpos);
                    lpos = 0;
                }
            }
            else if (lpos < RX_LINE_BUF_SIZE - 1)
            {
                line[lpos++] = (char)c;
            }
            else
            {
                ESP_LOGW(TAG, "rx line buffer overflow (%u bytes); dropping",
                         (unsigned)RX_LINE_BUF_SIZE);
                lpos = 0;
            }
        }
    }
}

// ---------- GAP / NimBLE plumbing ----------

void BLE::set_enabled(bool on)
{
    if (on == enabled)
        return;
    enabled = on;

    if (!on)
    {
        ble_gap_adv_stop();
        if (connected())
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ESP_LOGI(TAG, "advertising stopped");
    }
    else
    {
        // ble_on_sync set own_addr_type and device_name; safe to advertise now.
        if (device_name[0] != 0)
            start_advertising();
    }
}

void BLE::start_advertising()
{
    if (!enabled)
        return;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // 128-bit NUS UUID in main advertisement so phones can filter for it.
    fields.uuids128 = const_cast<ble_uuid128_t *>(&nus_svc_uuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    // Name in scan response (full 18+ chars don't fit alongside the
    // 128-bit UUID in the 31-byte primary AD).
    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (uint8_t *)device_name;
    rsp.name_len = strlen(device_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv, ble_gap_event_handler, NULL);
    if (rc != 0)
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    else
        ESP_LOGI(TAG, "advertising as '%s'", device_name);
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ble.conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected, handle=%d", ble.conn_handle);

            // Request slow + drift-tolerant connection parameters. The BT
            // controller's sleep clock is the internal RTC RC oscillator
            // (~5 % accuracy) so a 7.5 ms phone-default interval plus the
            // default 720 ms supervision timeout drops the link within
            // seconds. Stretching the interval, raising slave latency, and
            // pushing the timeout to ~15 s lets the link ride through RC
            // drift between connection events.
            struct ble_gap_upd_params p = {
                .itvl_min = 80,             // 100 ms (units of 1.25 ms)
                .itvl_max = 160,            // 200 ms
                .latency = 4,               // skip up to 4 events
                .supervision_timeout = 1500,// 15 s (units of 10 ms)
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            int rc = ble_gap_update_params(ble.conn_handle, &p);
            if (rc != 0)
                ESP_LOGW(TAG, "ble_gap_update_params rc=%d", rc);
        }
        else
        {
            ESP_LOGW(TAG, "connect failed status=%d, re-advertising",
                     event->connect.status);
            ble.start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        ble.conn_handle = 0xffff;
        ble.start_advertising();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
    case BLE_GAP_EVENT_MTU:
    case BLE_GAP_EVENT_SUBSCRIBE:
    case BLE_GAP_EVENT_NOTIFY_TX:
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble.start_advertising();
        return 0;
    }
    return 0;
}

void ble_on_sync()
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &ble.own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "infer_auto rc=%d", rc);
        return;
    }

    // Build "Bangle.js xxxx" using last 4 hex of MAC. Gadgetbridge auto-detects
    // anything starting with "Bangle.js".
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    // snprintf(ble.device_name, sizeof(ble.device_name),
    //          "Bangle.js %02x%02x", mac[4], mac[5]);
    snprintf(ble.device_name, sizeof(ble.device_name),
             "G-Watch");
    ble_svc_gap_device_name_set(ble.device_name);

    ble.start_advertising();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BLE::init()
{
    if (rx_queue)
        return; // already inited

    // Line + scratch buffers live in PSRAM. Both are 8 KB; allocating
    // them in internal BSS pushed BT init over the SRAM cliff (BT
    // controller's r_ble_util_buf_rx_alloc would assert at boot).
    s_rx_line = (char *)heap_caps_malloc(RX_LINE_BUF_SIZE,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rx_scratch = (uint8_t *)heap_caps_malloc(RX_SCRATCH_SIZE,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rx_line || !s_rx_scratch)
    {
        ESP_LOGE(TAG, "rx scratch alloc failed");
        return;
    }

    // 32 slots × 244 B/chunk = ~7.8 KB capacity. A single notify-img is
    // ~25 BLE writes at MTU 247; NimBLE can deliver several chunks per
    // connection interval, and the older 8-slot queue overflowed mid-
    // message (xQueueSend with timeout=0 silently dropped chunks),
    // leaving the b64 short and the decode rejecting it as invalid.
    // Use xQueueCreateWithCaps to put the queue storage in PSRAM too —
    // 8 KB of internal heap matters for BT controller init.
    rx_queue = xQueueCreateWithCaps(32, sizeof(RxChunk),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx_queue)
    {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
        return;
    }

    ble_hs_cfg.reset_cb = [](int reason)
    { ESP_LOGW(TAG, "host reset, reason=%d", reason); };
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = nullptr;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Pairing kept simple: no bonding, no MITM. Gadgetbridge talks to
    // Bangle.js without bonding by default.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(nus_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "gatts_count_cfg rc=%d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(nus_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "gatts_add_svcs rc=%d", rc);
        return;
    }

    // Set placeholder name; ble_on_sync() overwrites with MAC-derived name.
    ble_svc_gap_device_name_set("G-Watch");

    nimble_port_freertos_init(ble_host_task);

    xTaskCreatePinnedToCore(ble_rx_task, "ble_rx", 1024 * 6, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "BLE initialised");
}
