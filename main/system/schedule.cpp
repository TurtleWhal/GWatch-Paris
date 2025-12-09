#include "watch.hpp"
#include <time.h>
#include <sys/time.h>

// void Schedule::init()
// {
//     // show = false;
//     // text = nullptr;

//     show = true;
//     text = "History -> 12:10";
// }

const char *Schedule::getText()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    show = false;
    text = nullptr;

    for (const auto &event : events[t.tm_wday])
    {
        if ((t.tm_hour > event.starthour || (t.tm_hour == event.starthour && t.tm_min >= event.startminute)) &&
            (t.tm_hour < event.endhour || (t.tm_hour == event.endhour && t.tm_min < event.endminute)))
        {
            show = true;

            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%s -> %02d:%02d", event.text, event.endhour > 12 ? event.endhour - 12 : event.endhour, event.endminute);

            text = strdup(buffer);
            return text;
        }
    }

    for (uint8_t i = 0; i < MAX_EVENTS; i++)
    {
        ScheduleEvent event = events[t.tm_wday][i];

        if (t.tm_hour < event.starthour || (t.tm_hour == event.starthour && t.tm_min < event.startminute))
        {
            show = true;

            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%s @ %02d:%02d", event.text, event.starthour > 12 ? event.starthour - 12 : event.starthour, event.startminute);

            text = strdup(buffer);
            return text;
        }
    }

    return "";
}