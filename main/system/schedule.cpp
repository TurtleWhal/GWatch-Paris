#include "watch.hpp"
#include <time.h>
#include <sys/time.h>

ClassSchedule currentSchedule = ClassSchedule::AUTO;
// uint8_t lastScheduleDay = 255;

const struct ScheduleEvent *Schedule::getCurrentSchedule()
{
    if (currentSchedule == ClassSchedule::NONE)
    {
        return nullptr;
    }
    else if (currentSchedule == ClassSchedule::AUTO)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct tm t;
        localtime_r(&tv.tv_sec, &t);

        return schedules[static_cast<int>(defaultschedules[t.tm_wday])];
    }
    else
    {
        return schedules[static_cast<int>(currentSchedule)];
    }
}

const char *Schedule::getText()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    show = false;
    text = nullptr;

    const struct ScheduleEvent *schedule = getCurrentSchedule();
    if (schedule == nullptr || !useSchedule)
        return "";

    for (uint8_t i = 0; i < MAX_EVENTS; i++)
    {
        ScheduleEvent event = schedule[i];

        if (event.text == nullptr)
            break;

        if ((t.tm_hour > event.starthour || (t.tm_hour == event.starthour && t.tm_min >= event.startminute)) &&
            (t.tm_hour < event.endhour || (t.tm_hour == event.endhour && t.tm_min < event.endminute)))
        {
            show = true;

            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%s -> %d:%02d", event.text, event.endhour > 12 ? event.endhour - 12 : event.endhour, event.endminute);
            // snprintf(buffer, sizeof(buffer), "%s → %d:%02d", event.text, event.endhour > 12 ? event.endhour - 12 : event.endhour, event.endminute);

            text = strdup(buffer);
            return text;
        }
    }

    // schedule = getCurrentSchedule();
    for (uint8_t i = 0; i < MAX_EVENTS; i++)
    {
        ScheduleEvent event = schedule[i];

        if (event.text == nullptr)
            break;

        if (t.tm_hour < event.starthour || (t.tm_hour == event.starthour && t.tm_min < event.startminute))
        {
            show = true;

            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%s @ %d:%02d", event.text, event.starthour > 12 ? event.starthour - 12 : event.starthour, event.startminute);

            text = strdup(buffer);
            return text;
        }
    }

    return "";
}

const char *Schedule::getFullSchedule()
{
    char buffer[39 * MAX_EVENTS];
    buffer[0] = '\0';

    const struct ScheduleEvent *schedule = getCurrentSchedule();

    for (uint8_t i = 0; i < MAX_EVENTS; i++)
    {
        ScheduleEvent event = schedule[i];

        if (event.text == nullptr)
        {
            break;
        }

        char line[39];
        snprintf(line, sizeof(line), "%s: %d:%02d - %d:%02d\n", event.text,
                 event.starthour > 12 ? event.starthour - 12 : event.starthour, event.startminute,
                 event.endhour > 12 ? event.endhour - 12 : event.endhour, event.endminute);

        strcat(buffer, line);
    }

    // printf("Full schedule:\n%s", buffer);

    return strdup(buffer);
}

void Schedule::setCurrentSchedule(ClassSchedule schedule)
{
    currentSchedule = schedule;
}

ClassSchedule Schedule::getSelectedSchedule()
{
    if (currentSchedule == ClassSchedule::AUTO)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct tm t;
        localtime_r(&tv.tv_sec, &t);

        return defaultschedules[t.tm_wday];
    }

    return currentSchedule;
}