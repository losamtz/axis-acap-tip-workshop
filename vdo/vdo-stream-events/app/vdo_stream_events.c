/*
 * Copyright 2026 Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <vdo-error.h>
#include <vdo-stream.h>

static GMainLoop* main_loop = NULL;
static VdoStream* event_stream = NULL;

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsyslog(LOG_ERR, format, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

static const char* event_name(unsigned event_type) {
    switch (event_type) {
        case VDO_STREAM_EVENT_EXISTING:
            return "EXISTING";
        case VDO_STREAM_EVENT_CREATED:
            return "CREATED";
        case VDO_STREAM_EVENT_CLOSED:
            return "CLOSED";
        default:
            return "UNKNOWN";
    }
}

static void log_stream_info(unsigned stream_id) {
    GError* error = NULL;
    VdoStream* stream = vdo_stream_get(stream_id, &error);
    if (!stream) {
        syslog(LOG_WARNING, "Could not open stream %u for info: %s", stream_id, error->message);
        g_clear_error(&error);
        return;
    }

    VdoMap* info = vdo_stream_get_info(stream, NULL);
    if (!info) {
        syslog(LOG_WARNING, "Stream %u did not provide stream info", stream_id);
        g_object_unref(stream);
        return;
    }

    unsigned width = vdo_map_get_uint32(info, "width", 0);
    unsigned height = vdo_map_get_uint32(info, "height", 0);
    unsigned format = vdo_map_get_uint32(info, "format", 0);
    unsigned camera = vdo_map_get_uint32(info, "camera", 0);
    unsigned rotation = vdo_map_get_uint32(info, "rotation", 0);

    syslog(LOG_INFO,
           "Stream %u info: width=%u height=%u format=%u camera=%u rotation=%u",
           stream_id,
           width,
           height,
           format,
           camera,
           rotation);

    g_object_unref(info);
    g_object_unref(stream);
}

static gboolean signal_handler(gpointer user_data) {
    (void)user_data;
    g_main_loop_quit(main_loop);
    return G_SOURCE_REMOVE;
}

static gboolean stream_event_callback(GIOChannel* channel,
                                      GIOCondition condition,
                                      gpointer user_data) {
    (void)channel;
    (void)user_data;

    GError* error = NULL;
    VdoMap* event = NULL;

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        syslog(LOG_ERR, "VDO event connection was closed, condition=0x%04x", condition);
        g_main_loop_quit(main_loop);
        return G_SOURCE_REMOVE;
    }

    event = vdo_stream_get_event(event_stream, &error);
    if (!event) {
        if (g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_EVENT)) {
            g_clear_error(&error);
            return G_SOURCE_CONTINUE;
        }

        syslog(LOG_ERR, "Failed to read VDO stream event: %s", error->message);
        g_clear_error(&error);
        g_main_loop_quit(main_loop);
        return G_SOURCE_REMOVE;
    }

    unsigned event_type = vdo_map_get_uint32(event, "event", 0);
    unsigned stream_id = vdo_map_get_uint32(event, "id", 0);

    syslog(LOG_INFO, "VDO stream event: type=%s id=%u", event_name(event_type), stream_id);

    if (event_type == VDO_STREAM_EVENT_EXISTING || event_type == VDO_STREAM_EVENT_CREATED)
        log_stream_info(stream_id);

    g_object_unref(event);
    return G_SOURCE_CONTINUE;
}

int main(void) {
    GError* error = NULL;
    VdoMap* stream_filter = NULL;
    GIOChannel* channel = NULL;
    guint watch_id = 0;

    openlog("vdo_stream_events", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting VDO stream events example");

    main_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, signal_handler, NULL);
    g_unix_signal_add(SIGTERM, signal_handler, NULL);

    event_stream = vdo_stream_get(0, &error);
    if (!event_stream)
        panic("Failed to open VDO stream 0: %s", error->message);

    stream_filter = vdo_map_new();
    vdo_map_set_string(stream_filter, "filter", "overlay");

    if (!vdo_stream_attach(event_stream, stream_filter, &error))
        panic("Failed to attach overlay stream filter: %s", error->message);

    int event_fd = vdo_stream_get_event_fd(event_stream, &error);
    if (event_fd < 0)
        panic("Failed to get VDO event fd: %s", error->message);

    channel = g_io_channel_unix_new(event_fd);
    watch_id = g_io_add_watch(channel,
                              G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                              stream_event_callback,
                              NULL);
    if (!watch_id)
        panic("Failed to add VDO event fd to GLib main loop");

    syslog(LOG_INFO, "Waiting for overlay-capable stream events");
    g_main_loop_run(main_loop);

    if (watch_id)
        g_source_remove(watch_id);
    if (channel)
        g_io_channel_unref(channel);
    if (stream_filter)
        g_object_unref(stream_filter);
    if (event_stream)
        g_object_unref(event_stream);
    if (main_loop)
        g_main_loop_unref(main_loop);
    g_clear_error(&error);

    syslog(LOG_INFO, "Stopped VDO stream events example");
    closelog();
    return EXIT_SUCCESS;
}
