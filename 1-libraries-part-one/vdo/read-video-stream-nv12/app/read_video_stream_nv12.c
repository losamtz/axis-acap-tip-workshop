#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>

#define VDO_CLIENT_ERROR g_quark_from_static_string("vdo-client-error")
#define NFRAMES 25

static VdoMap* settings;
static VdoStream* stream;
static gboolean shutdown       = FALSE;


// Setup stream 
static void setup_video_stream(void) {

    settings = vdo_map_new();
    
    // set format to NV12
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
    vdo_map_set_string(settings, "subformat", "NV12");

    // Set default arguments
    vdo_map_set_uint32(settings, "width", 640);
    vdo_map_set_uint32(settings, "height", 360);

}
static gboolean create_stream(const char *format) {
    GError *error = NULL;

    stream = vdo_stream_new(settings, NULL, &error);
    g_clear_object(&settings);
    if (!stream) {
        syslog(LOG_ERR, "Failed to create stream: %s", error->message);
        g_clear_error(&error);
        return FALSE;
    }
        

    VdoMap* info = vdo_stream_get_info(stream, &error);
    if (!info) {
        syslog(LOG_ERR, "Failed to get stream info: %s", error->message);
        g_clear_error(&error);
        return FALSE;
    }
        

    syslog(LOG_INFO,
           "Starting stream: %s, %ux%u, %u fps\n",
           format,
           vdo_map_get_uint32(info, "width", 0),
           vdo_map_get_uint32(info, "height", 0),
           vdo_map_get_uint32(info, "framerate", 0));

    g_clear_object(&info);
    
    return TRUE;

}
static void print_frame(VdoFrame* frame) {
    if (!vdo_frame_get_is_last_buffer(frame))
        return;

    gchar* frame_type;
    if(vdo_frame_get_frame_type(frame) == VDO_FRAME_TYPE_YUV) 
        frame_type = "yuv";
    else
        frame_type = "NA";
    

    syslog(LOG_INFO,
           "frame = %4u, type = %s, size = %zu\n",
           vdo_frame_get_sequence_nbr(frame),
           frame_type,
           vdo_frame_get_size(frame));

}
static gboolean start_stream(FILE *dest_f) {

    GError *error = NULL;

    if (!vdo_stream_start(stream, &error)) {
        syslog(LOG_ERR, "Failed to start stream: %s", error->message);
        g_clear_error(&error);
        return FALSE;
    }

    for (guint n = 0; n < NFRAMES; ++n) {
        VdoBuffer *buffer = vdo_stream_get_buffer(stream, &error);
        if (!buffer) {
            syslog(LOG_ERR, "Failed to get buffer: %s", error->message);
            g_clear_error(&error);
            return FALSE;
        }

        VdoFrame *frame = vdo_buffer_get_frame(buffer);
        if (!frame) {
            syslog(LOG_ERR, "No frame in buffer");
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            return FALSE;
        }

        if (shutdown) {
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            return FALSE;
        }

        print_frame(frame);

        gpointer data = vdo_buffer_get_data(buffer);
        if (!data) {
            syslog(LOG_ERR, "Failed to get buffer data");
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            return FALSE;
        }

        if (!fwrite(data, vdo_frame_get_size(frame), 1, dest_f)) {
            syslog(LOG_ERR, "Failed to write frame to file: %m");
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            return FALSE;
        }

        if (!vdo_stream_buffer_unref(stream, &buffer, &error)) {
            syslog(LOG_ERR, "Failed to unref buffer: %s", error->message);
            g_clear_error(&error);
            return FALSE;
        }
    }

    return TRUE;

}


static void handle_sigint(int signum) {
    (void)signum;
    shutdown = TRUE;
}

int main(void) {

    GError* error      = NULL;
    gchar* format      = "nv12";
    gchar* output_file = "/dev/null";
    FILE* dest_f       = NULL;

    openlog(NULL, LOG_PID, LOG_USER);

    dest_f = fopen(output_file, "wb");
    
    if (!dest_f) {
        g_set_error(&error, VDO_CLIENT_ERROR, VDO_ERROR_IO, "open failed: %m");
        goto exit;
    }
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        g_set_error(&error, VDO_CLIENT_ERROR, VDO_ERROR_IO, "Failed to install signal handler: %m");
        goto exit;
    }
    // Set vdostream settings
    setup_video_stream();
        
    // Create stream
    if(!create_stream(format))
        goto exit;
    

    // Start the stream
    if(!start_stream(dest_f))
        goto exit;

  exit:
    // Ignore SIGINT and server maintenance
    if (shutdown || vdo_error_is_expected(&error))
        g_clear_error(&error);

    gint ret = EXIT_SUCCESS;
    if (error) {
        syslog(LOG_INFO, "read-video-stream: %s\n", error->message);
        ret = EXIT_FAILURE;
    }

    if (dest_f)
        fclose(dest_f);

    g_clear_error(&error);
    g_clear_object(&stream);


    return ret;  
}

