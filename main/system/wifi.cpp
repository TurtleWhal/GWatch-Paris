#include <sys/time.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#include "watch.hpp"
#include <esp_http_client.h>
#include <esp_sntp.h>

const static char *TAG = "wifi";

/** List of known WiFi networks */
typedef struct
{
    const char *ssid;
    const char *password;
} wifi_ap_entry_t;

static wifi_ap_entry_t known_aps[] = {
    {"garrettphone", "40961024"},
    {"NetworkOfIOT", "40961024"},
};

static const int known_ap_count = sizeof(known_aps) / sizeof(known_aps[0]);

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event");
}

/** Function to fetch the time from pool.ntp.org and update the system time */
static void obtain_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();

    // Wait for time to sync...
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 20;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(4000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year >= (2016 - 1900))
    {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "NTP time (UTC): %s", strftime_buf);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to get time from NTP");
    }

    watch.wifi.disconnect();
}

/** Connect to a wireless access point
 * @param ssid the network ssid
 * @param pass the network password
 */
void WiFi::connect_to_ap(char *ssid, char *pass)
{
    status = WIFI_CONNECTING;

    wifi_config_t wifi_config = {0};

    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, pass);

    if (strlen(pass) == 0)
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "Trying to connect to OPEN SSID: %s", ssid);
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(TAG, "Trying to connect to WPA2 SSID: %s", ssid);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();
}

/** handles wifi events */
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    // if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    // {
    //     // wifi_scan_config_t scan_config = {
    //     //     .ssid = NULL,
    //     //     .bssid = NULL,
    //     //     .channel = 0,
    //     //     .show_hidden = true};
    //     // esp_wifi_scan_start(&scan_config, true); // true = block until done

    //     // connect_to_ap();
    // }
    // else
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "WiFi Disconnected. Reason: %d", event->reason);

        watch.wifi.status = WIFI_DISCONNECTED;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi connected, got IP");
        watch.wifi.status = WIFI_CONNECTED;

        xTaskCreate([](void *args)
                    { obtain_time(); vTaskDelete(NULL); }, "obtain_time", 1024 * 3, NULL, 3, NULL);
    }
}

/** runs once a day (checks every hour) to synchronize the time */
void WiFi::timesync_task()
{
    while (true)
    {
        static uint8_t lastday = -1;

        time_t now;
        time(&now);

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_wday != lastday) // if day changed
        {
            lastday = timeinfo.tm_wday;

            connect();
        }

        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000)); // delay 60 minutes
    }
}

/** Initialize WiFi */
void WiFi::init()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();

    xTaskCreatePinnedToCore(
        [](void *pvParameters)
        {
            auto *obj = static_cast<WiFi *>(pvParameters);
            obj->timesync_task();
        },
        "timesync_task",
        1024 * 3,
        this,
        4,
        NULL,
        0);
}

/** Connect WiFI */
void WiFi::connect()
{
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start a blocking scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true};

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    // Try to connect to the first known AP that is available
    for (int i = 0; i < known_ap_count; i++)
    {
        for (int j = 0; j < ap_count; j++)
        {
            if (strcmp((char *)ap_records[j].ssid, known_aps[i].ssid) == 0)
            {
                ESP_LOGI(TAG, "Found known AP: %s, trying to connect", known_aps[i].ssid);
                connect_to_ap((char *)known_aps[i].ssid, (char *)known_aps[i].password);
                free(ap_records);
                return;
            }
        }
    }

    ESP_LOGW(TAG, "No known APs found in scan");
    free(ap_records);
}

/** Disconnect WiFI */
void WiFi::disconnect()
{
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK)
    {
        // Handle error
        ESP_LOGE("WiFi", "Failed to disconnect from Wi-Fi: %s", esp_err_to_name(err));
    }
    else
    {
        // ESP_LOGI("WiFi", "Disconnected from Wi-Fi");
    }
}