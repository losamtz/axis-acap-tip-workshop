#pragma once
#include <glib.h>
#include <stdbool.h>

typedef struct {
    unsigned int view;
    unsigned int channel;
} CameraView;

/* View numbers are used by VAPIX. VDO channel IDs are resolved
 * separately because their numbering depends on the camera platform. */
GArray* views_discover(GError** error);
bool views_find(const GArray* views, unsigned int view, unsigned int* channel);
