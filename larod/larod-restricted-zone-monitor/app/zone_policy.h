#pragma once
#include <stdbool.h>

typedef struct {
    double x, y, width, height;
} Zone;

typedef struct {
    bool occupied;
    bool alarm;
    bool timing_entry;
    bool timing_clear;
    double entered_at;
    double empty_since;
} ZoneState;

bool zone_contains_foot(Zone zone, double left, double top, double right, double bottom);
void zone_reset(ZoneState* state);
void zone_step(ZoneState* state, bool occupied, double now, double activation_seconds,
               double clear_seconds);
