#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>

// This is a simple consumer application that uses the VDO API to
// create a video stream, retrieve buffers, and process them.
// It sets the format to NV12 and retrieves 10 frames, processing each one.
// It is based on acap-native-sdk-samples

#define VDO_CLIENT_ERROR g_quark_from_static_string("vdo-client-error")
#define NFRAMES 10

static VdoMap* settings;
static VdoStream* stream;
static gboolean shutdown       = FALSE;

// Initialize settings for the video stream
static void setup_video_stream(void) {

    
    settings = vdo_map_new();
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);   // NV12 by default
    vdo_map_set_string(settings, "subformat", "NV12");       // Set subformat to NV12
    vdo_map_set_uint32(settings, "width",  640);
    vdo_map_set_uint32(settings, "height", 360);
}
// Create a new video stream with the specified settings
static gboolean create_stream(void) {

    GError *err = NULL;
    
    stream = vdo_stream_new(settings, NULL, &err);
    g_object_unref(settings);

    if (!stream) { 
        syslog(LOG_ERR, "Failed to create video stream: %s", err->message);
        g_clear_error(&err); 
        return FALSE; 
    }
    syslog(LOG_INFO, "Video stream created successfully.");


    VdoMap* info = vdo_stream_get_info(stream, &err);
    if (!info) {
        syslog(LOG_ERR, "Failed to get stream info: %s", err->message);
        g_clear_error(&err);
        return FALSE;
    }
        

    syslog(LOG_INFO,
           "Starting stream: %s, %ux%u, %u fps\n",
            (vdo_map_get_uint32(info, "format", 0) == 3) ? "YUV" : "",
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
// start the video stream
static gboolean start_stream(FILE *dest_f) {
    GError *err = NULL;

    if (!vdo_stream_start(stream, &err)) { 
        syslog(LOG_ERR, "Failed to start video stream: %s", err->message);
        g_clear_error(&err); 
        return FALSE; 
    }
    syslog(LOG_INFO, "Video stream started successfully.");

    // Retrieve and process 10 frames from the stream
    // This is where you would typically convert NV12 to RGB and run larod
    // For simplicity, we will just print the buffer data and frame info.
    syslog(LOG_INFO, "Retrieving and processing frames...");

    for (int i = 0; i < NFRAMES; ++i) {

        VdoBuffer *buffer = vdo_stream_get_buffer(stream, &err);
        if (!buffer) {
            syslog(LOG_ERR, "Failed to get buffer: %s", err->message);
            g_clear_error(&err);
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

        gpointer *data = vdo_buffer_get_data(buffer);
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
        syslog(LOG_INFO, "Writing to /del/null...");
        if (!vdo_stream_buffer_unref(stream, &buffer, &err)) {
            syslog(LOG_ERR, "Failed to unref buffer: %s", err->message);
            g_clear_error(&err);
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

    GError *err = NULL;
    gchar* output_file = "/dev/null";
    FILE* dest_f       = NULL;

    openlog("vdo_consumer", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "Starting VDO consumer application...");

    dest_f = fopen(output_file, "wb");
    
    if (!dest_f) {
        g_set_error(&err, VDO_CLIENT_ERROR, VDO_ERROR_IO, "open failed: %m");
        goto exit;
    }
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        g_set_error(&err, VDO_CLIENT_ERROR, VDO_ERROR_IO, "Failed to install signal handler: %m");
        goto exit;
    }

    // Set vdostream settings
    setup_video_stream();
    syslog(LOG_INFO, "Video stream settings initialized.");

    // Create a new video stream
    if (!create_stream())
        goto exit;
        
    syslog(LOG_INFO, "Video stream created successfully.");

    // Start the video stream
    if(!start_stream(dest_f))
        goto exit;

    exit:
    // Ignore SIGINT and server maintenance
    if (shutdown || vdo_error_is_expected(&err))
        g_clear_error(&err);

    gint ret = EXIT_SUCCESS;
    if (err) {
        syslog(LOG_INFO, "read-video-stream: %s\n", err->message);
        ret = EXIT_FAILURE;
    }
    syslog(LOG_INFO, "Exiting VDO consumer application...");

    g_clear_error(&err);
    g_clear_object(&stream);

    return ret;  
} 


