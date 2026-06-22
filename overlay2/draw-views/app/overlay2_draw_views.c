// Copyright (C) 2026 Axis Communications AB, Lund, Sweden
// Licensed under the MIT License. See LICENSE file for details.

#include <assert.h>
#include <axoverlay2.h>
#include <cairo/cairo.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <vdo-error.h>
#include <vdo-stream.h>

struct overlay {
    int overlay_id;
    unsigned stream_id;
    unsigned view_id;
    unsigned used_width;
    unsigned used_height;
    unsigned full_width;
    unsigned full_height;
    cairo_surface_t* surface;
};

static void overlay_record_deleter(void* overlay_void);
static gboolean signal_callback(gpointer userdata);
static gboolean animation_tick_callback(gpointer userdata);
static gboolean stream_event_callback(GIOChannel* channel, GIOCondition condition, gpointer userdata);
static void create_overlay(unsigned stream_id,
                           unsigned stream_width,
                           unsigned stream_height,
                           unsigned view_id);
static void remove_overlay(unsigned stream_id);
static void process_next_frame(struct overlay* overlay);
static void render_frame(struct overlay* overlay, char* target_buffer);

static VdoStream* vdo_event_stream = NULL;
static GHashTable* overlay_table = NULL;
static GMainLoop* main_loop = NULL;
static const unsigned tick_period_us = 1000000 / 15;

int main(void) {
    GError* error = NULL;
    axo_err* axo_error = NULL;
    VdoMap* stream_filter = NULL;
    GIOChannel* vdo_channel = NULL;
    unsigned vdo_watch_id = 0;
    bool axo_running = false;
    int ret = 0;

    openlog("overlay2_draw_views", LOG_PID, LOG_USER);

    if (!axo_start(NULL, &axo_error)) {
        syslog(LOG_ERR, "Failed to start axoverlay2: %s", axo_err_get_message(axo_error));
        ret = 1;
        goto out;
    }
    axo_running = true;

    overlay_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, overlay_record_deleter);
    main_loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(tick_period_us / 1000, animation_tick_callback, NULL);

    vdo_event_stream = vdo_stream_get(0, &error);
    if (!vdo_event_stream) {
        syslog(LOG_ERR, "Failed to open VDO stream 0: %s", error->message);
        ret = 1;
        goto out;
    }

    stream_filter = vdo_map_new();
    vdo_map_set_string(stream_filter, "filter", "overlay");

    if (!vdo_stream_attach(vdo_event_stream, stream_filter, &error)) {
        syslog(LOG_ERR, "Failed to attach VDO overlay filter: %s", error->message);
        ret = 1;
        goto out;
    }

    int stream_event_fd = vdo_stream_get_event_fd(vdo_event_stream, &error);
    if (stream_event_fd < 0) {
        syslog(LOG_ERR, "Failed to get VDO event fd: %s", error->message);
        ret = 1;
        goto out;
    }

    vdo_channel = g_io_channel_unix_new(stream_event_fd);
    vdo_watch_id = g_io_add_watch(vdo_channel,
                                  G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                                  stream_event_callback,
                                  NULL);
    if (!vdo_watch_id) {
        syslog(LOG_ERR, "Failed to add VDO event fd to GLib loop");
        ret = 1;
        goto out;
    }

    g_unix_signal_add(SIGINT, signal_callback, NULL);
    g_unix_signal_add(SIGTERM, signal_callback, NULL);

    g_main_loop_run(main_loop);

out:
    if (vdo_watch_id)
        g_source_remove(vdo_watch_id);
    if (vdo_channel)
        g_io_channel_unref(vdo_channel);
    if (axo_running)
        axo_stop(NULL);
    if (vdo_event_stream)
        g_object_unref(vdo_event_stream);
    if (stream_filter)
        g_object_unref(stream_filter);
    g_clear_error(&error);
    axo_err_clear(&axo_error);
    if (main_loop)
        g_main_loop_unref(main_loop);
    if (overlay_table)
        g_hash_table_unref(overlay_table);

    closelog();
    return ret;
}

static void overlay_record_deleter(void* overlay_void) {
    struct overlay* overlay = overlay_void;
    if (!overlay)
        return;
    if (overlay->surface)
        cairo_surface_destroy(overlay->surface);
    g_free(overlay);
}

static gboolean signal_callback(gpointer userdata) {
    (void)userdata;
    if (main_loop)
        g_main_loop_quit(main_loop);
    return G_SOURCE_REMOVE;
}

static gboolean animation_tick_callback(gpointer userdata) {
    (void)userdata;

    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;

    g_hash_table_iter_init(&iter, overlay_table);
    while (g_hash_table_iter_next(&iter, &key, &value))
        process_next_frame(value);

    return G_SOURCE_CONTINUE;
}

static gboolean stream_event_callback(GIOChannel* channel,
                                      GIOCondition condition,
                                      gpointer userdata) {
    (void)channel;
    (void)userdata;

    GError* error = NULL;
    VdoMap* vdo_event = NULL;
    VdoStream* vdo_stream = NULL;
    VdoMap* stream_info = NULL;
    gboolean ret = G_SOURCE_CONTINUE;

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        syslog(LOG_ERR, "Connection to VDO was broken, condition=0x%04x", condition);
        g_main_loop_quit(main_loop);
        return G_SOURCE_REMOVE;
    }

    vdo_event = vdo_stream_get_event(vdo_event_stream, &error);
    if (!vdo_event) {
        if (g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_EVENT))
            goto out;
        syslog(LOG_ERR, "Failed to get VDO stream event: %s", error->message);
        g_main_loop_quit(main_loop);
        ret = G_SOURCE_REMOVE;
        goto out;
    }

    unsigned event_type = vdo_map_get_uint32(vdo_event, "event", 0);
    unsigned stream_id = vdo_map_get_uint32(vdo_event, "id", 0);

    if (event_type == VDO_STREAM_EVENT_EXISTING || event_type == VDO_STREAM_EVENT_CREATED) {
        vdo_stream = vdo_stream_get(stream_id, &error);
        if (!vdo_stream) {
            syslog(LOG_ERR, "Failed to get VDO stream %u: %s", stream_id, error->message);
            g_main_loop_quit(main_loop);
            ret = G_SOURCE_REMOVE;
            goto out;
        }

        stream_info = vdo_stream_get_info(vdo_stream, NULL);
        if (!stream_info) {
            syslog(LOG_ERR, "VDO stream %u is missing stream info", stream_id);
            g_main_loop_quit(main_loop);
            ret = G_SOURCE_REMOVE;
            goto out;
        }

        unsigned width = vdo_map_get_uint32(stream_info, "width", 0);
        unsigned height = vdo_map_get_uint32(stream_info, "height", 0);
        if (!width || !height) {
            syslog(LOG_ERR, "VDO stream %u has invalid size %ux%u", stream_id, width, height);
            g_main_loop_quit(main_loop);
            ret = G_SOURCE_REMOVE;
            goto out;
        }

        unsigned view_id = vdo_map_get_uint32(stream_info, "camera", stream_id);
        create_overlay(stream_id, width, height, view_id);
    } else if (event_type == VDO_STREAM_EVENT_CLOSED) {
        remove_overlay(stream_id);
    }

out:
    g_clear_error(&error);
    if (vdo_event)
        g_object_unref(vdo_event);
    if (vdo_stream)
        g_object_unref(vdo_stream);
    if (stream_info)
        g_object_unref(stream_info);
    return ret;
}

static void create_overlay(unsigned stream_id,
                           unsigned stream_width,
                           unsigned stream_height,
                           unsigned view_id) {
    axo_err* axo_error = NULL;
    axo_props* props = NULL;
    axo_match* match = NULL;

    bool use_upscale = stream_width * stream_height > 4000000;
    unsigned used_width = use_upscale ? stream_width / 2 : stream_width;
    unsigned used_height = use_upscale ? stream_height / 2 : stream_height;
    unsigned full_width = 0;
    unsigned full_height = 0;

    if (!axo_get_aligned_size(AXO_FORMAT_ARGB32,
                              used_width,
                              used_height,
                              &full_width,
                              &full_height,
                              &axo_error)) {
        syslog(LOG_ERR, "Failed to align overlay size: %s", axo_err_get_message(axo_error));
        goto out;
    }

    props = axo_props_new();
    axo_props_set_format(props, AXO_FORMAT_ARGB32);
    axo_props_set_size(props, full_width, full_height);
    axo_props_set_upscale_x2(props, use_upscale);

    match = axo_match_new();
    axo_match_stream_id(match, stream_id);

    int overlay_id = axo_create_overlay(props, match, &axo_error);
    if (overlay_id < 0) {
        if (axo_err_get_code(axo_error) != AXO_ERR_NO_STREAM)
            syslog(LOG_ERR, "Failed to create overlay on stream %u: %s",
                   stream_id, axo_err_get_message(axo_error));
        goto out;
    }

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                         (int)full_width,
                                                         (int)full_height);
    assert(full_width * sizeof(uint32_t) == (unsigned)cairo_image_surface_get_stride(surface));

    struct overlay* overlay = g_malloc(sizeof(*overlay));
    *overlay = (struct overlay){
        .overlay_id = overlay_id,
        .stream_id = stream_id,
        .view_id = view_id,
        .used_width = used_width,
        .used_height = used_height,
        .full_width = full_width,
        .full_height = full_height,
        .surface = surface,
    };

    g_hash_table_insert(overlay_table, GUINT_TO_POINTER(stream_id), overlay);
    syslog(LOG_INFO, "Created overlay %d on stream %u", overlay_id, stream_id);
    process_next_frame(overlay);

out:
    axo_err_clear(&axo_error);
    if (props)
        axo_props_free(props);
    if (match)
        axo_match_free(match);
}

static void remove_overlay(unsigned stream_id) {
    axo_err* axo_error = NULL;
    const struct overlay* overlay = g_hash_table_lookup(overlay_table, GUINT_TO_POINTER(stream_id));

    if (!overlay)
        goto out;

    if (!axo_remove_overlay(overlay->overlay_id, &axo_error)) {
        syslog(LOG_ERR, "Failed to remove overlay %d on stream %u: %s",
               overlay->overlay_id, stream_id, axo_err_get_message(axo_error));
    }

out:
    g_hash_table_remove(overlay_table, GUINT_TO_POINTER(stream_id));
    axo_err_clear(&axo_error);
}

static void process_next_frame(struct overlay* overlay) {
    axo_err* axo_error = NULL;
    axo_buffer* buffer = axo_get_buffer(overlay->overlay_id, NULL, &axo_error);
    if (!buffer) {
        axo_err_code code = axo_err_get_code(axo_error);
        if (code != AXO_ERR_NO_STREAM && code != AXO_ERR_WAIT)
            syslog(LOG_ERR, "Failed to get buffer for overlay %d: %s",
                   overlay->overlay_id, axo_err_get_message(axo_error));
        goto out;
    }

    char* target_buffer = axo_buffer_get_data(buffer, &axo_error);
    if (!target_buffer) {
        syslog(LOG_ERR, "Failed to get overlay buffer data: %s", axo_err_get_message(axo_error));
        goto out;
    }

    render_frame(overlay, target_buffer);

    if (!axo_submit_buffer(buffer, NULL, &axo_error)) {
        syslog(LOG_ERR, "Failed to submit overlay buffer %d: %s",
               overlay->overlay_id, axo_err_get_message(axo_error));
        goto out;
    }

out:
    axo_err_clear(&axo_error);
}

static void render_frame(struct overlay* overlay, char* target_buffer) {
    cairo_t* cr = cairo_create(overlay->surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_scale(cr, overlay->used_width, overlay->used_height);
    cairo_set_line_width(cr, 0.006);

    if ((overlay->view_id % 3) == 1) {
        cairo_set_source_rgba(cr, 1.0, 0.85, 0.0, 1.0);
        cairo_rectangle(cr, 0.25, 0.25, 0.5, 0.3);
        cairo_stroke(cr);
    } else if ((overlay->view_id % 3) == 2) {
        cairo_set_source_rgba(cr, 0.0, 0.7, 1.0, 1.0);
        cairo_arc(cr, 0.5, 0.5, 0.25, 0.0, 2.0 * G_PI);
        cairo_stroke(cr);
    } else {
        cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 1.0);
        cairo_move_to(cr, 0.5, 0.25);
        cairo_line_to(cr, 0.3, 0.75);
        cairo_line_to(cr, 0.7, 0.75);
        cairo_close_path(cr);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(overlay->surface);

    unsigned byte_size = overlay->full_width * overlay->full_height * sizeof(uint32_t);
    memcpy(target_buffer, cairo_image_surface_get_data(overlay->surface), byte_size);
}
