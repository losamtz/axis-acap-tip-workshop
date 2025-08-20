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
           "Starting stream: %u, %ux%u, %u fps\n",
            vdo_map_get_uint32(info, "format", 0),
            vdo_map_get_uint32(info, "width", 0),
            vdo_map_get_uint32(info, "height", 0),
            vdo_map_get_uint32(info, "framerate", 0));

    g_clear_object(&info);
    return TRUE;
}

// start the video stream
static gboolean start_stream(void) {
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

    for (int i = 0; i < 10; ++i) {

        VdoBuffer *buffer = vdo_stream_get_buffer(stream, &err);
        void *yuv = vdo_buffer_get_data(buffer);
        VdoFrame *frame = vdo_buffer_get_frame(buffer);
        (void)yuv; (void)frame; // convert NV12 → RGB and run larod here
        g_object_unref(buffer); // return buffer to VDO
    }

    return TRUE;
}
int main(void) {

    GError *err = NULL;

    openlog("vdo_consumer", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "Starting VDO consumer application...");

    // Set vdostream settings
    setup_video_stream();
    syslog(LOG_INFO, "Video stream settings initialized.");

    // Create a new video stream
    if (!create_stream())
        goto exit;
        
    syslog(LOG_INFO, "Video stream created successfully.");

    // Start the video stream
    if(!start_stream())
        goto exit;

    syslog(LOG_INFO, "Video stream started successfully.");

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


