#ifndef CONVEYOR_H
#define CONVEYOR_H
#include <stdbool.h>
#include <stdint.h>
#define CYCLE_SECONDS 60
#define DEFAULT_JAM_THRESHOLD_SECONDS 10

typedef struct {
    bool enabled;
    int threshold_seconds;
    bool tracking;
    int64_t monitored_since;
    int64_t monitored_cycle;
    int monitored_seconds;
    bool blocked;
    bool jam;
    int blocked_seconds;
    int phase_seconds;
    int package_count;
    int64_t last_package_second;
} Conveyor;

/* elapsed_seconds is monotonic time since simulation start. Returns true for
 * an observed package passage. Delayed ticks do not fabricate missed packages. */
void conveyor_set_enabled(Conveyor* conveyor, bool enabled);
bool conveyor_step(Conveyor* conveyor, int64_t elapsed_seconds);
#endif
