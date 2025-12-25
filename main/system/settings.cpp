#include "watch.hpp"

void Settings::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

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