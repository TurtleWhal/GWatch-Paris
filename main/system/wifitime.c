#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "esp_http_client.h"
#include "cJSON.h"

#include "system.h"

#define MAX_WIFI_APS 2 // adjust this as needed

// List of WiFi credentials (try in order)
typedef struct
{
    const char *ssid;
    const char *password;
} wifi_ap_t;

wifi_ap_t wifi_ap_list[MAX_WIFI_APS] = {
    {"garrettphone", "40961024"},
    // {"Garrett's Phone", "40961024"},
    {"NetworkOfIOT", "40961024"},
    // {"SchoolNet", ""},
    // {"GuestNet", ""},
};

static int current_ap_index = 0;

const static char *TAG = "wifitime";

const char *months[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

void set_timezone(const char *tz)
{
    setenv("TZ", tz, 1); // e.g., "CET-1CEST,M3.5.0,M10.5.0/3"
    tzset();             // apply the new TZ
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event");
}

static void update_timezone_from_api(void)
{
    esp_http_client_config_t config = {
        .url = "http://worldtimeapi.org/api/ip",
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (esp_http_client_perform(client) == ESP_OK)
    {
        int content_length = esp_http_client_get_content_length(client);
        char *buffer = malloc(content_length + 1);
        if (buffer)
        {
            int read_len = esp_http_client_read(client, buffer, content_length);
            buffer[read_len] = '\0';

            // Parse JSON
            cJSON *root = cJSON_Parse(buffer);
            if (root)
            {
                cJSON *tz = cJSON_GetObjectItem(root, "timezone");
                if (tz && cJSON_IsString(tz))
                {
                    ESP_LOGI(TAG, "Setting timezone to: %s", tz->valuestring);
                    set_timezone(tz->valuestring); // <- updates TZ automatically
                }
                cJSON_Delete(root);
            }
            free(buffer);
        }
    }
    esp_http_client_cleanup(client);
}

static void timezone_task(void *pvParameters)
{
    update_timezone_from_api(); // Your HTTP + cJSON code
    vTaskDelete(NULL);          // Kill task after done
}

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

        // 🔹 Launch timezone fetch as a separate task
        xTaskCreate(&timezone_task, "timezone_task", 8192, NULL, 5, NULL);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to get time from NTP");
    }

    sysinfo.wifi.connected = false;
}

static void connect_to_ap(void)
{
    if (current_ap_index >= MAX_WIFI_APS)
    {
        ESP_LOGW(TAG, "No more APs to try");
        return;
    }

    sysinfo.wifi.connected = true;

    wifi_config_t wifi_config = {0};

    strcpy((char *)wifi_config.sta.ssid, wifi_ap_list[current_ap_index].ssid);
    strcpy((char *)wifi_config.sta.password, wifi_ap_list[current_ap_index].password);

    if (strlen(wifi_ap_list[current_ap_index].password) == 0)
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "Trying to connect to OPEN SSID: %s", wifi_ap_list[current_ap_index].ssid);
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(TAG, "Trying to connect to WPA2 SSID: %s", wifi_ap_list[current_ap_index].ssid);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = true};
        // esp_wifi_scan_start(&scan_config, true); // true = block until done

        connect_to_ap();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", wifi_ap_list[current_ap_index].ssid);

        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnect reason: %d", event->reason);
        sysinfo.wifi.connected = false;

        current_ap_index++;
        connect_to_ap(); // try next AP
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi connected, got IP");
        sysinfo.wifi.connected = true;
        obtain_time();
    }
}

void update_time(void *args)
{
    while (true)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct tm tm_info;
        localtime_r(&tv.tv_sec, &tm_info); // convert to local time

        sysinfo.time.year = tm_info.tm_year + 1900;
        sysinfo.time.month = tm_info.tm_mon + 1; // tm_mon = 0..11
        sysinfo.time.day = tm_info.tm_mday;
        sysinfo.time.hour = tm_info.tm_hour;
        sysinfo.time.min = tm_info.tm_min;
        sysinfo.time.sec = tm_info.tm_sec;
        sysinfo.time.ms = tv.tv_usec / 1000; // milliseconds within the second

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void timesync_task(void *args)
{
    while (true)
    {
        static uint8_t lastday = -1;

        if (sysinfo.time.day != lastday) // if day changed
        {
            lastday = sysinfo.time.day;
            wifi_connect(); // fetch time again
        }

        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000)); // delay 60 minutes
    }
}

void wifi_init(void)
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

    ESP_LOGI(TAG, "wifi initalised.");

    xTaskCreatePinnedToCore(
        timesync_task,
        "timesync_task",
        1024 * 1,
        NULL,
        4,
        NULL,
        0);

    xTaskCreatePinnedToCore(
        update_time,
        "update_time",
        1024 * 4,
        NULL,
        4,
        NULL,
        0);
}

void wifi_connect()
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}