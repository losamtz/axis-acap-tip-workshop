/* Camera-side drawing, following the workshop's Overlay2 stream-event example.
 * GLib dispatch and overlay_draw run on the inference thread, between frames. */
#include "overlay.h"
#include "overlay_render.h"
#include <axoverlay2.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <vdo-error.h>
#include <vdo-stream.h>

typedef struct {
    int id;
    unsigned width;
    unsigned height;
    cairo_surface_t* surface;
} Drawing;

struct StreamOverlay {
    unsigned view;
    unsigned channel;
    VdoStream* events;
    guint watch;
    bool started;
    bool healthy;
    GHashTable* drawings;
};

static void drawing_free(gpointer data) {
    Drawing* drawing = data;
    axo_err* error = NULL;
    if (!axo_remove_overlay(drawing->id, &error)) {
        syslog(LOG_WARNING, "Remove stream overlay %d: %s", drawing->id,
               axo_err_get_message(error));
    }
    axo_err_clear(&error);
    cairo_surface_destroy(drawing->surface);
    g_free(drawing);
}

static bool submit(Drawing* drawing, Zone zone, bool enabled, bool alarm,
                   const Detections* detections) {
    axo_err* error = NULL;
    bool ok = false;
    axo_buffer* buffer = axo_get_buffer(drawing->id, NULL, &error);
    if (!buffer) {
        /* A busy buffer is retried at the next monitor tick. */
        ok = axo_err_get_code(error) == AXO_ERR_WAIT;
        goto out;
    }
    char* target = axo_buffer_get_data(buffer, &error);
    if (!target) {
        goto out;
    }
    size_t bytes = (size_t)cairo_image_surface_get_stride(drawing->surface) *
                   cairo_image_surface_get_height(drawing->surface);
    if (bytes > axo_buffer_get_byte_size(buffer)) {
        syslog(LOG_ERR, "Overlay buffer is smaller than the drawing surface");
        goto out;
    }
    overlay_render(drawing->surface, drawing->width, drawing->height, zone, enabled, alarm,
                   detections);
    cairo_surface_flush(drawing->surface);
    memcpy(target, cairo_image_surface_get_data(drawing->surface), bytes);
    ok = axo_submit_buffer(buffer, NULL, &error);
out:
    if (error && axo_err_get_code(error) != AXO_ERR_WAIT &&
        axo_err_get_code(error) != AXO_ERR_NO_STREAM) {
        syslog(LOG_WARNING, "Update stream overlay: %s", axo_err_get_message(error));
    }
    axo_err_clear(&error);
    return ok;
}

static bool add_stream(StreamOverlay* overlay, unsigned id) {
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoStream) stream = vdo_stream_get(id, &error);
    if (!stream) {
        return true; /* Stream may have closed before its event was handled. */
    }
    g_autoptr(VdoMap) info = vdo_stream_get_info(stream, &error);
    if (!info) {
        syslog(LOG_WARNING, "Cannot inspect overlay stream %u: %s", id, error->message);
        return false;
    }
    g_autoptr(VdoMap) settings = vdo_stream_get_settings(stream, NULL);
    unsigned channel = vdo_map_get_uint32(info, "channel", G_MAXUINT);
    if (channel == G_MAXUINT && settings) {
        channel = vdo_map_get_uint32(settings, "channel", G_MAXUINT);
    }
    unsigned view = vdo_map_get_uint32(info, "view", 0);
    if (!view && settings) {
        view = vdo_map_get_uint32(settings, "view", 0);
    }
    /* Prefer explicit view identity, then the resolved VDO channel.
     * Never guess from the stream ID.
     * Only annotate encoded output; raw model input must stay unannotated. */
    unsigned format = vdo_map_get_uint32(info, "format", 0);
    if (!format && settings) {
        format = vdo_map_get_uint32(settings, "format", 0);
    }
    syslog(LOG_INFO, "Overlay candidate: stream=%u view=%u channel=%u format=%u target_view=%u target_channel=%u",
           id, view, channel, format, overlay->view, overlay->channel);
    if (!view && channel == G_MAXUINT) {
        syslog(LOG_WARNING, "Overlay stream %u has no channel identity; skipping", id);
        return false;
    }
    bool selected_view = view ? view == overlay->view : channel == overlay->channel;
    if (!selected_view ||
        (format != VDO_FORMAT_H264 && format != VDO_FORMAT_H265 && format != VDO_FORMAT_JPEG)) {
        return true;
    }
    if (g_hash_table_contains(overlay->drawings, GUINT_TO_POINTER(id))) {
        return true;
    }
    unsigned width = vdo_map_get_uint32(info, "width", 0);
    unsigned height = vdo_map_get_uint32(info, "height", 0);
    if (settings) {
        width = width ? width : vdo_map_get_uint32(settings, "width", 0);
        height = height ? height : vdo_map_get_uint32(settings, "height", 0);
    }
    if (!width || !height) {
        syslog(LOG_WARNING, "Overlay stream %u has no dimensions", id);
        return false;
    }
    bool upscale = (uint64_t)width * height > 4000000;
    unsigned used_width = upscale ? (width + 1) / 2 : width;
    unsigned used_height = upscale ? (height + 1) / 2 : height;
    unsigned aligned_width, aligned_height;
    axo_err* axo_error = NULL;
    axo_props* props = axo_props_new();
    axo_match* match = axo_match_new();
    bool ok = false;
    if (!axo_get_aligned_size(AXO_FORMAT_ARGB32, used_width, used_height, &aligned_width,
                              &aligned_height, &axo_error)) {
        goto out;
    }
    axo_props_set_format(props, AXO_FORMAT_ARGB32);
    axo_props_set_size(props, aligned_width, aligned_height);
    axo_props_set_upscale_x2(props, upscale);
    axo_match_stream_id(match, id);
    int overlay_id = axo_create_overlay(props, match, &axo_error);
    if (overlay_id < 0) {
        ok = axo_err_get_code(axo_error) == AXO_ERR_NO_STREAM;
        goto out;
    }
    Drawing* drawing = g_new0(Drawing, 1);
    drawing->id = overlay_id;
    drawing->width = used_width;
    drawing->height = used_height;
    drawing->surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, aligned_width, aligned_height);
    if (cairo_surface_status(drawing->surface) != CAIRO_STATUS_SUCCESS) {
        drawing_free(drawing);
        goto out;
    }
    g_hash_table_insert(overlay->drawings, GUINT_TO_POINTER(id), drawing);
    /* Never show uninitialized pixels while waiting for the first result. */
    Detections empty = {0};
    ok = submit(drawing, (Zone){0}, false, false, &empty);
    syslog(LOG_INFO, "Stream overlay created: view=%u channel=%u stream=%u size=%ux%u",
           overlay->view, channel, id, width, height);
out:
    if (axo_error && axo_err_get_code(axo_error) != AXO_ERR_NO_STREAM) {
        syslog(LOG_WARNING, "Create stream overlay: %s", axo_err_get_message(axo_error));
    }
    axo_err_clear(&axo_error);
    axo_props_free(props);
    axo_match_free(match);
    return ok;
}

static gboolean stream_event(GIOChannel* channel, GIOCondition condition, gpointer data) {
    (void)channel;
    StreamOverlay* overlay = data;
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        overlay->healthy = false;
        overlay->watch = 0;
        g_hash_table_remove_all(overlay->drawings);
        syslog(LOG_ERR, "Overlay stream-event connection lost");
        return G_SOURCE_REMOVE;
    }
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoMap) event = vdo_stream_get_event(overlay->events, &error);
    if (!event) {
        if (!g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_EVENT)) {
            overlay->healthy = false;
            syslog(LOG_WARNING, "Overlay event: %s", error ? error->message : "missing event");
        }
        return G_SOURCE_CONTINUE;
    }
    unsigned type = vdo_map_get_uint32(event, "event", 0);
    unsigned id = vdo_map_get_uint32(event, "id", 0);
    if (type == VDO_STREAM_EVENT_CLOSED) {
        g_hash_table_remove(overlay->drawings, GUINT_TO_POINTER(id));
    } else if (type == VDO_STREAM_EVENT_EXISTING || type == VDO_STREAM_EVENT_CREATED) {
        if (!add_stream(overlay, id)) {
            overlay->healthy = false;
        }
    }
    return G_SOURCE_CONTINUE;
}

StreamOverlay* overlay_create(unsigned int view, unsigned int channel) {
    StreamOverlay* overlay = g_new0(StreamOverlay, 1);
    overlay->view = view;
    overlay->channel = channel;
    overlay->healthy = true;
    overlay->drawings = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, drawing_free);
    axo_err* axo_error = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoMap) filter = vdo_map_new();
    g_autoptr(GIOChannel) io = NULL;
    if (!axo_start(NULL, &axo_error)) {
        goto failure;
    }
    overlay->started = true;
    overlay->events = vdo_stream_get(0, &error);
    if (!overlay->events) {
        goto failure;
    }
    vdo_map_set_string(filter, "filter", "overlay");
    vdo_map_set_uint32(filter, "intent", VDO_INTENT_EVENTFD);
    if (!vdo_stream_attach(overlay->events, filter, &error)) {
        goto failure;
    }
    int fd = vdo_stream_get_event_fd(overlay->events, &error);
    if (fd < 0) {
        goto failure;
    }
    io = g_io_channel_unix_new(fd);
    overlay->watch = g_io_add_watch(io, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                    stream_event, overlay);
    if (!overlay->watch) {
        goto failure;
    }
    syslog(LOG_INFO, "Overlay stream watch ready: view=%u channel=%u", view, channel);
    return overlay;
failure:
    syslog(LOG_ERR, "Stream overlay setup: %s",
           error       ? error->message
           : axo_error ? axo_err_get_message(axo_error)
                       : "watch failed");
    axo_err_clear(&axo_error);
    overlay_destroy(overlay);
    return NULL;
}

bool overlay_draw(StreamOverlay* overlay, Zone zone, bool enabled, bool alarm,
                  const Detections* detections) {
    if (!overlay) {
        return false;
    }
    bool ok = overlay->healthy && g_hash_table_size(overlay->drawings) > 0;
    GHashTableIter iterator;
    gpointer value;
    g_hash_table_iter_init(&iterator, overlay->drawings);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        if (!submit(value, zone, enabled, alarm, detections)) {
            ok = false;
        }
    }
    return ok;
}

void overlay_destroy(StreamOverlay* overlay) {
    if (!overlay) {
        return;
    }
    if (overlay->watch) {
        g_source_remove(overlay->watch);
    }
    g_clear_object(&overlay->events);
    g_hash_table_unref(overlay->drawings);
    if (overlay->started) {
        axo_stop(NULL);
    }
    g_free(overlay);
}

unsigned int overlay_stream_count(const StreamOverlay* overlay) {
    return overlay ? g_hash_table_size(overlay->drawings) : 0;
}
