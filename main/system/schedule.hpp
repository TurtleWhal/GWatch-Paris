#pragma once

#ifdef __cplusplus

#include <string>
#include <vector>

// One time-boxed entry within a day's schedule. Times are stored as
// integer hour + integer minute; the config-file encoding uses a
// "H.MM" float where MM is the literal minute count (so 7.43 → 7:43,
// NOT 7 hours 25.8 minutes). See parse_time in schedule.cpp.
struct ScheduleEvent
{
    uint8_t     starthour;
    uint8_t     startminute;
    uint8_t     endhour;
    uint8_t     endminute;
    std::string text;
};

// A named day-schedule. Users hand-edit these in config.json under
// schedule.schedules[]; the class holds them in memory as vectors so
// there's no fixed cap on event count.
struct ScheduleDay
{
    std::string                name;
    std::vector<ScheduleEvent> events;
};

// Sentinel values for `currentSchedule`. Everything ≥ AUTO is a
// sentinel; anything less is an index into `loaded[]`.
enum class ClassSchedule
{
    AUTO = 254, // follow weekday_map + current wday
    NONE = 255, // never show a schedule (manual "hide")
};

class Schedule
{
private:
    // Loaded from schedule.schedules[] in config.json at init.
    std::vector<ScheduleDay> loaded;

    // Which loaded[] index to use for each weekday, indexed
    // 0=Sun..6=Sat. -1 means "no schedule for this day". Populated
    // from schedule.weekdays[] in config.json (an array of schedule
    // names, mapped to indices during load); absent config = all -1.
    int8_t weekday_map[7];

    // Current manual selection. AUTO = follow weekday_map; NONE =
    // never show; anything else is an index into loaded.
    ClassSchedule currentSchedule;

    // Parse a single "H.MM" float from config.json into split hour /
    // minute bytes. See header comment on ScheduleEvent.
    static void parse_time(double v, uint8_t &hour, uint8_t &minute);

public:
    // schedule.enabled bool from config.json. Persisted via
    // Settings::writeBool from the schedule screen toggle. Read on
    // init(); the screen also refreshes it on each screen build.
    bool useSchedule = true;
    bool show;
    char *text;

    // Load schedules + weekday map + `enabled` from config.json. Safe
    // to call before Settings::init in principle (schedule loads its
    // own copy of the config file), but conventionally called right
    // after Settings::init so both classes read the same on-disk
    // state at the same instant.
    void init();

    // Currently-active day (based on AUTO/manual selection). Returns
    // nullptr if there's no schedule for the day, or the config was
    // missing/empty.
    const ScheduleDay *getCurrentSchedule();

    // Human-readable "next event" line for the watchface glance.
    const char *getText();
    // Full multi-line list of today's events, for the schedule screen.
    const char *getFullSchedule();

    // Manual override. Pass AUTO to return to weekday-driven picking,
    // NONE to hide, or an integer-cast ClassSchedule(idx) to force a
    // specific loaded schedule.
    void setCurrentSchedule(ClassSchedule s);
    // Returns AUTO's resolved index (for the dropdown current-value
    // display) when in AUTO, otherwise the manual selection.
    ClassSchedule getSelectedSchedule();

    // Introspection helpers for the schedule screen's dropdown. Names
    // come straight from schedule.schedules[].name in config.json.
    int         scheduleCount() const { return (int)loaded.size(); }
    const char *scheduleName(int idx) const;
};

#endif // __cplusplus
