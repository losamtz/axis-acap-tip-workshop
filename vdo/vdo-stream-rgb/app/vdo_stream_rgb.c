#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>
#include <glib/gstdio.h>
// Needed for g_autoptr
#include <glib-object.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

#include "panic.h"

#include <poll.h>
#include <unistd.h>

 

static int handle_vdo_failed(GError* error) {
    // Maintenance/Installation in progress (e.g. Global-Rotation)
    if (vdo_error_is_expected(&error)) {
        syslog(LOG_INFO, "Expected vdo error %s", error->message);
        return EXIT_SUCCESS;
    } else {
        panic("Unexpected vdo error %s", error->message);
    }
    return EXIT_FAILURE;
}


int main(int argc, char** argv) {
    (void)argc;
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoStream) stream = NULL;
    g_autoptr(VdoMap) info = NULL;

    syslog(LOG_INFO, "Starting %s", argv[0]);

    

    // Create RGB stream
    // / This convenience API is roughly equivalent to:
    // g_autoptr(GError) error = nullptr;
    // g_autoptr(VdoMap) settings = vdo_map_new();
    // vdo_map_set_boolean(settings, "socket.blocking", false);
    // vdo_map_set_string(settings,  "image.fit", "scale");
    // vdo_map_set_uint32(settings,  "buffer.count", 2u);
    // vdo_map_set_uint32(settings,  "format", VDO_FORMAT_RGB);
    // vdo_map_set_uint32(settings,  "input", ...);
    // vdo_map_set_pair32u(settings, "resolution", ...);
    // vdo_stream_new(settings, nullptr, &error); 

    stream = vdo_stream_rgb_new(NULL, 1u, (VdoResolution){ .width = 640u, .height = 360u }, &error);

    if (!stream)
        return handle_vdo_failed(error);

    //Get stream fd
    int fd = vdo_stream_get_fd(stream, &error);

    if (fd < 0)
        return handle_vdo_failed(error);

    struct pollfd fds = {
        .fd = fd,
        .events = POLL_IN,
    };

    // Start stream
    if (!vdo_stream_start(stream, &error))
        return handle_vdo_failed(error);

    syslog(LOG_INFO, "Polling stream fd for new frames with non-blocking socket");

    info = vdo_stream_get_info(stream, &error);

    if (!info)
        panic("%s: Failed to get vdo stream info: %s", __func__, error->message);

    syslog(LOG_INFO, "Starting stream format RGB - resolution: %ux%u, at %u fps\n", vdo_map_get_uint32(info, "width", 0), vdo_map_get_uint32(info, "height", 0), (unsigned int)(vdo_map_get_double(info, "framerate", 0.0) + 0.5));

    // Fetch 10 frames
    for (size_t i = 0; i < 10;)
    {
        int status = 0;
        do {
            // If poll returns -1 then errno is set
            // if the errno is set to EINTR then just
            // continue this loop
            status = poll(&fds, 1, -1);
        } while (status == -1 && errno == EINTR);

        if (status < 0) {
            panic("Failed to poll with status %d", status);
        }

        g_autoptr(VdoBuffer) vdo_buf = vdo_stream_get_buffer(stream, &error);
        
        if (!vdo_buf && g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
            g_clear_error(&error);
            continue;
        }
        if (!vdo_buf) {
            return handle_vdo_failed(error);
        }

        // Get frame metadata
        VdoFrame *frame = vdo_buffer_get_frame(vdo_buf);

        // Capture timestamp (microseconds)
        gint64 pts = vdo_frame_get_timestamp(frame);

        syslog(LOG_INFO, "<6>Timestamp: %u us - Frame: %u, Size: %zu\n", (unsigned int)pts, vdo_frame_get_sequence_nbr(frame), vdo_frame_get_size(frame));
        
        // Allow VDO to reuse frame
        if (!vdo_stream_buffer_unref(stream, &vdo_buf, &error))
        {
            return handle_vdo_failed(error);
        }
        // Only successful frames count
        i += 1;
    }

    return EXIT_SUCCESS;
}