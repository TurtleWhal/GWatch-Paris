#include "watch.hpp"
#include "settings.hpp"

#include <cJSON.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

static const char *TAG_SCHED = "schedule";

void Schedule::parse_time(double v, uint8_t &hour, uint8_t &minute)
{
    // Config-file encoding: 7.43 → 7:43, not 7 + 0.43*60 minutes. The
    // fractional part is the literal minute count, so lift out floor
    // + (frac * 100). round() to survive the usual float-imprecision
    // where 12.10 might parse as 12.0999... .
    if (v < 0) v = 0;
    int h  = (int)std::floor(v);
    int mm = (int)std::lround((v - h) * 100.0);
    if (mm >= 60) mm = 59;   // clamp bad hand-edits
    if (h  >= 24) h  = 23;
    hour   = (uint8_t)h;
    minute = (uint8_t)mm;
}

// Standalone read of config.json (independent of Settings' in-memory
// tree). Called only from Schedule::init at boot; the small extra I/O
// is preferable to coupling the two classes and needing to reach into
// Settings' locked cJSON tree.
static cJSON *load_config_snapshot()
{
    FILE *f = fopen(GWATCH_CONFIG_PATH, "r");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 128 * 1024) { fclose(f); return nullptr; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

void Schedule::init()
{
    loaded.clear();
    for (int i = 0; i < 7; i++) weekday_map[i] = -1;
    currentSchedule = ClassSchedule::AUTO;
    show = false;
    text = nullptr;

    cJSON *root = load_config_snapshot();
    if (!root) {
        ESP_LOGW(TAG_SCHED, "config.json missing — no schedules loaded");
        useSchedule = true;
        return;
    }

    cJSON *sched = cJSON_GetObjectItemCaseSensitive(root, "schedule");
    if (!cJSON_IsObject(sched)) {
        ESP_LOGW(TAG_SCHED, "config.json has no \"schedule\" section");
        useSchedule = true;
        cJSON_Delete(root);
        return;
    }

    // Enabled flag — same key the screen toggle writes back.
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(sched, "enabled");
    useSchedule = cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : true;

    // schedule.schedules[]  → loaded[]
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(sched, "schedules");
    if (cJSON_IsArray(arr)) {
        cJSON *day_obj;
        cJSON_ArrayForEach(day_obj, arr) {
            if (!cJSON_IsObject(day_obj)) continue;

            ScheduleDay day;
            cJSON *name = cJSON_GetObjectItemCaseSensitive(day_obj, "name");
            if (cJSON_IsString(name) && name->valuestring)
                day.name = name->valuestring;

            cJSON *events = cJSON_GetObjectItemCaseSensitive(day_obj, "events");
            if (cJSON_IsArray(events)) {
                cJSON *ev_obj;
                cJSON_ArrayForEach(ev_obj, events) {
                    if (!cJSON_IsObject(ev_obj)) continue;
                    ScheduleEvent ev = {};
                    cJSON *ev_name  = cJSON_GetObjectItemCaseSensitive(ev_obj, "name");
                    cJSON *ev_start = cJSON_GetObjectItemCaseSensitive(ev_obj, "start");
                    cJSON *ev_end   = cJSON_GetObjectItemCaseSensitive(ev_obj, "end");
                    if (cJSON_IsString(ev_name) && ev_name->valuestring)
                        ev.text = ev_name->valuestring;
                    if (cJSON_IsNumber(ev_start))
                        parse_time(ev_start->valuedouble, ev.starthour, ev.startminute);
                    if (cJSON_IsNumber(ev_end))
                        parse_time(ev_end->valuedouble, ev.endhour, ev.endminute);
                    day.events.push_back(std::move(ev));
                }
            }
            loaded.push_back(std::move(day));
        }
    }

    // schedule.weekdays[] (optional) → weekday_map. Each entry is a
    // schedule NAME (matched against loaded[i].name) or empty string
    // for "none". Absent config → weekday_map stays all -1, meaning
    // AUTO mode shows nothing and the user has to pick a schedule
    // manually from the dropdown.
    cJSON *wdays = cJSON_GetObjectItemCaseSensitive(sched, "weekdays");
    if (cJSON_IsArray(wdays) && cJSON_GetArraySize(wdays) == 7) {
        for (int i = 0; i < 7; i++) {
            cJSON *entry = cJSON_GetArrayItem(wdays, i);
            weekday_map[i] = -1;
            if (cJSON_IsString(entry) && entry->valuestring && entry->valuestring[0]) {
                for (int k = 0; k < (int)loaded.size(); k++) {
                    if (loaded[k].name == entry->valuestring) {
                        weekday_map[i] = (int8_t)k;
                        break;
                    }
                }
            }
        }
    }

    ESP_LOGI(TAG_SCHED, "loaded %d schedule(s), enabled=%d",
             (int)loaded.size(), useSchedule ? 1 : 0);

    cJSON_Delete(root);
}

const ScheduleDay *Schedule::getCurrentSchedule()
{
    if (loaded.empty()) return nullptr;

    if (currentSchedule == ClassSchedule::NONE)
        return nullptr;

    int idx = -1;
    if (currentSchedule == ClassSchedule::AUTO) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm t;
        localtime_r(&tv.tv_sec, &t);
        idx = weekday_map[t.tm_wday];  // may be -1 for weekend
    } else {
        idx = (int)currentSchedule;
    }

    if (idx < 0 || idx >= (int)loaded.size()) return nullptr;
    return &loaded[idx];
}

const char *Schedule::getText()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    show = false;
    // The old implementation strdup'd the returned buffer, leaking on
    // every call. Return a static instead — analog watch face copies
    // via lv_label_set_text so the pointer only needs to be valid for
    // that single call.
    static char buffer[48];
    text = nullptr;

    const ScheduleDay *day = getCurrentSchedule();
    if (!day || !useSchedule) return "";

    // Currently in an event?
    for (const ScheduleEvent &event : day->events) {
        if (event.text.empty()) continue;
        if ((t.tm_hour > event.starthour ||
             (t.tm_hour == event.starthour && t.tm_min >= event.startminute)) &&
            (t.tm_hour < event.endhour ||
             (t.tm_hour == event.endhour && t.tm_min < event.endminute))) {
            show = true;
            snprintf(buffer, sizeof(buffer), "%s › %d:%02d", event.text.c_str(),
                     event.endhour > 12 ? event.endhour - 12 : event.endhour,
                     event.endminute);
            text = buffer;
            return buffer;
        }
    }

    // Otherwise the next-upcoming event, if any.
    for (const ScheduleEvent &event : day->events) {
        if (event.text.empty()) continue;
        if (t.tm_hour < event.starthour ||
            (t.tm_hour == event.starthour && t.tm_min < event.startminute)) {
            show = true;
            snprintf(buffer, sizeof(buffer), "%s - %d:%02d", event.text.c_str(),
                     event.starthour > 12 ? event.starthour - 12 : event.starthour,
                     event.startminute);
            text = buffer;
            return buffer;
        }
    }

    return "";
}

const char *Schedule::getFullSchedule()
{
    static char buffer[512];
    buffer[0] = '\0';

    const ScheduleDay *day = getCurrentSchedule();
    if (!day) return buffer;

    for (const ScheduleEvent &event : day->events) {
        if (event.text.empty()) continue;
        char line[48];
        snprintf(line, sizeof(line), "%s: %d:%02d - %d:%02d\n",
                 event.text.c_str(),
                 event.starthour > 12 ? event.starthour - 12 : event.starthour,
                 event.startminute,
                 event.endhour > 12 ? event.endhour - 12 : event.endhour,
                 event.endminute);
        size_t used = strlen(buffer);
        if (used + strlen(line) + 1 < sizeof(buffer))
            strcat(buffer, line);
    }
    return buffer;
}

void Schedule::setCurrentSchedule(ClassSchedule s)
{
    currentSchedule = s;
}

ClassSchedule Schedule::getSelectedSchedule()
{
    if (currentSchedule == ClassSchedule::AUTO) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm t;
        localtime_r(&tv.tv_sec, &t);
        int8_t idx = weekday_map[t.tm_wday];
        if (idx < 0) return ClassSchedule::NONE;
        return (ClassSchedule)idx;
    }
    return currentSchedule;
}

const char *Schedule::scheduleName(int idx) const
{
    if (idx < 0 || idx >= (int)loaded.size()) return "";
    return loaded[idx].name.c_str();
}
