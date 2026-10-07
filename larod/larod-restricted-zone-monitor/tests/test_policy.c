#include "detections.h"
#include "zone_policy.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    Zone zone = {.x = .3, .y = .2, .width = .4, .height = .6};
    assert(zone_contains_foot(zone, .4, .1, .6, .8));
    assert(!zone_contains_foot(zone, .4, .1, .6, .9));
    assert(!zone_contains_foot(zone, .0, .3, .2, .5));

    ZoneState state = {0};
    zone_step(&state, true, 0, 3, 2);
    zone_step(&state, true, 2.9, 3, 2);
    assert(!state.alarm);
    zone_step(&state, true, 3, 3, 2);
    assert(state.alarm);
    zone_step(&state, false, 4, 3, 2);
    zone_step(&state, false, 5.9, 3, 2);
    assert(state.alarm);
    zone_step(&state, true, 6, 3, 2);
    assert(state.alarm && !state.timing_clear);
    zone_step(&state, false, 7, 3, 2);
    zone_step(&state, false, 9, 3, 2);
    assert(!state.alarm);

    zone_reset(&state);
    zone_step(&state, true, 0, 3, 2);
    zone_step(&state, false, 2, 3, 2);
    zone_step(&state, true, 3, 3, 2);
    zone_step(&state, true, 5, 3, 2);
    assert(!state.alarm); /* An entry gap restarts activation timing. */
    zone_reset(&state);
    zone_step(&state, true, 0, 0, 0);
    assert(state.alarm);
    zone_step(&state, false, 0, 0, 0);
    assert(!state.alarm);
    zone_reset(&state);
    assert(!state.alarm && !state.occupied && !state.timing_entry);

    const float boxes[] = {.1f, .2f, .8f, .5f, .2f, .3f, .7f, .8f};
    float classes[] = {0, 2};
    const float scores[] = {.9f, .99f};
    Detections detections;
    assert(detections_decode(boxes, 8, classes, 2, scores, 2, 2, .6f, &detections));
    assert(detections.count == 1 && detections.people[0].left == .2f);
    assert(detections.raw_count == 2 && detections.person_candidates == 1);
    assert(detections.top_class == 2 && detections.top_confidence == .99f);
    assert(detections.best_person_confidence == .9f);
    assert(detections_decode(boxes, 8, classes, 2, scores, 2, 2, .95f, &detections));
    assert(detections.count == 0 && detections.person_candidates == 1);
    assert(detections.best_person_confidence == .9f);
    assert(detections_decode(boxes, 8, classes, 2, scores, 2, 0, .6f, &detections));
    assert(detections.count == 0 && detections.person_candidates == 0 &&
           detections.raw_count == 0); /* Clear all previous evidence. */
    assert(!detections_decode(boxes, 8, classes, 2, scores, 2, NAN, .6f, &detections));
    assert(!detections_decode(boxes, 8, classes, 2, scores, 2, 3, .6f, &detections));
    assert(!detections_decode(boxes, 8, classes, 2, scores, 2, -1, .6f, &detections));
    assert(!detections_decode(boxes, 8, classes, 2, scores, 2, 1.5f, .6f, &detections));
    classes[0] = NAN;
    assert(detections_decode(boxes, 8, classes, 2, scores, 2, 2, .6f, &detections));
    assert(detections.count == 0);
    puts("Zone timing and SSD decoder tests passed");
}
