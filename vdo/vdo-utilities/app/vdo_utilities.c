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


int main(void) {

        openlog(NULL, LOG_PID, LOG_USER);

        // 1- List channels & resolutions 
        get_video_channels();
        get_stream_resolutions();
        
        // 2 -create a stream (NV12) 
        image_t *img = create_image(WITH, HEIGHT, VDO_FORMAT_YUV);
        
        // 3 - create stream
        create_stream(img);

        // 4 - get rotation
        get_stream_rotation(img);

        if (img && img->vdo_stream) {
            g_object_unref(img->vdo_stream);
        }
        g_free(img);

        syslog(LOG_INFO, "Exiting cleanly.");
        closelog();
        return EXIT_SUCCESS;
}

