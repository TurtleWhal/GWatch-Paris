#pragma once

#include "nvs_flash.h"
#include "nvs.h"

#ifdef __cplusplus

#include <string>

// Config-file location on the ESP. Populated at build time by
// spiffs_create_partition_image from the project's fs/ directory (see
// main/CMakeLists.txt), and mountable at runtime by Settings::init.
#ifdef ESP_PLATFORM
#define GWATCH_CONFIG_MOUNT "/spiffs"
#define GWATCH_CONFIG_PATH  "/spiffs/config.json"
#else
// Non-ESP (simulator) build. GWATCH_CONFIG_PATH may be pre-defined
// via CMake target_compile_definitions to point at a sim-specific
// file (see simulator/CMakeLists.txt), so hand-edits made while
// testing the sim never touch the source-controlled ESP config. If
// no override is set we fall back to fs/config.json relative to the
// launch dir.
#define GWATCH_CONFIG_MOUNT "fs"
#ifndef GWATCH_CONFIG_PATH
#define GWATCH_CONFIG_PATH  "fs/config.json"
#endif
#endif

class Settings
{
private:
public:
    void init();

    // -- Legacy NVS-backed API. Kept for the settings that don't fit
    // the JSON model yet (alarm blobs, useSchedule bool, battery-anchor
    // uint16s, etc.). New settings should go through the JSON API below.
    void writeUint8(const char *key, uint8_t value);
    uint8_t readUint8(const char *key, uint8_t defaultValue);

    void writeUint16(const char *key, uint16_t value);
    uint16_t readUint16(const char *key, uint16_t defaultValue);

    // -- JSON-backed API, reads/writes /spiffs/config.json.
    // The parsed document is kept in memory (a cJSON tree owned by
    // this class); every setter mutates the tree and flushes the
    // whole file back to disk. That's cheap for a small config
    // (single-KB range) and avoids partial-write corruption.
    //
    // `section` is the top-level object name (e.g. "settings",
    // "schedule", "homeassistant") and `key` is the leaf key inside
    // it. Missing keys / wrong types return the supplied default.
    // Setters create the section object on demand if it doesn't
    // exist.
    std::string readString(const char *section, const char *key,
                           const char *default_val);
    void        writeString(const char *section, const char *key,
                            const char *value);

    bool readBool(const char *section, const char *key, bool default_val);
    void writeBool(const char *section, const char *key, bool value);

    int  readInt(const char *section, const char *key, int default_val);
    void writeInt(const char *section, const char *key, int value);
};

#endif // __cplusplus
