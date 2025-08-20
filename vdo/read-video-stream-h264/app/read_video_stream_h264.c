#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>

#define VDO_CLIENT_ERROR g_quark_from_static_string("vdo-client-error")
#define NFRAMES 25


static VdoStream* stream;
static gboolean shutdown       = FALSE;

static void print_frame(VdoFrame* frame) {
    if (!vdo_frame_get_is_last_buffer(frame))
        return;

    gchar* frame_type;
    switch (vdo_frame_get_frame_type(frame)) {
        case VDO_FRAME_TYPE_H264_IDR:
        case VDO_FRAME_TYPE_H264_I:
            frame_type = "I";
            break;
        case VDO_FRAME_TYPE_H264_P:
            frame_type = "P";
            break;
        case VDO_FRAME_TYPE_YUV:
            frame_type = "yuv";
            break;
        default:
            frame_type = "NA";
    }

    syslog(LOG_INFO,
           "frame = %4u, type = %s, size = %zu\n",
           vdo_frame_get_sequence_nbr(frame),
           frame_type,
           vdo_frame_get_size(frame));

}

static void handle_sigint(int signum) {
    (void)signum;
    shutdown = TRUE;
}

int main(void) {

    GError* error      = NULL;
    gchar* format      = "h264";
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

    VdoMap* settings = vdo_map_new();
    

    // Set format to H264
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_H264);

    // set format to NV12
    // vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
    //vdo_map_set_string(settings, "subformat", "NV12");

    // Set default arguments
    vdo_map_set_uint32(settings, "width", 640);
    vdo_map_set_uint32(settings, "height", 360);

    // Create a new stream
    stream = vdo_stream_new(settings, NULL, &error);
    g_clear_object(&settings);
    if (!stream)
        goto exit;

    if (!vdo_stream_attach(stream, NULL, &error))
        goto exit;

    VdoMap* info = vdo_stream_get_info(stream, &error);
    if (!info)
        goto exit;

    syslog(LOG_INFO,
           "Starting stream: %s, %ux%u, %u fps\n",
           format,
           vdo_map_get_uint32(info, "width", 0),
           vdo_map_get_uint32(info, "height", 0),
           vdo_map_get_uint32(info, "framerate", 0));

    g_clear_object(&info);

    // Start the stream
    if (!vdo_stream_start(stream, &error))
        goto exit;

    // Loop until interrupt by Ctrl-C or reaching G_MAXUINT
    for (guint n = 0; n < NFRAMES; ++n) {
        // Lifetimes of buffer and frame are linked, no need to free frame
        VdoBuffer* buffer = vdo_stream_get_buffer(stream, &error);
        VdoFrame* frame   = vdo_buffer_get_frame(buffer);

        // Error occurred
        if (!frame)
            goto exit;

        // SIGINT occurred
        if (shutdown) {
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            goto exit;
        }

        print_frame(frame);

        gpointer data = vdo_buffer_get_data(buffer);
        if (!data) {
            g_set_error(&error, VDO_CLIENT_ERROR, 0, "Failed to get data: %m");
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            goto exit;
        }

        if (!fwrite(data, vdo_frame_get_size(frame), 1, dest_f)) {
            g_set_error(&error, VDO_CLIENT_ERROR, 0, "Failed to write frame: %m");
            vdo_stream_buffer_unref(stream, &buffer, NULL);
            goto exit;
        }

        // Release the buffer and allow the server to reuse it
        if (!vdo_stream_buffer_unref(stream, &buffer, &error))
            goto exit;
    }


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

