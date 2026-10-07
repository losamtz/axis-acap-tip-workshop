#include "zone_policy.h"

bool zone_contains_foot(Zone zone, double left, double top, double right, double bottom) {
    (void)top;
    double foot_x = (left + right) / 2.0;
    return foot_x >= zone.x && foot_x <= zone.x + zone.width && bottom >= zone.y &&
           bottom <= zone.y + zone.height;
}

void zone_reset(ZoneState* state) {
    *state = (ZoneState){0};
}

void zone_step(ZoneState* state, bool occupied, double now, double activation_seconds,
               double clear_seconds) {
    state->occupied = occupied;
    if (occupied) {
        state->timing_clear = false;
        if (!state->timing_entry) {
            state->entered_at = now;
            state->timing_entry = true;
        }
        if (now - state->entered_at >= activation_seconds) {
            state->alarm = true;
        }
    } else {
        /* Entry requires uninterrupted detections. Once active, the alarm
         * tolerates short gaps according to the separate clear delay. */
        state->timing_entry = false;
        if (state->alarm && !state->timing_clear) {
            state->empty_since = now;
            state->timing_clear = true;
        }
        if (state->timing_clear && now - state->empty_since >= clear_seconds) {
            state->alarm = false;
            state->timing_clear = false;
        }
    }
}
