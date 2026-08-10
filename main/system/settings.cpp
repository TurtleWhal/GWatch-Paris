#include "watch.hpp"

#include <cJSON.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef ESP_PLATFORM
#include "esp_spiffs.h"
#endif

static const char *TAG_SETTINGS = "settings";

// Parsed config.json tree, held in memory for the life of the process.
// nullptr if config.json is missing / unparseable — in that case the
// JSON getters all return their supplied defaults and setters lazy-
// create the object. Guarded by g_json_mutex so a BLE-task writer
// can't tear the tree out from under an LVGL-task reader (settings
// screen builds run on the LVGL task; future cfg-set commands will
// run on the BLE-rx task).
static cJSON     *g_config = nullptr;
static std::mutex g_json_mutex;

static cJSON *section_get(const char *section)
{
    if (!g_config) return nullptr;
    cJSON *s = cJSON_GetObjectItemCaseSensitive(g_config, section);
    return (cJSON_IsObject(s)) ? s : nullptr;
}

// Get-or-create the named section object on the root of g_config.
// Called from the setters when the user is writing a key into a
// section that doesn't exist yet in config.json. Returns nullptr only
// if g_config itself was never parsed (config.json missing entirely).
static cJSON *section_get_or_create(const char *section)
{
    if (!g_config) return nullptr;
    cJSON *s = cJSON_GetObjectItemCaseSensitive(g_config, section);
    if (cJSON_IsObject(s)) return s;
    // Wrong type or missing — replace with a fresh object.
    if (s) cJSON_DeleteItemFromObjectCaseSensitive(g_config, section);
    cJSON *fresh = cJSON_CreateObject();
    if (!fresh) return nullptr;
    cJSON_AddItemToObject(g_config, section, fresh);
    return fresh;
}

// Serialise g_config back to disk. Called after every setter — cheap
// for a config in the single-KB range, and losing an update on
// crash would be worse than the extra write. Writes to a .tmp file
// first and renames, so a mid-write reset can't leave a truncated
// config.json.
static void save_config_locked()
{
    if (!g_config) return;
    char *out = cJSON_Print(g_config);
    if (!out) return;

    std::string tmp_path = std::string(GWATCH_CONFIG_PATH) + ".tmp";
    FILE *f = fopen(tmp_path.c_str(), "w");
    if (!f) {
        ESP_LOGE(TAG_SETTINGS, "open %s for write failed", tmp_path.c_str());
        cJSON_free(out);
        return;
    }
    fputs(out, f);
    fclose(f);
    cJSON_free(out);

    // SPIFFS's rename() fails outright if the destination file exists
    // (unlike POSIX where it atomically overwrites). Remove the
    // destination first so the rename lands into a free slot. This
    // costs the last-moment atomicity — there's a brief window where
    // neither name resolves — but SPIFFS wasn't giving us POSIX
    // rename guarantees to begin with, and the tmp file survives if
    // we're interrupted between the remove and the rename, so a
    // subsequent boot can still recover the previous value from
    // config.json.tmp if load_config were extended to check for it.
    remove(GWATCH_CONFIG_PATH);
    if (rename(tmp_path.c_str(), GWATCH_CONFIG_PATH) != 0) {
        ESP_LOGE(TAG_SETTINGS, "rename %s -> %s failed",
                 tmp_path.c_str(), GWATCH_CONFIG_PATH);
        remove(tmp_path.c_str());
    }
}

// Read the entire config.json into memory, parse to cJSON, store as
// g_config. Called once from Settings::init after SPIFFS is mounted.
// A missing / unparseable file leaves g_config as nullptr and the
// getters fall back to their defaults — a fresh SPIFFS image after a
// full erase will have this state briefly until the first setter
// writes a file back.
static void load_config()
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    if (g_config) {
        cJSON_Delete(g_config);
        g_config = nullptr;
    }

    FILE *f = fopen(GWATCH_CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG_SETTINGS, "config.json missing — using defaults");
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 128 * 1024) {
        ESP_LOGE(TAG_SETTINGS, "config.json size out of range: %ld", sz);
        fclose(f);
        return;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);

    // require_null_terminated=1 so a truncated / headless file (e.g.
    // one left behind by a mid-transfer BLE post that we mistakenly
    // committed) gets rejected — cJSON_Parse without this happily
    // returns success on the first parseable token and ignores any
    // trailing garbage.
    g_config = cJSON_ParseWithOpts(buf, NULL, 1);
    free(buf);
    if (!g_config)
        ESP_LOGE(TAG_SETTINGS, "config.json parse failed");
    else
        ESP_LOGI(TAG_SETTINGS, "config.json loaded (%ld B)", sz);
}

void Settings::init()
{
#ifdef ESP_PLATFORM
    // NVS flash init + auto-erase on version/page mismatch. Both the
    // error codes and the underlying flash are ESP-only; the sim's
    // shim nvs_* stubs need no init.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Mount the "storage" SPIFFS partition. format_if_mount_failed
    // gives us an empty filesystem on a virgin device rather than a
    // boot loop; the config.json baked into the image by
    // spiffs_create_partition_image lands here on a normal flash
    // cycle, and after a full-erase the missing-file branch of
    // load_config() kicks in with defaults until the first setter.
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = GWATCH_CONFIG_MOUNT,
        .partition_label        = "storage",
        .max_files              = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t spi_err = esp_vfs_spiffs_register(&spiffs_conf);
    if (spi_err != ESP_OK)
        ESP_LOGE(TAG_SETTINGS, "spiffs mount failed: %s", esp_err_to_name(spi_err));
#endif

    load_config();
}

void Settings::reload()
{
    // Just re-run load_config — SPIFFS is already mounted, NVS init
    // was one-shot. load_config takes the g_json_mutex and swaps the
    // parsed tree atomically, so readers on other tasks see either
    // the old tree or the new tree, never a torn read.
    load_config();
}

// -- Legacy NVS API (unchanged) --

void Settings::writeUint8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    nvs_open("settings", NVS_READWRITE, &handle);
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

uint8_t Settings::readUint8(const char *key, uint8_t defaultValue)
{
    nvs_handle_t handle;
    nvs_open("settings", NVS_READONLY, &handle);
    uint8_t value = defaultValue;
    nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    return value;
}

void Settings::writeUint16(const char *key, uint16_t value)
{
    nvs_handle_t handle;
    nvs_open("settings", NVS_READWRITE, &handle);
    nvs_set_u16(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

uint16_t Settings::readUint16(const char *key, uint16_t defaultValue)
{
    nvs_handle_t handle;
    nvs_open("settings", NVS_READONLY, &handle);
    uint16_t value = defaultValue;
    nvs_get_u16(handle, key, &value);
    nvs_close(handle);
    return value;
}

// -- JSON-backed API --

std::string Settings::readString(const char *section, const char *key,
                                 const char *default_val)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get(section);
    if (s) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(s, key);
        if (cJSON_IsString(v) && v->valuestring)
            return std::string(v->valuestring);
    }
    return std::string(default_val ? default_val : "");
}

void Settings::writeString(const char *section, const char *key, const char *value)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get_or_create(section);
    if (!s) return;
    cJSON *item = cJSON_CreateString(value ? value : "");
    if (!item) return;
    // cJSON_ReplaceItemInObject / AddItemToObject — use
    // ReplaceItemInObjectCaseSensitive which handles both cases:
    // it replaces if present, does nothing if absent. So we
    // pre-delete then add to guarantee "upsert" semantics.
    cJSON_DeleteItemFromObjectCaseSensitive(s, key);
    cJSON_AddItemToObject(s, key, item);
    save_config_locked();
}

bool Settings::readBool(const char *section, const char *key, bool default_val)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get(section);
    if (s) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(s, key);
        if (cJSON_IsBool(v))
            return cJSON_IsTrue(v);
        if (cJSON_IsNumber(v))
            return v->valuedouble != 0;
    }
    return default_val;
}

void Settings::writeBool(const char *section, const char *key, bool value)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get_or_create(section);
    if (!s) return;
    cJSON *item = cJSON_CreateBool(value);
    if (!item) return;
    cJSON_DeleteItemFromObjectCaseSensitive(s, key);
    cJSON_AddItemToObject(s, key, item);
    save_config_locked();
}

int Settings::readInt(const char *section, const char *key, int default_val)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get(section);
    if (s) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(s, key);
        if (cJSON_IsNumber(v))
            return (int)v->valuedouble;
    }
    return default_val;
}

void Settings::writeInt(const char *section, const char *key, int value)
{
    std::lock_guard<std::mutex> lk(g_json_mutex);
    cJSON *s = section_get_or_create(section);
    if (!s) return;
    cJSON *item = cJSON_CreateNumber((double)value);
    if (!item) return;
    cJSON_DeleteItemFromObjectCaseSensitive(s, key);
    cJSON_AddItemToObject(s, key, item);
    save_config_locked();
}
