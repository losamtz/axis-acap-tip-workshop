#pragma once
#include "detections.h"
#include "event.h"
#include "overlay.h"
#include "views.h"
#include "zone_policy.h"
#include <axsdk/axparameter.h>

typedef struct {
    AXParameter* parameters;
    GArray* views;
    unsigned int requested_view;
    unsigned int active_view;
    unsigned int active_channel;
    unsigned int registered_parameters;
    AlarmEvent event;
    StreamOverlay* boxes;
    Zone zone;
    ZoneState state;
    Detections detections;
    bool preview_enabled;
    gchar* preview_rgb;
    gchar* preview_error;
    unsigned int preview_width;
    unsigned int preview_height;
    guint64 preview_sequence;
    double preview_captured_at;
    bool enabled;
    bool available;
    bool overlay_ok;
    bool event_ok;
    unsigned int confidence;
    unsigned int activation_seconds;
    unsigned int clear_seconds;
    double last_frame;
    double next_diagnostic_log;
    double inference_ms;
    double frame_interval;
    const char* message;
} Monitor;

bool monitor_init(Monitor* monitor);
bool monitor_attach_overlay(Monitor* monitor, unsigned int view, unsigned int channel);
void monitor_frame(Monitor* monitor, Detections* detections, double now, double inference_ms);
void monitor_tick(Monitor* monitor, double now);
void monitor_invalidate(Monitor* monitor, const char* reason);
void monitor_destroy(Monitor* monitor);
