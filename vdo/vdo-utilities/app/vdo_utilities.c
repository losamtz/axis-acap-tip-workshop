#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"
#include <vdo-channel.h>
#include "panic.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <gmodule.h>


#define VDO_CHANNEL 1u

static volatile gboolean g_shutdown = FALSE;

typedef struct image {
    VdoFormat vdo_format;
    VdoStream* vdo_stream;
} image_t;

static gboolean on_unix_signal(gpointer user_data) {
    GMainLoop *loop = user_data;
    g_shutdown = TRUE;
    if (loop) g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void create_stream(image_t *image, unsigned int w, unsigned int h) {

    VdoMap* settings = vdo_map_new();
    GError* error   = NULL;

    // set format to NV12
    vdo_map_set_uint32(settings, "format", image->vdo_format);
    vdo_map_set_string(settings, "subformat", "nv12");

    // Set default arguments
    vdo_map_set_uint32(settings, "width", w);
    vdo_map_set_uint32(settings, "height", h);

    VdoStream* vdo_stream = vdo_stream_new(settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s",
              __func__,
              (error != NULL) ? error->message : "N/A");
    }

    // Start the actual VDO streaming.
    if (!vdo_stream_start(vdo_stream, &error)) {
        panic("%s: Failed starting stream: %s", __func__, (error != NULL) ? error->message : "N/A");
    }

    image->vdo_stream = vdo_stream;
    syslog(LOG_INFO, "Stream created...");

    // Always do this
    g_object_unref(settings);
    g_clear_error(&error);


}


static image_t* create_image(unsigned int w, unsigned int h,  VdoFormat format){

    image_t* image = calloc(1, sizeof(image_t));
    if (!image) {
        panic("%s: Unable to allocate Image structure: %s", __func__, strerror(errno));
    }

    image->vdo_format     = format;
    

    syslog(LOG_INFO, "Image format=%u", (unsigned)format);

    create_stream(image, w, h);
    return image;
}


static void get_stream_resolutions(void) {

    VdoResolutionSet* set = NULL;
    VdoChannel* channel   = NULL;
    GError* error         = NULL;

    channel = vdo_channel_get(VDO_CHANNEL, &error);

    if (!channel) {
        panic("%s: Failed vdo_channel_get(): %s",
              __func__,
              (error != NULL) ? error->message : "N/A");
    }

    syslog(LOG_INFO, "Getting stream resolutions...");

    // Only retrieve resolutions with native aspect ratio
    VdoMap* map = vdo_map_new();
    vdo_map_set_string(map, "aspect_ratio", "native");

    // Retrieve channel resolutions
    set = vdo_channel_get_resolutions(channel, map, &error);
    if (!set) {
        panic("%s: Failed vdo_channel_get_resolutions(): %s",
              __func__,
              (error != NULL) ? error->message : "N/A");
    }

    for (size_t i = 0; i < set->count; ++i) {
        VdoResolution* res = &set->resolutions[i];
        syslog(LOG_INFO, "  [%zu] %ux%u", (size_t)i, (unsigned)res->width, (unsigned)res->height);       
    }

    g_object_unref(map);
    g_clear_object(&channel);
    g_free(set);
    g_clear_error(&error);
}

static guint32 get_stream_rotation(image_t* provider) {
    GError* error = NULL;

    g_autoptr(VdoMap) info = vdo_stream_get_info(provider->vdo_stream, &error);
    if (error) {
        panic("%s: vdo_stream_get_info failed: %s", __func__, error->message);
    }

    return vdo_map_get_uint32(info, "rotation", 0);
}

static void get_video_channels(void) {

    GError *error = NULL;

    // 1) List all channels
    GList *channels = vdo_channel_get_all(&error);
    if (!channels)
        panic("%s: Failed finding channels: %s", __func__, (error != NULL) ? error->message : "N/A");

    syslog(LOG_INFO, "Getting video channels...");

    for (GList *list = channels; list; list = list->next) {
        VdoChannel *channel = list->data;
        guint id = vdo_channel_get_id(channel);
        syslog(LOG_INFO, "Channel id: %u => Camera number: %u", id, id + 1);
    }
    g_list_free_full(channels, (GDestroyNotify)g_object_unref);
}



int main(void) {

    //GError* error      = NULL;
    unsigned int stream_width  = 640;
    unsigned int stream_height = 360;

    openlog(NULL, LOG_PID, LOG_USER);

    // 1- ist channels & resolutions 
    get_video_channels();
    get_stream_resolutions();
    
    // 2 -create/start a stream (NV12) 
    image_t *img = create_image(stream_width, stream_height, VDO_FORMAT_YUV);

    guint32 rotation = get_stream_rotation(img);
    syslog(LOG_INFO, "Current stream rotation: %u", rotation);

    //3 - GLib main loop + signal handlers for clean shutdown */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT,  (GSourceFunc)on_unix_signal, loop);
    g_unix_signal_add(SIGTERM, (GSourceFunc)on_unix_signal, loop);

    syslog(LOG_INFO, "Running. Press Ctrl+C to stop.");
    g_main_loop_run(loop);

    // 4 - teardown 
    if (img && img->vdo_stream) {
        vdo_stream_stop(img->vdo_stream);
        g_object_unref(img->vdo_stream);
    }
    g_free(img);
    g_main_loop_unref(loop);

    syslog(LOG_INFO, "Exiting cleanly.");
    closelog();
    return EXIT_SUCCESS;
}

