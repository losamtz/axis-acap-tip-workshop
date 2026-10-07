#pragma once
#include "detections.h"
#include "zone_policy.h"
#include <cairo/cairo.h>

/* Draw in output-image coordinates, excluding alignment padding. */
void overlay_render(cairo_surface_t* surface, unsigned width, unsigned height, Zone zone,
                    bool enabled, bool alarm, const Detections* detections);
