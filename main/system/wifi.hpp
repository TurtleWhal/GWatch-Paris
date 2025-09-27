#include "esp_event.h"

// // typedef struct
// // {
// //     int year;  // full year, e.g., 2025
// //     int month; // 1..12
// //     int day;   // 1..31
// //     int hour;  // 0..23
// //     int min;   // 0..59
// //     int sec;   // 0..59
// //     int ms;    // 0..999
// // } local_datetime_t;

// extern const char *months[12];

// void set_timezone(const char *tz);
// void wifi_init(void);
// void wifi_connect();

#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus

enum WiFiStatus
{
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED
};

class WiFi
{
private:
    void connect_to_ap(char *ssid, char *pass);
    void timesync_task();

public:
    void init();
    void connect();
    void disconnect();

    WiFiStatus status = WIFI_DISCONNECTED;
};

#endif // __cplusplus

#endif // WIFI_H