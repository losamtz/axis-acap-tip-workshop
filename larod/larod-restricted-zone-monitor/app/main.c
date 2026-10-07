/* Workshop entry point: initialize once, then acquire, infer, evaluate, release.
 * Model and preprocessing helpers are adapted from Axis object-detection.
 * See UPSTREAM.md for the pinned revision and local changes. */
#include "argparse.h"
#include "channel_util.h"
#include "detections.h"
#include "model.h"
#include "monitor.h"
#include "overlay.h"
#include "stream.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <vdo-channel.h>

static volatile sig_atomic_t running = 1;

static void stop(int signal_number) {
    (void)signal_number;
    running = 0;
}

static double monotonic_seconds(void) {
    return g_get_monotonic_time() / 1000000.0;
}

/* Validate the SSD output contract before interpreting mapped memory as floats.
 * A different model needs its own decoder; matching file extensions is not enough. */
static bool read_people(model_provider_t* model, unsigned int confidence, Detections* people) {
    if (model->num_outputs != 4) {
        return false;
    }
    model_tensor_output_t output[4];
    for (unsigned int index = 0; index < 4; index++) {
        if (!model_get_tensor_output_info(model, index, &output[index]) ||
            output[index].datatype != LAROD_TENSOR_DATA_TYPE_FLOAT32 ||
            output[index].size < sizeof(float) || !output[index].data) {
            return false;
        }
    }
    return detections_decode(output[0].data, output[0].size / sizeof(float), output[1].data,
                             output[1].size / sizeof(float), output[2].data,
                             output[2].size / sizeof(float), ((const float*)output[3].data)[0],
                             confidence / 100.0f, people);
}

/* This runs between frames: no VDO buffer is held by the inference loop. */
static void close_view(Monitor* monitor, model_provider_t* model, VdoStream** stream) {
    if (*stream) {
        vdo_stream_stop(*stream);
    }
    model_provider_reset_stream(model);
    g_clear_object(stream);
    overlay_destroy(monitor->boxes);
    monitor->boxes = NULL;
    monitor->active_view = 0;
    monitor->active_channel = 0;
}

static bool open_view(Monitor* monitor, model_provider_t* model, VdoStream** stream,
                      struct pollfd* descriptor) {
    unsigned int view = monitor->requested_view;
    unsigned int channel;
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoMap) stream_info = NULL;
    g_autoptr(VdoChannel) source = NULL;
    g_autoptr(VdoMap) channel_info = NULL;
    monitor_invalidate(monitor, "Switching inference view");
    close_view(monitor, model, stream);
    monitor_tick(monitor, monotonic_seconds());
    syslog(LOG_INFO, "Opening inference view=%u", view);
    if (!views_find(monitor->views, view, &channel)) {
        monitor_invalidate(monitor, "Selected view is unavailable. Select an available view.");
        return false;
    }

    /* Resolve geometry again: different views can have different sizes/formats. */
    source = vdo_channel_get(channel, &error);
    if (!source) {
        goto failure;
    }
    channel_info = vdo_channel_get_info(source, &error);
    if (!channel_info) {
        goto failure;
    }
    syslog(LOG_INFO, "Resolved inference view=%u to VDO channel=%u; channel info follows",
           view, channel);
    vdo_map_dump(channel_info);
    img_info_t metadata = model_provider_get_model_metadata(model);
    VdoResolution requested = {metadata.width, metadata.height};
    VdoResolution chosen = requested;
    unsigned int rotation = vdo_map_get_uint32(channel_info, "rotation", 0);
    if (!channel_util_choose_stream_resolution(channel, requested, &chosen, rotation,
                                               &metadata.format)) {
        goto failure;
    }
    *stream = stream_create(channel, metadata.format, chosen, &error);
    if (!*stream) {
        goto failure;
    }
    stream_info = vdo_stream_get_info(*stream, &error);
    if (!stream_info) {
        goto failure;
    }
    syslog(LOG_INFO, "Actual VDO stream info for requested view=%u channel=%u follows",
           view, channel);
    vdo_map_dump(stream_info);
    unsigned int reported_channel = vdo_map_get_uint32(stream_info, "channel", G_MAXUINT);
    if (reported_channel != G_MAXUINT && reported_channel != channel) {
        syslog(LOG_ERR, "VDO source mismatch: view=%u requested channel=%u, returned channel=%u",
               view, channel, reported_channel);
        goto failure;
    }
    if (reported_channel == G_MAXUINT) {
        syslog(LOG_WARNING, "VDO stream info omits channel; source cannot be verified from metadata");
    }
    /* A matching channel does not prove that firmware applied the view crop.
     * Compare Actual model input with live video when validating a new camera. */
    if (!model_provider_update_image_metadata(model, stream_info) ||
        !vdo_stream_start(*stream, &error)) {
        goto failure;
    }
    int fd = vdo_stream_get_fd(*stream, &error);
    if (fd < 0) {
        goto failure;
    }
    *descriptor = (struct pollfd){.fd = fd, .events = POLLIN};
    monitor_attach_overlay(monitor, view, channel);
    monitor->active_view = view;
    monitor->active_channel = channel;
    monitor->next_diagnostic_log = 0;
    monitor->message = "Waiting for the first frame from the selected view";
    syslog(LOG_INFO, "Inference view=%u channel=%u ready; model retained", view, channel);
    return true;

failure:
    syslog(LOG_WARNING, "Cannot open view=%u: %s", view,
           error ? error->message : "stream, tensor, or overlay setup failed");
    close_view(monitor, model, stream);
    monitor_invalidate(monitor,
                       "Cannot open selected view. Retrying; another view can be selected.");
    return false;
}

int main(int argc, char** argv) {
    openlog("larod_restricted_zone", LOG_PID, LOG_USER);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    args_t args;
    parse_args(argc, argv, &args);
    if (!args.model_file || !args.device_name) {
        syslog(LOG_ERR, "Model path and -d backend are required; use the packaged run options");
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    Monitor monitor = {0};
    model_provider_t* model = NULL;
    VdoStream* stream = NULL;
    GError* error = NULL;
    if (!monitor_init(&monitor)) {
        goto cleanup;
    }
    /* Publish settings before model loading, which may take several seconds.
     * Otherwise the FastCGI worker has no snapshot and returns HTTP 503. */
    monitor_tick(&monitor, monotonic_seconds());
    syslog(LOG_INFO, "Status ready; loading inference model using %s", args.device_name);

    /* 1. Load the model, inspect its input, allocate and map all output tensors.
     * The initial model input is a descriptor used to discover dimensions/pitch. */
    size_t output_count;
    model = model_provider_new(args.model_file, args.device_name, args.labels_file, &output_count);
    if (output_count != 4) {
        syslog(LOG_ERR, "Expected the bundled four-output SSD model");
        goto cleanup;
    }
    img_info_t metadata = model_provider_get_model_metadata(model);

    struct pollfd descriptor = {.fd = -1, .events = POLLIN};
    double next_status = 0;
    double next_preview = 0;
    double next_discovery = 0;
    double next_retry = 0;
    unsigned int attempted_view = 0;

    /* 3. Reuse the tensors and job requests. Only bind each newly encountered
     * VDO buffer once; model_run_inference selects it again on later frames. */
    while (running) {
        /* AXParameter and AXEvent dispatch on this thread's GLib context. */
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        double now = monotonic_seconds();
        /* Refresh the dropdown, including views created while this app is running. */
        if (now >= next_discovery || attempted_view != monitor.requested_view) {
            GArray* views = views_discover(&error);
            if (views) {
                g_clear_pointer(&monitor.views, g_array_unref);
                monitor.views = views;
            }
            if (error) {
                syslog(LOG_WARNING, "View discovery: %s", error->message);
                g_clear_error(&error);
            }
            next_discovery = now + 5.0;
        }
        if (monitor.requested_view != monitor.active_view &&
            (attempted_view != monitor.requested_view || now >= next_retry)) {
            attempted_view = monitor.requested_view;
            open_view(&monitor, model, &stream, &descriptor);
            next_retry = now + 5.0;
            next_preview = 0;
            next_status = 0;
        }
        if (now >= next_status) {
            monitor_tick(&monitor, now);
            next_status = now + 0.2;
        }
        if (!stream) {
            g_usleep(100000);
            continue;
        }
        int ready = poll(&descriptor, 1, 100);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            syslog(LOG_WARNING, "VDO polling failed; reopening the selected view");
            monitor_invalidate(&monitor, "Video stream interrupted. Reopening selected view.");
            close_view(&monitor, model, &stream);
            next_retry = now + 1.0;
            continue;
        }
        if (ready == 0) {
            continue;
        }
        VdoBuffer* buffer = vdo_stream_get_buffer(stream, &error);
        if (!buffer && g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
            g_clear_error(&error);
            continue;
        }
        if (!buffer) {
            syslog(LOG_WARNING, "VDO capture failed: %s; reopening selected view",
                   error ? error->message : "no buffer");
            g_clear_error(&error);
            monitor_invalidate(&monitor, "Video stream interrupted. Reopening selected view.");
            close_view(&monitor, model, &stream);
            next_retry = now + 1.0;
            continue;
        }
        VdoFrame* frame = vdo_buffer_get_frame(buffer);
        guint64 timestamp = frame ? vdo_frame_get_timestamp(frame) : 0;
        double started = monotonic_seconds();
        model->capture_input = monitor.preview_enabled &&
                               monitor.requested_view == monitor.active_view &&
                               started >= next_preview;
        if (model->capture_input) {
            next_preview = started + 1.0;
        }
        bool inferred = model_run_inference(model, buffer);
        double finished = monotonic_seconds();
        Detections people = {0};
        bool decoded = inferred && read_people(model, monitor.confidence, &people);

        /* Return the camera buffer before drawing, JSON serialization, or event
         * publishing. All subsequent work uses our own small detection records. */
        if (!vdo_stream_buffer_unref(stream, &buffer, &error)) {
            if (buffer) {
                g_object_unref(buffer);
            }
            goto cleanup;
        }
        if (!decoded) {
            monitor_invalidate(&monitor, "Inference unavailable or invalid SSD outputs");
        } else if (timestamp == 0 || timestamp / 1000000.0 > finished ||
                   finished - timestamp / 1000000.0 > 3.0) {
            monitor_invalidate(&monitor, "Frame exceeded the 3-second freshness limit");
        } else {
            monitor_frame(&monitor, &people, finished, (finished - started) * 1000.0);
            if (model->capture_input) {
                if (model->preview_error &&
                    g_strcmp0(model->preview_error, monitor.preview_error) != 0) {
                    syslog(LOG_WARNING, "Model input preview: %s", model->preview_error);
                }
                g_free(monitor.preview_rgb);
                g_free(monitor.preview_error);
                monitor.preview_rgb = g_steal_pointer(&model->preview_rgb);
                monitor.preview_error = g_steal_pointer(&model->preview_error);
                monitor.preview_width = metadata.width;
                monitor.preview_height = metadata.height;
                monitor.preview_sequence++;
                monitor.preview_captured_at = finished;
            }
        }
    }
    result = EXIT_SUCCESS;
cleanup:
    if (error) {
        syslog(LOG_ERR, "Video: %s", error->message);
        g_clear_error(&error);
    }
    if (stream) {
        vdo_stream_stop(stream);
    }
    monitor_destroy(&monitor);
    if (model) {
        model_provider_destroy(model);
    }
    g_clear_object(&stream);
    closelog();
    return result;
}
