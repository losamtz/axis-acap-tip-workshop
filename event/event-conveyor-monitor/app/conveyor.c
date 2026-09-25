#include "conveyor.h"
#include <limits.h>

void conveyor_set_enabled(Conveyor* conveyor, bool enabled) {
    if (conveyor->enabled != enabled) {
        conveyor->tracking = false;
        conveyor->monitored_seconds = 0;
        conveyor->jam = false;
    }
    conveyor->enabled = enabled;
}

bool conveyor_step(Conveyor* conveyor, int64_t elapsed_seconds) {
    conveyor->phase_seconds = (int)(elapsed_seconds % CYCLE_SECONDS);
    conveyor->blocked = conveyor->phase_seconds >= 20 && conveyor->phase_seconds < 40;
    conveyor->blocked_seconds = conveyor->blocked ? conveyor->phase_seconds - 20 : 0;
    if (!conveyor->enabled || !conveyor->blocked) {
        conveyor->tracking = false;
        conveyor->monitored_seconds = 0;
    } else {
        if (!conveyor->tracking || conveyor->monitored_cycle != elapsed_seconds / CYCLE_SECONDS) {
            conveyor->monitored_since = elapsed_seconds;
            conveyor->monitored_cycle = elapsed_seconds / CYCLE_SECONDS;
            conveyor->tracking = true;
        }
        conveyor->monitored_seconds = (int)(elapsed_seconds - conveyor->monitored_since);
    }
    conveyor->jam = conveyor->enabled && conveyor->tracking &&
                    conveyor->monitored_seconds >= conveyor->threshold_seconds;
    bool passed = !conveyor->blocked && elapsed_seconds - conveyor->last_package_second >= 4;
    if (passed) {
        conveyor->last_package_second = elapsed_seconds;
        if (conveyor->package_count < INT_MAX)
            ++conveyor->package_count;
    }
    return passed;
}
