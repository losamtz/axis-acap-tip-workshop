#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"
#include <vdo-channel.h>
#include "panic.h"
#include "utilities.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <gmodule.h>

#define WITH 640
#define HEIGHT 360

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

int main(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoStream) vdo_stream = NULL;

    openlog(NULL, LOG_PID, LOG_USER);

    // 1- List channels & resolutions 
    get_video_channels();
    get_filtered_channels();
    get_stream_resolutions();
          
    // 3 - create stream without starting it to show how to retrieve stream information such as rotation. No need to start stream for that.
    vdo_stream = vdo_stream_rgb_new(NULL, 1u, (VdoResolution){ .width = WITH, .height = HEIGHT }, &error);

    if (!vdo_stream) {
        return handle_vdo_failed(error);
    }   

    // 4 - get rotation
    get_stream_rotation(vdo_stream);

    if (vdo_stream) {
        g_object_unref(vdo_stream);
    }
    

    syslog(LOG_INFO, "Exiting cleanly.");
    closelog();
    return EXIT_SUCCESS;
}

