
#ifdef __cplusplus

struct ScheduleEvent
{
    uint8_t starthour;
    uint8_t startminute;
    uint8_t endhour;
    uint8_t endminute;
    const char *text;
};

class Schedule
{
private:
#define MAX_EVENTS 8

    const struct ScheduleEvent events[7][MAX_EVENTS] = {
        {}, // Sunday
        {
            // Monday
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Calculus"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "History"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "English"},
        },
        {
            // Tuesday
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Band"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "Physics"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "Cooking"},
        },
        {
            // Wednesday
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 8, 25, "Calculus"},
            {8, 30, 9, 05, "Band"},
            {9, 25, 10, 10, "History"},
            {10, 20, 10, 55, "Physics"},
            {10, 55, 11, 30, "Lunch"},
            {11, 40, 12, 15, "English"},
            {12, 20, 12, 55, "Cooking"},
        },
        {
            // Thursday
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Calculus"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "History"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "English"},
        },
        {
            // Friday
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Band"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "Physics"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "Cooking"},
        },
        {}, // Saturday
    };

public:
    bool show;
    char *text;

    // void init();
    const char *getText();
};

#endif // __cplusplus