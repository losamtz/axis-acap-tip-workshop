#include "../app/conveyor.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    Conveyor c = {.enabled = true, .threshold_seconds = DEFAULT_JAM_THRESHOLD_SECONDS};
    assert(!conveyor_step(&c, 0));
    assert(!c.jam && !c.blocked);
    assert(conveyor_step(&c, 4));
    assert(c.package_count == 1);
    assert(!conveyor_step(&c, 4)); /* No duplicate passage on the same tick. */
    assert(conveyor_step(&c, 8));
    assert(conveyor_step(&c, 12));
    assert(conveyor_step(&c, 16));
    assert(!conveyor_step(&c, 20));
    assert(c.blocked && !c.jam && c.blocked_seconds == 0);
    assert(!conveyor_step(&c, 29));
    assert(!c.jam && c.blocked_seconds == 9);
    assert(!conveyor_step(&c, 30));
    assert(c.jam && c.blocked_seconds == 10);
    assert(!conveyor_step(&c, 39));
    assert(c.jam && c.package_count == 4);
    assert(conveyor_step(&c, 40));
    assert(!c.jam && !c.blocked && c.blocked_seconds == 0 && c.package_count == 5);
    assert(conveyor_step(&c, 60)); /* No burst of fabricated catch-up events. */
    assert(c.package_count == 6 && c.phase_seconds == 0);
    assert(!conveyor_step(&c, 80));
    assert(!conveyor_step(&c, 90));
    assert(c.jam);
    conveyor_set_enabled(&c, false);
    assert(!c.jam && c.monitored_seconds == 0);
    assert(!conveyor_step(&c, 91));
    conveyor_set_enabled(&c, true);
    assert(!conveyor_step(&c, 92));
    assert(c.monitored_seconds == 0 && !c.jam);
    c.threshold_seconds = 5;
    conveyor_step(&c, 96); assert(!c.jam);
    conveyor_step(&c, 97); assert(c.jam && c.monitored_seconds == 5);
    c.threshold_seconds = 10;
    conveyor_step(&c, 98); assert(!c.jam);
    conveyor_set_enabled(&c, false);
    assert(conveyor_step(&c, 100)); /* Package events continue while disabled. */
    conveyor_set_enabled(&c, true);
    c.threshold_seconds = 20;
    conveyor_step(&c, 140);
    conveyor_step(&c, 159); assert(!c.jam);
    conveyor_step(&c, 160); assert(!c.jam && c.monitored_seconds == 0);
    puts("PASS: passage timing, blockage, jam threshold, recovery, cycle repeat, delayed ticks");
    return 0;
}
