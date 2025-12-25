
#ifdef __cplusplus

struct ScheduleEvent
{
    uint8_t starthour;
    uint8_t startminute;
    uint8_t endhour;
    uint8_t endminute;
    const char *text;
};

enum class ClassSchedule
{
    O,
    E,
    A,
    Y,
    MA,
    AUTO = 254,
    NONE = 255
};

class Schedule
{
private:
#define MAX_EVENTS 8

    const ClassSchedule defaultschedules[7] = {
        ClassSchedule::NONE,
        ClassSchedule::O,
        ClassSchedule::E,
        ClassSchedule::A,
        ClassSchedule::O,
        ClassSchedule::E,
        ClassSchedule::NONE};

    const struct ScheduleEvent schedules[5][MAX_EVENTS] = {
        {
            // O
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Calculus"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "History"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "English"},
        },
        {
            // E
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 30, "Band"},
            {9, 40, 10, 15, "HiHo"},
            {10, 30, 12, 10, "Physics"},
            {12, 10, 12, 45, "Lunch"},
            {12, 55, 14, 35, "Cooking"},
        },
        {
            // A
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
            // Y
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 8, 16, "Calculus"},
            {8, 21, 8, 47, "Band"},
            {8, 52, 9, 18, "History"},
            {9, 23, 9, 49, "Physics"},
            {9, 54, 10, 20, "English"},
            {10, 25, 10, 50, "Cooking"},
        },
        {
            // MA (Morning Assembly)
            {7, 0, 7, 43, "Jazz Band"},
            {7, 50, 9, 20, "P1/P2"},
            {9, 30, 10, 30, "Assembly"},
            {10, 50, 12, 20, "P3/P4"},
            {12, 30, 12, 55, "Lunch"},
            {13, 05, 14, 35, "P5/P6"},
        },
    };

    // const struct ScheduleEvent schedules[5][MAX_EVENTS] = {
    //     {
    //         // O
    //         {12, 0, 23, 59, "O Day"},
    //     },
    //     {
    //         // E
    //         {12, 0, 23, 59, "E Day"},
    //     },
    //     {
    //         // A
    //         {12, 0, 23, 59, "A Day"},
    //     },
    //     {
    //         // Y
    //         {12, 0, 23, 59, "Y Day"},
    //     },
    //     {
    //         // MA (Morning Assembly)
    //         {12, 0, 23, 59, "MA Day"},
    //     },
    // };

public:
    bool useSchedule = true;
    bool show;
    char *text;

    // void init();
    const char *getText();
    const char *getFullSchedule();
    const struct ScheduleEvent *getCurrentSchedule();
    void setCurrentSchedule(ClassSchedule schedule);
    ClassSchedule getSelectedSchedule();
};

#endif // __cplusplus