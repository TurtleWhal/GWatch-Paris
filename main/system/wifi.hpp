#include "esp_event.h"

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