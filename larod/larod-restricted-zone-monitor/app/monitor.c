#include "monitor.h"
#include "overlay.h"
#include "status_server.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define APP_NAME "larod_restricted_zone"
static const char* parameter_names[] = {"Enabled",           "Zone",         "Confidence",
                                        "ActivationSeconds", "ClearSeconds", "ViewArea",
                                        "ModelInputPreview"};

void monitor_invalidate(Monitor* monitor, const char* reason) {
    if (monitor->available) {
        syslog(LOG_INFO, "Monitoring unavailable: %s", reason);
    }
    g_clear_pointer(&monitor->preview_rgb, g_free);
    monitor->available = false;
    monitor->message = reason;
    monitor->last_frame = 0;
    monitor->detections = (Detections){0};
    zone_reset(&monitor->state);
}

static bool apply_parameter(Monitor* monitor, const char* name, const char* value) {
    const char* separator = strrchr(name, '.');
    name = separator ? separator + 1 : name;
    if (strcmp(name, "ModelInputPreview") == 0) {
        if (strcmp(value, "yes") != 0 && strcmp(value, "no") != 0) {
            return false;
        }
        monitor->preview_enabled = strcmp(value, "yes") == 0;
        syslog(LOG_INFO, "Model input preview %s",
               monitor->preview_enabled ? "enabled" : "disabled");
        g_clear_pointer(&monitor->preview_rgb, g_free);
        g_clear_pointer(&monitor->preview_error, g_free);
        return true; /* Diagnostic display does not change the alarm policy. */
    }
    if (strcmp(name, "ViewArea") == 0) {
        guint64 view;
        if (!g_ascii_string_to_unsigned(value, 10, 1, 256, &view, NULL)) {
            return false;
        }
        monitor->requested_view = view;
        syslog(LOG_INFO, "Requested inference view=%u", monitor->requested_view);
    } else if (strcmp(name, "Enabled") == 0) {
        if (strcmp(value, "yes") != 0 && strcmp(value, "no") != 0) {
            return false;
        }
        monitor->enabled = strcmp(value, "yes") == 0;
    } else if (strcmp(name, "Zone") == 0) {
        unsigned int x, y, width, height;
        char trailing;
        /* Reject a rectangle that extends beyond the image. */
        if (strlen(value) > 20 ||
            sscanf(value, "%3u,%3u,%3u,%3u%c", &x, &y, &width, &height, &trailing) != 4 ||
            x > 100 || y > 100 || width < 1 || height < 1 || width > 100 || height > 100 ||
            x + width > 100 || y + height > 100) {
            return false;
        }
        monitor->zone = (Zone){x / 100.0, y / 100.0, width / 100.0, height / 100.0};
    } else {
        guint64 number;
        bool confidence = strcmp(name, "Confidence") == 0;
        if (!g_ascii_string_to_unsigned(value, 10, confidence ? 1 : 0, confidence ? 100 : 60,
                                        &number, NULL)) {
            return false;
        }
        if (confidence) {
            monitor->confidence = number;
        } else if (strcmp(name, "ActivationSeconds") == 0) {
            monitor->activation_seconds = number;
        } else if (strcmp(name, "ClearSeconds") == 0) {
            monitor->clear_seconds = number;
        } else {
            return false;
        }
    }
    /* Old evidence must not activate an alarm under a newly selected policy. */
    monitor_invalidate(monitor, "Waiting for a frame after settings changed");
    return true;
}

static void parameter_changed(const gchar* name, const gchar* value, gpointer user_data) {
    Monitor* monitor = user_data;
    if (!apply_parameter(monitor, name, value)) {
        syslog(LOG_WARNING, "Rejected invalid parameter %s", name);
    }
}

bool monitor_init(Monitor* monitor) {
    monitor->message = "Loading model";
    GError* error = NULL;
    monitor->parameters = ax_parameter_new(APP_NAME, &error);
    if (!monitor->parameters) {
        goto failure;
    }
    for (size_t index = 0; index < G_N_ELEMENTS(parameter_names); index++) {
        gchar* value = NULL;
        const char* name = parameter_names[index];
        if (!ax_parameter_get(monitor->parameters, name, &value, &error)) {
            goto failure;
        }
        bool valid = apply_parameter(monitor, name, value);
        g_free(value);
        if (!valid || !ax_parameter_register_callback(monitor->parameters, name, parameter_changed,
                                                      monitor, &error)) {
            goto failure;
        }
        monitor->registered_parameters++;
    }
    monitor->views = views_discover(&error);
    if (error) {
        syslog(LOG_WARNING, "View discovery: %s", error->message);
        g_clear_error(&error);
    }
    monitor->message = "Loading model";
    return status_server_start() && alarm_event_init(&monitor->event);
failure:
    syslog(LOG_ERR, "Parameter setup: %s", error ? error->message : "invalid saved value");
    g_clear_error(&error);
    return false;
}

bool monitor_attach_overlay(Monitor* monitor, unsigned int view, unsigned int channel) {
    monitor->boxes = overlay_create(view, channel);
    return monitor->boxes != NULL;
}

void monitor_frame(Monitor* monitor, Detections* detections, double now, double inference_ms) {
    if (monitor->requested_view != monitor->active_view) {
        monitor_invalidate(monitor, "Switching inference view");
        return;
    }
    if (monitor->last_frame > 0 && now - monitor->last_frame > 3.0) {
        zone_reset(&monitor->state);
    }
    monitor->frame_interval = monitor->last_frame > 0 ? now - monitor->last_frame : 0;
    monitor->last_frame = now;
    monitor->available = true;
    monitor->message = "Live inference";
    monitor->inference_ms = inference_ms;
    bool occupied = false;
    for (size_t index = 0; index < detections->count; index++) {
        Person* person = &detections->people[index];
        person->inside = zone_contains_foot(monitor->zone, person->left, person->top, person->right,
                                            person->bottom);
        occupied = occupied || person->inside;
    }
    monitor->detections = *detections;
    if (now >= monitor->next_diagnostic_log) {
        syslog(LOG_INFO,
               "Inference view=%u channel=%u raw=%zu top_class=%d top_score=%.3f "
               "person_candidates=%zu best_person=%.3f threshold=%u%% accepted=%zu time_ms=%.1f",
               monitor->active_view, monitor->active_channel, detections->raw_count,
               detections->top_class, (double)detections->top_confidence,
               detections->person_candidates, (double)detections->best_person_confidence,
               monitor->confidence, detections->count, inference_ms);
        monitor->next_diagnostic_log = now + 10.0;
    }
    if (monitor->enabled) {
        zone_step(&monitor->state, occupied, now, monitor->activation_seconds,
                  monitor->clear_seconds);
    } else {
        zone_reset(&monitor->state);
    }
}

void monitor_tick(Monitor* monitor, double now) {
    if (monitor->active_view && monitor->requested_view != monitor->active_view) {
        monitor_invalidate(monitor, "Switching inference view");
    }
    if (monitor->available && now - monitor->last_frame > 3.0) {
        monitor_invalidate(monitor, "No recent inference result");
    }
    monitor->event_ok = alarm_event_update(&monitor->event, monitor->state.alarm,
                                           monitor->available && monitor->enabled);
    monitor->overlay_ok = false;
    if (monitor->boxes) {
        monitor->overlay_ok =
            overlay_draw(monitor->boxes, monitor->zone, monitor->enabled && monitor->available,
                         monitor->state.alarm, &monitor->detections);
    }
    const char* state = !monitor->enabled         ? "Disabled"
                        : !monitor->available     ? "Unknown"
                        : monitor->state.alarm    ? "Alarm"
                        : monitor->state.occupied ? "Presence pending"
                                                  : "Clear";
    json_t* root = json_pack(
        "{s:s,s:s,s:b,s:b,s:b,s:b,s:b,s:i,s:i,s:i,s:f,s:f,s:f,s:I}", "state", state, "message",
        monitor->message, "enabled", monitor->enabled, "available", monitor->available, "alarm",
        monitor->state.alarm, "overlayOk", monitor->overlay_ok, "eventOk", monitor->event_ok,
        "confidence", monitor->confidence, "activationSeconds", monitor->activation_seconds,
        "clearSeconds", monitor->clear_seconds, "inferenceMs", monitor->inference_ms, "fps",
        monitor->frame_interval > 0 ? 1.0 / monitor->frame_interval : 0, "occupiedSeconds",
        monitor->state.timing_entry ? now - monitor->state.entered_at : 0, "heartbeatMs",
        (json_int_t)(g_get_monotonic_time() / 1000));
    json_object_set_new(root, "overlayStreams", json_integer(overlay_stream_count(monitor->boxes)));
    json_object_set_new(root, "previewEnabled", json_boolean(monitor->preview_enabled));
    json_t* preview = json_null();
    if (monitor->preview_enabled && monitor->available && monitor->preview_rgb) {
        preview = json_pack("{s:i,s:i,s:I,s:f,s:s}", "width", monitor->preview_width, "height",
                            monitor->preview_height, "sequence",
                            (json_int_t)monitor->preview_sequence, "ageSeconds",
                            now - monitor->preview_captured_at, "rgb", monitor->preview_rgb);
    }
    json_object_set_new(root, "inputPreview", preview);
    json_object_set_new(root, "previewError",
                        json_string(monitor->preview_error ? monitor->preview_error : ""));
    json_object_set_new(root, "rawDetections", json_integer(monitor->detections.raw_count));
    json_object_set_new(root, "personCandidates",
                        json_integer(monitor->detections.person_candidates));
    json_object_set_new(root, "bestPersonConfidence",
                        monitor->detections.person_candidates
                            ? json_real(monitor->detections.best_person_confidence * 100.0)
                            : json_null());
    json_object_set_new(root, "requestedView", json_integer(monitor->requested_view));
    json_object_set_new(root, "activeView", json_integer(monitor->active_view));
    json_object_set_new(root, "activeChannel", json_integer(monitor->active_channel));
    json_object_set_new(root, "switchingView",
                        json_boolean(monitor->requested_view != monitor->active_view));
    json_t* views = json_array();
    if (monitor->views) {
        for (guint index = 0; index < monitor->views->len; index++) {
            CameraView entry = g_array_index(monitor->views, CameraView, index);
            json_array_append_new(
                views, json_pack("{s:i,s:i}", "view", entry.view, "channel", entry.channel));
        }
    }
    json_object_set_new(root, "views", views);
    json_object_set_new(root, "zone",
                        json_pack("[f,f,f,f]", monitor->zone.x, monitor->zone.y,
                                  monitor->zone.width, monitor->zone.height));
    json_t* people = json_array();
    for (size_t index = 0; index < monitor->detections.count; index++) {
        Person* person = &monitor->detections.people[index];
        json_array_append_new(people,
                              json_pack("{s:f,s:f,s:f,s:f,s:f,s:b}", "left", (double)person->left,
                                        "top", (double)person->top, "right", (double)person->right,
                                        "bottom", (double)person->bottom, "confidence",
                                        (double)person->confidence, "inside", person->inside));
    }
    json_object_set_new(root, "people", people);
    char* text = json_dumps(root, JSON_COMPACT);
    if (text) {
        status_server_publish(text);
        free(text);
    }
    json_decref(root);
}

void monitor_destroy(Monitor* monitor) {
    g_free(monitor->preview_rgb);
    g_free(monitor->preview_error);
    g_clear_pointer(&monitor->views, g_array_unref);
    overlay_destroy(monitor->boxes);
    alarm_event_destroy(&monitor->event);
    status_server_stop();
    if (monitor->parameters) {
        for (unsigned int index = 0; index < monitor->registered_parameters; index++) {
            ax_parameter_unregister_callback(monitor->parameters, parameter_names[index]);
        }
        ax_parameter_free(monitor->parameters);
    }
}
