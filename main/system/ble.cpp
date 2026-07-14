#include "ble.hpp"
#include "watch.hpp"

#include <sys/time.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

#include "freertos/queue.h"
#include "freertos/idf_additions.h"

#include "cJSON.h"
#include "lvgl.h"
#if LV_USE_SNAPSHOT
#include "draw/snapshot/lv_snapshot.h"
#endif
#include "esp_lvgl_port.h"

#include <atomic>

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

// Notification-image transfer service (custom, alongside NUS).
//   6E500001-B5A3-F393-E0A9-E50E24DCCA9E   service
//   6E500002-...                           Image Data char (phone -> watch, write)
// The phone reassembles BEGIN / DATA*N / END / ABORT frames here; see the
// state machine in ble_image_rx_access below for the wire format.
static const ble_uuid128_t img_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x50, 0x6e);
static const ble_uuid128_t img_data_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x50, 0x6e);

// RX-side incoming-data queue. The NimBLE host calls our access callback on
// its own task; we copy the bytes into a queue so we don't block the host.
struct RxChunk
{
    uint16_t len;
    uint8_t data[244]; // matches ATT_PREFERRED_MTU - 3
};
static QueueHandle_t rx_queue = nullptr;

// Posted from the image-RX state machine (NimBLE host task) to ble_rx_task
// on transfer completion. Ownership of `buffer` (heap_caps_malloc'd in
// PSRAM) transfers along with the message — ble_rx_task copies it into the
// destination (Notification.img or MusicState.album_art) and frees the
// original.
struct ImageInstallMsg
{
    uint8_t kind;            // matches BEGIN image_kind (0x00 icon, 0x01 album art)
    uint32_t correlation_id; // notification id for icons, 0 for album art
    uint16_t width;
    uint16_t height;
    uint32_t length;
    uint8_t *buffer;
};
static QueueHandle_t img_done_queue = nullptr;

// Pending album-art handoff. Written from ble_rx_task and drained by
// the LVGL music_update timer. Uses its own mutex (not lvgl_port_lock)
// so the BLE side can stage new art even while the watch is asleep —
// LVGL is suspended then, and the LVGL port mutex might be held by the
// suspended task, which would deadlock anyone trying to take it.
struct PendingAlbumArt
{
    PsramByteVec pixels;
    uint16_t w;
    uint16_t h;
    bool dirty;
};
static PendingAlbumArt s_pending_album_art;
static SemaphoreHandle_t s_album_art_mux = nullptr;

// Queue set lets ble_rx_task drain both the text rx queue and the
// image-install queue on a single blocking wait. All notifs-vector
// mutation happens on ble_rx_task so there's no concurrent writer.
static QueueSetHandle_t rx_queue_set = nullptr;

// Line buffer for the text-protocol RX task. PSRAM-backed so it
// doesn't tax internal SRAM — BT controller alone takes ~80 KB.
static constexpr size_t RX_LINE_BUF_SIZE = 8192;
static char *s_rx_line = nullptr;

// Set from BLE_GAP_EVENT_DISCONNECT to ask ble_rx_task to clear its line
// buffer and drain rx_queue. Doing the reset on the rx task itself keeps
// the line buffer a single-writer structure (no atomic-lpos gymnastics).
// Without this, a half-message left in the accumulator across a drop
// gets glued onto the first message of the next session — the most
// common cause of "BLE can't receive after a reconnect."
static std::atomic<bool> s_rx_force_reset{false};

// Image-RX state machine state. Mutated only on the NimBLE host task
// (the only thread that runs ble_image_rx_access), so no locking is
// required even across multi-frame transfers.
struct ImageRxState
{
    bool in_progress;
    uint16_t transfer_id;
    uint8_t image_kind;      // BEGIN offset 12 — 0x00 icon, 0x01 album art
    uint32_t correlation_id; // notification id for icons, 0 for album art
    uint16_t width;
    uint16_t height;
    uint16_t chunk_payload_size;
    uint32_t total_bytes;
    uint32_t received_bytes;
    int64_t last_frame_us;
    uint8_t *buffer; // PSRAM, sized to total_bytes
};
static ImageRxState s_img_rx = {};
static constexpr int64_t IMG_RX_TIMEOUT_US = 5'000'000; // 5 s

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
    if (rx_queue && xQueueSend(rx_queue, &chunk, 0) != pdTRUE)
        ESP_LOGW(TAG, "rx queue overflow; dropped %u B", (unsigned)copied);
    return 0;
}

// Standard CRC-32/IEEE 802.3 (PNG / zlib / java.util.zip.CRC32).
// Reflected polynomial 0xEDB88320, init 0xFFFFFFFF, reflect in/out,
// final XOR 0xFFFFFFFF. Implemented byte-at-a-time without a lookup
// table; ~200 µs for a 4.6 KB icon on the S3 at 240 MHz — well below
// the wall-clock of the BLE transfer.
//
// The ROM's esp_rom_crc32_le has the same polynomial but its
// pre/post-XOR convention varies across chip families, so rolling our
// own is the simplest way to guarantee a byte-for-byte match with the
// phone's CRC.
static uint32_t crc32_ieee(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// Reset the in-progress image RX state and free its buffer.
static void img_rx_reset(void)
{
    if (s_img_rx.buffer)
    {
        heap_caps_free(s_img_rx.buffer);
        s_img_rx.buffer = nullptr;
    }
    s_img_rx.in_progress = false;
    s_img_rx.received_bytes = 0;
}

// Notification-image RX state machine. One write = one frame.
//
//   BEGIN  (20 B): 0x01, tid u16, nid u32, w u16, h u16, fmt u8, flags u8,
//                  total_bytes u32, chunk_size u16, reserved u8
//   DATA   (5+N B): 0x02, tid u16, seq u16, payload[N]
//   END    (7 B):  0x03, tid u16, crc32 u32 (CRC-32/IEEE 802.3 over the
//                                            reassembled payload only)
//   ABORT  (3 B):  0x04, tid u16
//
// All multi-byte fields little-endian. Only pixel_format 0x01 (RGB565 LE)
// is supported. On a successful END the buffer is handed off to
// ble_rx_task via img_done_queue so the Notification.img install runs
// on the same task that does the text-protocol writes (single-writer
// invariant on the notifs vector).
int ble_image_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return 0;

    // Stack buffer for one write. Generous upper bound covers MTU up to 517.
    uint8_t buf[520];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0)
        return BLE_ATT_ERR_UNLIKELY;

    if (len < 1)
        return 0;

    int64_t now_us = esp_timer_get_time();
    if (s_img_rx.in_progress &&
        (now_us - s_img_rx.last_frame_us) > IMG_RX_TIMEOUT_US)
    {
        ESP_LOGW(TAG, "img transfer stalled, resetting");
        img_rx_reset();
    }
    s_img_rx.last_frame_us = now_us;

    uint8_t op = buf[0];

    if (op == 0x01) // BEGIN
    {
        if (len != 20)
            return 0;
        uint16_t transfer_id = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        uint32_t correlation = (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) |
                               ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);
        uint16_t width = (uint16_t)buf[7] | ((uint16_t)buf[8] << 8);
        uint16_t height = (uint16_t)buf[9] | ((uint16_t)buf[10] << 8);
        uint8_t pixel_format = buf[11];
        uint8_t image_kind = buf[12];
        uint32_t total = (uint32_t)buf[13] | ((uint32_t)buf[14] << 8) |
                         ((uint32_t)buf[15] << 16) | ((uint32_t)buf[16] << 24);
        uint16_t chunk_size = (uint16_t)buf[17] | ((uint16_t)buf[18] << 8);
        // buf[19] = reserved

        if (pixel_format != 0x01)
        {
            ESP_LOGW(TAG, "img: unsupported pixel_format 0x%02x", pixel_format);
            return 0;
        }
        // Reject kinds we don't know how to route on completion — saves
        // allocating a full PSRAM buffer just to drop it at END.
        if (image_kind != 0x00 && image_kind != 0x01)
        {
            ESP_LOGW(TAG, "img: unsupported image_kind 0x%02x", image_kind);
            return 0;
        }
        if (width == 0 || height == 0 || chunk_size == 0 ||
            total == 0 || total != (uint32_t)width * height * 2)
        {
            ESP_LOGW(TAG, "img: bogus header w=%u h=%u total=%u",
                     width, height, total);
            return 0;
        }

        // BEGIN pre-empts any in-progress transfer.
        img_rx_reset();
        s_img_rx.buffer = (uint8_t *)heap_caps_malloc(total,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_img_rx.buffer)
        {
            ESP_LOGE(TAG, "img: PSRAM alloc %u B failed", total);
            return 0;
        }
        s_img_rx.transfer_id = transfer_id;
        s_img_rx.image_kind = image_kind;
        s_img_rx.correlation_id = correlation;
        s_img_rx.width = width;
        s_img_rx.height = height;
        s_img_rx.chunk_payload_size = chunk_size;
        s_img_rx.total_bytes = total;
        s_img_rx.received_bytes = 0;
        s_img_rx.in_progress = true;
        ESP_LOGI(TAG, "img BEGIN tid=%u kind=0x%02x corr=%" PRIu32 " %ux%u total=%u chunk=%u",
                 transfer_id, image_kind, correlation,
                 width, height, total, chunk_size);
        return 0;
    }

    if (op == 0x02) // DATA
    {
        if (!s_img_rx.in_progress || len < 5)
            return 0;
        uint16_t transfer_id = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        if (transfer_id != s_img_rx.transfer_id)
            return 0; // stale
        uint16_t seq = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
        size_t chunk_len = (size_t)len - 5;
        size_t offset = (size_t)seq * s_img_rx.chunk_payload_size;
        if (offset + chunk_len > s_img_rx.total_bytes)
        {
            ESP_LOGW(TAG, "img DATA out of range seq=%u offset=%u len=%u",
                     seq, (unsigned)offset, (unsigned)chunk_len);
            img_rx_reset();
            return 0;
        }
        memcpy(s_img_rx.buffer + offset, buf + 5, chunk_len);
        s_img_rx.received_bytes += chunk_len;
        return 0;
    }

    if (op == 0x03) // END
    {
        if (!s_img_rx.in_progress || len != 7)
            return 0;
        uint16_t transfer_id = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        if (transfer_id != s_img_rx.transfer_id)
            return 0;
        uint32_t expected_crc = (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) |
                                ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);
        if (s_img_rx.received_bytes != s_img_rx.total_bytes)
        {
            ESP_LOGW(TAG, "img END size mismatch got=%u expected=%u",
                     s_img_rx.received_bytes, s_img_rx.total_bytes);
            img_rx_reset();
            return 0;
        }
        uint32_t actual_crc = crc32_ieee(s_img_rx.buffer, s_img_rx.total_bytes);
        if (actual_crc != expected_crc)
        {
            ESP_LOGW(TAG, "img END CRC mismatch got=0x%08x expected=0x%08x",
                     (unsigned)actual_crc, (unsigned)expected_crc);
            img_rx_reset();
            return 0;
        }

        ImageInstallMsg msg = {
            .kind = s_img_rx.image_kind,
            .correlation_id = s_img_rx.correlation_id,
            .width = s_img_rx.width,
            .height = s_img_rx.height,
            .length = s_img_rx.total_bytes,
            .buffer = s_img_rx.buffer,
        };
        if (img_done_queue && xQueueSend(img_done_queue, &msg, 0) == pdTRUE)
        {
            s_img_rx.buffer = nullptr; // ownership transferred
            s_img_rx.in_progress = false;
            ESP_LOGI(TAG, "img complete kind=0x%02x corr=%" PRIu32 " %ux%u",
                     msg.kind, msg.correlation_id, msg.width, msg.height);
        }
        else
        {
            ESP_LOGW(TAG, "img install queue full; dropping corr=%" PRIu32,
                     msg.correlation_id);
            img_rx_reset();
        }
        return 0;
    }

    if (op == 0x04) // ABORT
    {
        if (!s_img_rx.in_progress || len != 3)
            return 0;
        uint16_t transfer_id = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        if (transfer_id != s_img_rx.transfer_id)
            return 0;
        ESP_LOGI(TAG, "img ABORT tid=%u", transfer_id);
        img_rx_reset();
        return 0;
    }

    return 0;
}

// Standard Battery Service (BAS) so Android's connected-device battery
// widget — the same one that shows headphones / smartwatch levels —
// can read and subscribe to the watch's battery percent. BAS is just
// service 0x180F + Battery Level char 0x2A19, value = one uint8_t [0..100].
static const ble_uuid16_t bas_svc_uuid = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t bas_lvl_uuid = BLE_UUID16_INIT(0x2A19);

static int ble_bas_lvl_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return 0;
    uint8_t lvl = watch.battery.percent;
    if (lvl > 100) lvl = 100; // sentinel UINT8_MAX during pre-init reads
    int rc = os_mbuf_append(ctxt->om, &lvl, sizeof(lvl));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
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
    {
        // Image transfer service (phone -> watch). Custom protocol with
        // BEGIN / DATA / END / ABORT frames; see ble_image_rx_access.
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &img_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &img_data_uuid.u,
                .access_cb = ble_image_rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0},
        },
    },
    {
        // Standard Battery Service so Android's connected-device battery
        // widget picks up the watch's level. Read returns the current
        // watch.battery.percent; notifications are pushed from
        // BLE::send_status whenever the level changes.
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bas_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &bas_lvl_uuid.u,
                .access_cb = ble_bas_lvl_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &ble.bas_lvl_handle,
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

        // Backpressure-tolerant notify. NimBLE's mbuf pool is small and
        // a bulk sender (screenshot, image transfer) easily out-runs the
        // radio's drain rate — mbuf alloc returns NULL or notify returns
        // BLE_HS_ENOMEM/EAGAIN under back-pressure. Retry with a short
        // yield so the host task can drain TX completions; if it still
        // fails after ~150 ms the link is probably broken, give up.
        constexpr int MAX_RETRIES = 15;
        int retries = 0;
        struct os_mbuf *om = nullptr;
        int rc = 0;
        for (;;)
        {
            om = ble_hs_mbuf_from_flat(buf + off, n);
            if (om)
            {
                rc = ble_gatts_notify_custom(conn_handle, tx_attr_handle, om);
                // ble_gatts_notify_custom consumes the mbuf on success
                // and on most error paths; don't double-free.
                if (rc == 0) break;
            }
            else
            {
                rc = -1;
            }
            if (++retries > MAX_RETRIES)
            {
                ESP_LOGW(TAG, "notify gave up after %d retries (rc=%d)", retries, rc);
                ok = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!ok) break;
        off += n;
    }

    free(buf);
    return ok;
}

void BLE::send_status()
{
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"status\",\"bat\":%u,\"chg\":%d,\"volt\":%.3f}",
             (unsigned)watch.battery.percent,
             watch.battery.charging ? 1 : 0,
             (double)watch.battery.voltage / 1000.0);
    send_gb(buf);

    // Mirror to the standard BAS Battery Level characteristic so Android's
    // generic connected-device battery widget tracks it without needing
    // the Gadgetbridge channel. Called whenever battery_task observes a
    // percent or charging change, which is the right cadence for BAS too.
    if (connected() && bas_lvl_handle != 0)
    {
        uint8_t lvl = watch.battery.percent;
        if (lvl > 100) lvl = 100;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&lvl, sizeof(lvl));
        if (om)
            ble_gatts_notify_custom(conn_handle, bas_lvl_handle, om);
    }
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

void BLE::send_notification_reply(uint32_t id, const char *text)
{
    // Build via cJSON so the body field is properly escaped — reply
    // strings can contain quotes, backslashes, and multi-byte UTF-8
    // (the suggestion list often includes things like ":)" and emoji),
    // all of which would corrupt a hand-rolled snprintf format.
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", "notify");
    cJSON_AddNumberToObject(root, "id", (double)id);
    cJSON_AddStringToObject(root, "n", "REPLY");
    cJSON_AddStringToObject(root, "msg", text ? text : "");
    char *s = cJSON_PrintUnformatted(root);
    if (s)
    {
        send_gb(s);
        cJSON_free(s);
    }
    cJSON_Delete(root);
}

void BLE::request_low_power_conn_params()
{
    if (!connected())
        return;
    // 320–400 ms (units of 1.25 ms). Spec requires supervision_timeout >
    // (1 + latency) * interval_max * 2 = 800 ms at these values; 6 s
    // gives the link comfortable headroom over a typical missed-event
    // burst. Latency stays 0 — at this interval the radio is already
    // sleeping most of the time, and using latency would mean a missed
    // event in the worst direction (notification arrives just after a
    // skipped slot) takes (1+latency) * interval before it's serviced.
    struct ble_gap_upd_params p = {
        .itvl_min = 256,            // 320 ms
        .itvl_max = 320,            // 400 ms
        .latency = 0,
        .supervision_timeout = 600, // 6 s
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(conn_handle, &p);
    if (rc != 0)
        ESP_LOGW(TAG, "low-power conn params rc=%d", rc);
}

void BLE::request_normal_conn_params()
{
    if (!connected())
        return;
    // Mirrors the post-connect request in ble_gap_event_handler: fast
    // events for interactive use and image transfer. Re-applied on wake
    // so the link is responsive again by the time the user is looking
    // at the screen.
    struct ble_gap_upd_params p = {
        .itvl_min = 24,             // 30 ms
        .itvl_max = 48,             // 60 ms
        .latency = 0,
        .supervision_timeout = 400, // 4 s
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(conn_handle, &p);
    if (rc != 0)
        ESP_LOGW(TAG, "normal conn params rc=%d", rc);
}

void BLE::send_find_phone(bool on)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"findPhone\",\"n\":%s}", on ? "true" : "false");
    send_gb(buf);
}

// Capture lv_screen_active() and stream it to the phone as a 24-bit BMP
// file via Gadgetbridge's chunked file-write protocol. Each chunk is
//   {"t":"file","n":"<name>","part":"first|next|last","d":"<base64>"}
// with "first" truncating any prior file by that name and "last"
// signalling the final chunk. The Gadgetbridge fork's BangleJS message
// handler is expected to concatenate and save the decoded bytes to a
// watched directory — that's the side this protocol mates with.
//
// Memory: a 240×240 24-bit BMP is ~173 KB, allocated in PSRAM. The LVGL
// snapshot is rendered straight into the pixel-data region of the BMP
// (after we lay out the header), so we don't keep two big buffers
// alive at the same time.
//
// Wall-clock: at MTU 247 + 30 ms conn interval, transfer takes roughly
// 8–15 s. Blocks the rx task for the duration — fine for a one-shot
// user-triggered command, not OK for periodic use.
void BLE::send_screenshot()
{
#if !LV_USE_SNAPSHOT
    // LV_USE_SNAPSHOT is off (BT controller can't afford the extra IRAM
    // footprint on this build — re-enabling caused ble_init malloc-fail
    // panics). Log and bail so a misfired {t:"screenshot"} command from
    // the phone doesn't look like silent failure.
    ESP_LOGW(TAG, "screenshot requested but LV_USE_SNAPSHOT is disabled");
    return;
#else
    if (!connected())
        return;

    // 240×240 fits on this build; if you change the panel size the BMP
    // header field sizes don't need to change — they're computed below.
    lv_display_t *disp = lv_display_get_default();
    int32_t w = disp ? lv_display_get_horizontal_resolution(disp) : 240;
    int32_t h = disp ? lv_display_get_vertical_resolution(disp) : 240;

    // BMP layout: 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER
    // + 24-bit BGR pixel rows, each row padded up to a 4-byte multiple,
    // stored bottom-up (positive height field).
    const uint32_t header_size = 14 + 40;
    const uint32_t row_bytes = ((w * 3 + 3) / 4) * 4;
    const uint32_t pixel_bytes = row_bytes * h;
    const uint32_t file_size = header_size + pixel_bytes;

    uint8_t *bmp = (uint8_t *)heap_caps_malloc(
        file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bmp)
    {
        ESP_LOGE(TAG, "screenshot bmp alloc %u B failed", (unsigned)file_size);
        return;
    }
    // Heap may return uninitialised memory; the row-padding bytes need
    // to be zero, and zeroing the whole pixel area means a partial
    // snapshot still produces a sane image.
    memset(bmp, 0, file_size);

    // Headers (all little-endian on the wire and on this chip, so we
    // can just write 16/32-bit fields with byte stores or aligned writes).
    bmp[0] = 'B'; bmp[1] = 'M';
    *(uint32_t *)(bmp + 2)  = file_size;
    *(uint32_t *)(bmp + 6)  = 0;            // reserved (2x u16)
    *(uint32_t *)(bmp + 10) = header_size;  // pixel offset
    *(uint32_t *)(bmp + 14) = 40;           // info header size
    *(int32_t  *)(bmp + 18) = w;
    *(int32_t  *)(bmp + 22) = h;            // positive = bottom-up rows
    *(uint16_t *)(bmp + 26) = 1;            // planes
    *(uint16_t *)(bmp + 28) = 24;           // bpp
    *(uint32_t *)(bmp + 30) = 0;            // BI_RGB
    *(uint32_t *)(bmp + 34) = pixel_bytes;
    *(int32_t  *)(bmp + 38) = 2835;         // ~72 DPI horizontal
    *(int32_t  *)(bmp + 42) = 2835;         // ~72 DPI vertical
    *(uint32_t *)(bmp + 46) = 0;
    *(uint32_t *)(bmp + 50) = 0;

    // Render the screen into a temporary RGB565 buffer in PSRAM and
    // then convert row-by-row into the BMP's bottom-up BGR888 layout.
    // lv_snapshot_take_to_buf is the buffer-borrowing variant of
    // lv_snapshot_take, which avoids a 115 KB internal-heap allocation
    // that wouldn't fit.
    const uint32_t snap_bytes = w * h * 2;  // RGB565
    uint8_t *snap = (uint8_t *)heap_caps_malloc(
        snap_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snap)
    {
        ESP_LOGE(TAG, "screenshot snap alloc %u B failed", (unsigned)snap_bytes);
        heap_caps_free(bmp);
        return;
    }

    // lv_snapshot_take_to_draw_buf is the non-deprecated API (the older
    // lv_snapshot_take_to_buf prints a runtime warning). It wants an
    // lv_draw_buf_t wrapper around our raw buffer — lv_draw_buf_init
    // populates the header in-place without allocating, so the pixel
    // bytes still come from PSRAM.
    lv_draw_buf_t dbuf = {};
    bool snap_ok = false;
    if (lvgl_port_lock(0))
    {
        lv_obj_t *target = lv_screen_active();
        if (target &&
            lv_draw_buf_init(&dbuf, w, h, LV_COLOR_FORMAT_RGB565,
                             /*stride auto*/ 0, snap, snap_bytes) == LV_RESULT_OK)
        {
            lv_result_t rc = lv_snapshot_take_to_draw_buf(
                target, LV_COLOR_FORMAT_RGB565, &dbuf);
            snap_ok = (rc == LV_RESULT_OK);
        }
        lvgl_port_unlock();
    }
    if (!snap_ok)
    {
        ESP_LOGE(TAG, "lv_snapshot_take_to_draw_buf failed");
        heap_caps_free(snap);
        heap_caps_free(bmp);
        return;
    }

    // RGB565 (LE, R5-G6-B5) → BGR888, written into BMP rows bottom-up.
    // The (r5<<3)|(r5>>2) form fills the low bits so 0x1F → 0xFF rather
    // than 0xF8, preserving full-range whites/blacks across the
    // 5→8-bit expansion.
    for (int32_t y = 0; y < h; y++)
    {
        uint8_t *row = bmp + header_size + (h - 1 - y) * row_bytes;
        const uint16_t *src_row = (const uint16_t *)(snap + y * w * 2);
        for (int32_t x = 0; x < w; x++)
        {
            uint16_t p = src_row[x];
            uint8_t r5 = (p >> 11) & 0x1F;
            uint8_t g6 = (p >> 5)  & 0x3F;
            uint8_t b5 =  p        & 0x1F;
            row[x * 3 + 0] = (b5 << 3) | (b5 >> 2);
            row[x * 3 + 1] = (g6 << 2) | (g6 >> 4);
            row[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
        }
    }
    heap_caps_free(snap);

    // Stream as Gadgetbridge {t:"file"} chunked writes. Field shapes
    // per the BangleJS file-write API:
    //   n: filename
    //   m: "w" on the first chunk (truncate), "a" on the rest (append)
    //   c: contents as a string — raw bytes encoded with Espruino's
    //      \xNN convention so the string parses back to the original
    //      binary on the GB side. Printable ASCII (except " and \) is
    //      sent unescaped to keep the payload smaller; everything else
    //      is \xNN (4 chars per byte). The watch uses the same escape
    //      convention on inbound messages (see fix_js_x_escapes), so
    //      the GB fork already understands it.
    //
    // CHUNK_RAW is tuned so the worst-case-escaped payload + JSON
    // wrapper stays under ~1.1 KB — well within Gadgetbridge's RX line
    // buffer on the BangleJS side.
    constexpr size_t CHUNK_RAW = 256;            // 256 * 4 = 1024 worst-case
    constexpr size_t ESC_CAP   = CHUNK_RAW * 4 + 8;
    constexpr size_t JSON_CAP  = ESC_CAP + 96;
    char *esc = (char *)heap_caps_malloc(ESC_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *jsn = (char *)heap_caps_malloc(JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!esc || !jsn)
    {
        ESP_LOGE(TAG, "screenshot chunk buf alloc failed");
        heap_caps_free(esc);
        heap_caps_free(jsn);
        heap_caps_free(bmp);
        return;
    }

    char filename[40];
    uint64_t ts = (uint64_t)(esp_timer_get_time() / 1000);
    snprintf(filename, sizeof(filename), "screenshot_%llu.bmp",
             (unsigned long long)ts);

    ESP_LOGI(TAG, "screenshot %s: %u B in %ux%u",
             filename, (unsigned)file_size, (unsigned)w, (unsigned)h);

    static const char hex[] = "0123456789abcdef";
    size_t offset = 0;
    while (offset < file_size)
    {
        size_t chunk = (file_size - offset > CHUNK_RAW)
                           ? CHUNK_RAW
                           : (file_size - offset);

        // Escape bytes into `esc`. Keep printable ASCII unescaped except
        // " (would close the string) and \ (escape lead-in); everything
        // else is \xNN.
        size_t epos = 0;
        const uint8_t *src = bmp + offset;
        for (size_t i = 0; i < chunk; i++)
        {
            uint8_t b = src[i];
            if (b >= 0x20 && b <= 0x7E && b != '"' && b != '\\')
            {
                esc[epos++] = (char)b;
            }
            else
            {
                esc[epos++] = '\\';
                esc[epos++] = 'x';
                esc[epos++] = hex[b >> 4];
                esc[epos++] = hex[b & 0x0F];
            }
        }
        esc[epos] = 0;

        const char *mode = (offset == 0) ? "w" : "a";
        snprintf(jsn, JSON_CAP,
                 "{\"t\":\"file\",\"n\":\"%s\",\"m\":\"%s\",\"c\":\"%s\"}",
                 filename, mode, esc);
        if (!send_gb(jsn))
        {
            ESP_LOGW(TAG, "screenshot send_gb failed at offset %u",
                     (unsigned)offset);
            break;
        }
        offset += chunk;

        // One chunk = one full JSON message = ~5 sub-notifies after
        // send_gb's MTU split. The negotiated conn interval is 30 ms
        // and the controller drains 5–10 packets per event, so each
        // chunk consumes roughly one event's worth of bandwidth. Wait
        // ~30 ms between chunks so we don't out-run the radio and
        // exhaust NimBLE's mbuf pool (the source of the rc=-1 retries).
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    heap_caps_free(esc);
    heap_caps_free(jsn);
    heap_caps_free(bmp);
#endif // LV_USE_SNAPSHOT
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

void BLE::post_pending_album_art(PsramByteVec &&pixels, uint16_t w, uint16_t h)
{
    if (!s_album_art_mux)
        return;
    xSemaphoreTake(s_album_art_mux, portMAX_DELAY);
    s_pending_album_art.pixels = std::move(pixels);
    s_pending_album_art.w = w;
    s_pending_album_art.h = h;
    s_pending_album_art.dirty = true;
    xSemaphoreGive(s_album_art_mux);
}

bool BLE::promote_pending_album_art()
{
    if (!s_album_art_mux)
        return false;
    bool changed = false;
    if (xSemaphoreTake(s_album_art_mux, 0) == pdTRUE)
    {
        if (s_pending_album_art.dirty)
        {
            // Promote pending into the live music_state. Called from
            // the LVGL task with the LVGL lock held implicitly, so
            // the subsequent descriptor rebind in music_update is
            // atomic with this swap from a render's point of view.
            music_state.album_art = std::move(s_pending_album_art.pixels);
            music_state.album_art_w = s_pending_album_art.w;
            music_state.album_art_h = s_pending_album_art.h;
            s_pending_album_art.pixels = PsramByteVec{};
            s_pending_album_art.w = 0;
            s_pending_album_art.h = 0;
            s_pending_album_art.dirty = false;
            changed = true;
        }
        xSemaphoreGive(s_album_art_mux);
    }
    return changed;
}

void BLE::clear_notifications()
{
    if (notifs.empty())
        return;
    notifs.clear();
    notifs_version++;
}

// ---------- Incoming command parsing ----------

// Collapse Unicode whitespace variants to a plain ASCII space, and drop
// zero-width / direction-mark control characters entirely. Product Sans
// (and most TTFs) only has a glyph for U+0020, so any other space-like
// codepoint that survives into LVGL renders as the missing-glyph box.
// Doing the rewrite at the BLE ingest layer means every label
// downstream (notification labels, popup, music info) is clean without
// needing to repeat the substitution per UI surface.
//
// The mapping is UTF-8 byte-pattern based:
//   C2 A0                          U+00A0 NO-BREAK SPACE          → ' '
//   E2 80 80..8A                   U+2000..U+200A (en/em/thin/etc.) → ' '
//   E2 80 8B..8F                   U+200B..U+200F (zero-width / LRM/RLM) → dropped
//   E2 80 AF                       U+202F NARROW NO-BREAK SPACE   → ' '
//   E2 81 9F                       U+205F MEDIUM MATHEMATICAL SPACE → ' '
//   E3 80 80                       U+3000 IDEOGRAPHIC SPACE       → ' '
//   EF BB BF                       U+FEFF ZERO WIDTH NO-BREAK SPACE (BOM) → dropped
static std::string sanitize_text(const char *s)
{
    std::string out;
    if (!s)
        return out;
    size_t n = strlen(s);
    out.reserve(n);
    const uint8_t *p = (const uint8_t *)s;
    size_t i = 0;
    while (i < n)
    {
        uint8_t c = p[i];
        if (i + 1 < n && c == 0xC2 && p[i + 1] == 0xA0)
        {
            out += ' ';
            i += 2;
            continue;
        }
        if (i + 2 < n && c == 0xE2 && p[i + 1] == 0x80)
        {
            uint8_t lo = p[i + 2];
            if (lo >= 0x80 && lo <= 0x8A)
            {
                out += ' ';
                i += 3;
                continue;
            }
            if (lo >= 0x8B && lo <= 0x8F)
            {
                i += 3;
                continue;
            }
            if (lo == 0xAF)
            {
                out += ' ';
                i += 3;
                continue;
            }
        }
        if (i + 2 < n && c == 0xE2 && p[i + 1] == 0x81 && p[i + 2] == 0x9F)
        {
            out += ' ';
            i += 3;
            continue;
        }
        if (i + 2 < n && c == 0xE3 && p[i + 1] == 0x80 && p[i + 2] == 0x80)
        {
            out += ' ';
            i += 3;
            continue;
        }
        if (i + 2 < n && c == 0xEF && p[i + 1] == 0xBB && p[i + 2] == 0xBF)
        {
            i += 3;
            continue;
        }
        out += (char)c;
        i++;
    }
    return out;
}

// Optional helper: pull a child field as std::string, sanitised. Missing → empty.
static std::string json_str(const cJSON *obj, const char *key)
{
    cJSON *f = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(f) && f->valuestring)
        return sanitize_text(f->valuestring);
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
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
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

    ESP_LOGI(TAG, "GB msg: %s", json);

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
        cJSON *reply = cJSON_GetObjectItemCaseSensitive(root, "reply");
        n.reply = cJSON_IsTrue(reply);

        if (n.reply)
        {
            cJSON *replies = cJSON_GetObjectItemCaseSensitive(root, "suggestions");
            if (cJSON_IsArray(replies))
            {
                cJSON *item = NULL;

                // 3. Iterate through each element in the array
                cJSON_ArrayForEach(item, replies)
                {
                    // 4. Verify the element is a string before accessing it
                    if (cJSON_IsString(item) && (item->valuestring != NULL))
                    {
                        n.replies.push_back(item->valuestring);
                    }
                }
            }
        }

        ESP_LOGI(TAG, "notify: id=%" PRIu32 " src='%s' title='%s' body='%s' reply=%s",
                 n.id, n.src.c_str(), n.title.c_str(), n.body.c_str(), n.reply ? "true" : "false");

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
    // Notification images now arrive on the dedicated image GATT
    // characteristic (ble_image_rx_access); the old "notify-img" JSON
    // case was removed when that wire format took over.
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
        music_state.last_msg_ms = esp_timer_get_time() / 1000;
    }
    else if (strcmp(t->valuestring, "musicinfo") == 0)
    {
        music_state.artist = json_str(root, "artist");
        music_state.album = json_str(root, "album");
        music_state.track = json_str(root, "track");
        cJSON *dur = cJSON_GetObjectItemCaseSensitive(root, "dur");
        if (cJSON_IsNumber(dur))
            music_state.duration_s = (int32_t)dur->valuedouble;
        music_state.last_msg_ms = esp_timer_get_time() / 1000;
    }
    else if (strcmp(t->valuestring, "weather") == 0)
    {
        // {"t":"weather","v":1,"temp":290,"hi":297,"lo":285,"hum":73,
        //  "rain":4,"uv":1,"code":800,"txt":"Clear Sky","wind":4.67,
        //  "wdir":321,"loc":"My Location"}
        // Numeric fields are missing on some senders — guard each lookup
        // and only overwrite when the field is actually present.
        auto num_i = [&](const char *k, int32_t def) -> int32_t {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(root, k);
            return cJSON_IsNumber(n) ? (int32_t)n->valuedouble : def;
        };
        auto num_f = [&](const char *k, float def) -> float {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(root, k);
            return cJSON_IsNumber(n) ? (float)n->valuedouble : def;
        };

        weather_state.txt       = json_str(root, "txt");
        weather_state.loc       = json_str(root, "loc");
        weather_state.temp_k    = num_i("temp", weather_state.temp_k);
        weather_state.hi_k      = num_i("hi",   weather_state.hi_k);
        weather_state.lo_k      = num_i("lo",   weather_state.lo_k);
        weather_state.humidity  = (uint8_t)num_i("hum", weather_state.humidity);
        weather_state.uv        = (uint8_t)num_i("uv",  weather_state.uv);
        weather_state.code      = (uint16_t)num_i("code", weather_state.code);
        weather_state.wind_mps  = num_f("wind", weather_state.wind_mps);
        weather_state.wind_dir  = (uint16_t)num_i("wdir", weather_state.wind_dir);
        weather_state.version++;

        ESP_LOGI(TAG, "weather: %s @ %s code=%u temp=%dK hum=%u uv=%u wind=%.1fm/s@%u°",
                 weather_state.txt.c_str(), weather_state.loc.c_str(),
                 (unsigned)weather_state.code,
                 (int)weather_state.temp_k,
                 (unsigned)weather_state.humidity, (unsigned)weather_state.uv,
                 weather_state.wind_mps, (unsigned)weather_state.wind_dir);
    }
    else if (strcmp(t->valuestring, "screenshot") == 0)
    {
        // No point grabbing a snapshot while the LVGL task is suspended
        // (Watch::sleep stops it) — the panel is dark anyway and the
        // last rendered frame may be partial. Just ignore the request.
        if (!watch.sleeping)
            send_screenshot();
    }
    else if (strcmp(t->valuestring, "is_gps_active") == 0)
    {
        send_gb("{t:\"gps_power\", status: false}");
    }
    else if (strcmp(t->valuestring, "reboot") == 0)
    {
        esp_restart();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to handle GB message\n");
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

// ---------- ble_rx_task: text + image consumer ----------

// Blocks on a queue set covering both rx_queue (text-protocol bytes from
// the NUS RX char) and img_done_queue (completed image transfers from
// the image GATT state machine). Keeping everything on one task is the
// reason the image RX state machine in ble_image_rx_access doesn't
// touch the notifs vector itself — it hands the buffer off here so the
// install runs on the same task that does push_notification(), which
// keeps the single-writer invariant on `notifs` intact.
void ble_rx_task(void *arg)
{
    char *line = s_rx_line;
    size_t lpos = 0;

    while (true)
    {
        // Disconnect-triggered reset. Drains the rx queue and clears
        // the line accumulator so a half-message from before the drop
        // doesn't poison the first message of the next session. The
        // 500 ms select timeout below is what lets this fire promptly
        // when no traffic is in flight.
        if (s_rx_force_reset.exchange(false, std::memory_order_acq_rel))
        {
            lpos = 0;
            RxChunk discard;
            while (rx_queue && xQueueReceive(rx_queue, &discard, 0) == pdTRUE) {}
            img_rx_reset();
        }

        QueueSetMemberHandle_t activated =
            xQueueSelectFromSet(rx_queue_set, pdMS_TO_TICKS(500));
        if (!activated)
            continue;

        if (activated == (QueueSetMemberHandle_t)rx_queue)
        {
            RxChunk chunk;
            if (xQueueReceive(rx_queue, &chunk, 0) != pdTRUE)
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
        else if (activated == (QueueSetMemberHandle_t)img_done_queue)
        {
            ImageInstallMsg msg;
            if (xQueueReceive(img_done_queue, &msg, 0) != pdTRUE)
                continue;

            // Copy from the raw PSRAM buffer into a PsramByteVec the
            // destination structure owns. Then route based on image_kind.
            // The std::move into the destination vector frees the
            // previous album-art / icon buffer via the allocator, so
            // memory doesn't accumulate across image updates.
            //
            // For album art specifically: it lives in MusicState and
            // the music screen's lv_image_dsc_t points directly at the
            // buffer, so the move could free pixels mid-render. Take
            // lvgl_port_lock around that branch so LVGL can't be in
            // the middle of drawing while we swap the vector.
            // Notification icons use a copy-out path (UI rebuilds the
            // card list from the version counter) so they don't need
            // the same protection.
            PsramByteVec pixels(msg.buffer, msg.buffer + msg.length);
            heap_caps_free(msg.buffer);
            switch (msg.kind)
            {
            case 0x00: // notification icon
                attach_notification_image(msg.correlation_id, std::move(pixels),
                                          msg.width, msg.height);
                break;
            case 0x01: // album art (correlation_id ignored per spec)
                // Stage the new image in the pending slot under our
                // own mutex; the LVGL music_update timer promotes it
                // to music_state on its next tick. Doing the install
                // there means we can update music data while the watch
                // is asleep (LVGL task is suspended, but ble_rx_task
                // keeps running) — the live install runs at wake.
                ble.post_pending_album_art(std::move(pixels),
                                           msg.width, msg.height);
                break;
            default:
                ESP_LOGW(TAG, "img install: unknown kind 0x%02x dropped",
                         msg.kind);
                break;
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

    // Appearance = Smartwatch (BT SIG category 0x00C2). Tells Android to
    // surface this as a watch in the Connected Devices list — picks the
    // watch icon and slots into the same battery-widget code path that
    // headphones and smartwatches use.
    fields.appearance = 0x00C2;
    fields.appearance_is_present = 1;

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

            // Request faster, drift-tolerant connection parameters.
            // Earlier this used 100–200 ms interval + slave latency 4
            // to save power; that strangled the notification-image
            // and album-art transfers — 4 KB at MTU 244 should take
            // <1 s but was taking 5–10 s because every connection
            // event only carries a handful of writes. Dropping the
            // interval to 24–48 ms (30–60 ms) with no latency gives
            // ~5× more events per second so the phone can stream
            // image chunks through quickly. Supervision timeout
            // stays generous since the controller's main XTAL is the
            // sleep clock now (BT_CTRL_LPCLK_SEL_MAIN_XTAL) and
            // doesn't have the RC-oscillator drift problem the
            // previous params worked around.
            struct ble_gap_upd_params p = {
                .itvl_min = 24,             // 30 ms (units of 1.25 ms)
                .itvl_max = 48,             // 60 ms
                .latency = 0,               // don't skip events
                .supervision_timeout = 400, // 4 s (units of 10 ms)
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
        // Ask ble_rx_task to drop any half-assembled line + drained
        // queue + in-progress image transfer. Without this, the first
        // message of the next session can be glued onto whatever bytes
        // were in flight when the link dropped (very common cause of
        // "BLE can't receive after a reconnect"). img_rx_reset is
        // safe to call multiple times.
        s_rx_force_reset.store(true, std::memory_order_release);
        ble.start_advertising();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
    {
        // Log the params the link actually settled on. If the phone
        // pushed the interval back up to something slow we'll see it
        // here rather than scratching our heads at the transfer rate.
        struct ble_gap_conn_desc d = {};
        if (ble_gap_conn_find(event->conn_update.conn_handle, &d) == 0)
            ESP_LOGI(TAG, "conn_update itvl=%u (×1.25ms) latency=%u timeout=%u (×10ms)",
                     d.conn_itvl, d.conn_latency, d.supervision_timeout);
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        // The image GATT char's effective chunk size is mtu - 5 (frame
        // header) - 3 (att opcode + handle). A small MTU here is the
        // most likely cause of slow image transfers.
        ESP_LOGI(TAG, "mtu negotiated: %u (image chunk = %u B)",
                 event->mtu.value,
                 (event->mtu.value > 8u) ? (event->mtu.value - 8u) : 0u);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        // When the central subscribes to our TX characteristic, push a
        // fresh battery snapshot immediately. send_status's per-percent
        // dedupe in battery_task means the phone otherwise wouldn't see
        // a value until the next percent transition — which can be
        // minutes after a reconnect, leaving Gadgetbridge displaying
        // stale or unknown battery in the meantime.
        if (event->subscribe.attr_handle == ble.tx_attr_handle &&
            event->subscribe.cur_notify)
        {
            ble.send_status();
        }
        return 0;
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

    // Line buffer lives in PSRAM — allocating it in internal BSS pushed
    // BT init over the SRAM cliff (controller's r_ble_util_buf_rx_alloc
    // would assert at boot).
    s_rx_line = (char *)heap_caps_malloc(RX_LINE_BUF_SIZE,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rx_line)
    {
        ESP_LOGE(TAG, "rx line alloc failed");
        return;
    }

    // Text-protocol queue (NUS RX writes). 32 slots × 244 B/chunk ≈ 8 KB,
    // backed by PSRAM so it doesn't eat internal heap.
    rx_queue = xQueueCreateWithCaps(32, sizeof(RxChunk),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx_queue)
    {
        ESP_LOGE(TAG, "rx_queue alloc failed");
        return;
    }

    // Completed-image queue. Small — one ImageInstallMsg per image, the
    // RX state machine only finalises one at a time.
    // Pending album-art slot's mutex. Lightweight FreeRTOS mutex,
    // independent of lvgl_port_lock so the BLE side can stage new
    // album art while LVGL is suspended during light sleep.
    s_album_art_mux = xSemaphoreCreateMutex();
    if (!s_album_art_mux)
    {
        ESP_LOGE(TAG, "album_art_mux alloc failed");
        return;
    }

    img_done_queue = xQueueCreateWithCaps(4, sizeof(ImageInstallMsg),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!img_done_queue)
    {
        ESP_LOGE(TAG, "img_done_queue alloc failed");
        return;
    }

    // Queue set lets ble_rx_task block on both queues simultaneously
    // (rather than polling) — keeps the single-writer invariant on the
    // notifs vector since one task drains text and image installs.
    rx_queue_set = xQueueCreateSet((UBaseType_t)(32 + 4));
    if (!rx_queue_set ||
        xQueueAddToSet(rx_queue, rx_queue_set) != pdPASS ||
        xQueueAddToSet(img_done_queue, rx_queue_set) != pdPASS)
    {
        ESP_LOGE(TAG, "queue set setup failed");
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

    // Smartwatch appearance on the GAP service (0x1800 / 0x2A01) — read
    // by hosts that pick up appearance from the GATT GAP service rather
    // than the advertisement, including some Android paths that decide
    // the device's icon and category after pairing.
    ble_svc_gap_device_appearance_set(0x00C2);

    nimble_port_freertos_init(ble_host_task);

    // Stack lives in PSRAM — rx_task does no DMA or ISR work (just
    // blocking queue reads + JSON parse + std::vector mutation), and
    // internal SRAM is too tight on this build for a 6 KB contiguous
    // chunk after the BT controller and LVGL have taken their share.
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        ble_rx_task, "ble_rx", 1024 * 6, NULL, 5, NULL, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS)
        ESP_LOGE(TAG, "ble_rx_task creation failed (rc=%d); BLE RX is dead",
                 (int)ok);

    ESP_LOGI(TAG, "BLE initialised");
}
