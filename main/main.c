#include "watch.hpp"

/** Entry Function for the code */
void app_main(void)
{
    // First thing after boot: if this wake-up was a timer wake from
    // a previous low_battery_shutdown, re-sample the battery via a
    // minimal ADC bring-up. If the cell rail is still flat, drop
    // straight back into deep sleep instead of paying the ~80 mA
    // boot burst on a battery that can't sustain it (which is what
    // produced the brown-out / boot-loop / protection-trip cycle).
    // Returns normally for fresh boots and once the rail recovers
    // (USB plug-in lifts it well above the threshold).
    battery_early_check_or_sleep_again();

    watch_init();
}