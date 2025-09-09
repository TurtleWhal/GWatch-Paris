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

#include "wifitime.h"

#define MAX_WIFI_APS 3 // adjust this as needed

// List of WiFi credentials (try in order)
typedef struct
{
    const char *ssid;
    const char *password;
} wifi_ap_t;

wifi_ap_t wifi_ap_list[MAX_WIFI_APS] = {
    {"Garrett's Phone", "40961024"},
    {"NetworkOfIOT", "40961024"},
    // {"SchoolNet", ""},
    {"GuestNet", ""},
};

static int current_ap_index = 0;

const static char *TAG = "wifitime";

const char *months[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

local_datetime_t get_local_datetime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info); // convert to local time

    local_datetime_t t;
    t.year = tm_info.tm_year + 1900;
    t.month = tm_info.tm_mon + 1; // tm_mon = 0..11
    t.day = tm_info.tm_mday;
    t.hour = tm_info.tm_hour;
    t.min = tm_info.tm_min;
    t.sec = tm_info.tm_sec;
    t.ms = tv.tv_usec / 1000; // milliseconds within the second

    return t;
}

void set_timezone(const char *tz)
{
    setenv("TZ", tz, 1); // e.g., "CET-1CEST,M3.5.0,M10.5.0/3"
    tzset();             // apply the new TZ
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event");
}

static void obtain_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year >= (2016 - 1900))
    {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Current time: %s", strftime_buf);

        // Turn off WiFi after time is obtained
        ESP_LOGI(TAG, "Shutting down WiFi to save power");
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to get time from NTP");
    }
}

static void connect_to_ap(void)
{
    if (current_ap_index >= MAX_WIFI_APS)
    {
        ESP_LOGW(TAG, "No more APs to try");
        return;
    }

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
        connect_to_ap();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", wifi_ap_list[current_ap_index].ssid);
        current_ap_index++;
        connect_to_ap(); // try next AP
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi connected, got IP");
        obtain_time();
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}
