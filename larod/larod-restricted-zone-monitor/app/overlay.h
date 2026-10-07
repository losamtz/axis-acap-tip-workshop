#pragma once
#include "detections.h"
#include "zone_policy.h"

typedef struct StreamOverlay StreamOverlay;

/* Owns Overlay2 drawings on encoded streams from this VDO channel. */
StreamOverlay* overlay_create(unsigned int view, unsigned int channel);
bool overlay_draw(StreamOverlay* overlay, Zone zone, bool enabled, bool alarm,
                  const Detections* detections);
void overlay_destroy(StreamOverlay* overlay);

unsigned int overlay_stream_count(const StreamOverlay* overlay);
