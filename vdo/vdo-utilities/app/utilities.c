
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



#define VDO_CHANNEL 1u


void create_stream(image_t *image) {

    VdoMap* settings = vdo_map_new();
    GError* error   = NULL;

    // set format to NV12
    vdo_map_set_uint32(settings, "format", image->vdo_format);
    vdo_map_set_string(settings, "subformat", "nv12");

    // Set default arguments
    vdo_map_set_uint32(settings, "width", image->width);
    vdo_map_set_uint32(settings, "height", image->height);

    VdoStream* vdo_stream = vdo_stream_new(settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s",
              __func__,
              (error != NULL) ? error->message : "N/A");
    }

    image->vdo_stream = vdo_stream;
    syslog(LOG_INFO, "Stream  created ...");

    // Always do this
    g_object_unref(settings);
    g_clear_error(&error);


}


image_t* create_image(unsigned int w, unsigned int h,  VdoFormat format){

    image_t* image = calloc(1, sizeof(image_t));
    if (!image) {
        panic("%s: Unable to allocate Image structure: %s", __func__, strerror(errno));
    }

    image->vdo_format     = format;
    image->width          = w;
    image->height         = h;

    syslog(LOG_INFO, "Image format= %s", ((unsigned)format == 3) ? "yuv" : "");

    
    return image;
}

// start the video stream
gboolean start_stream(image_t *image) {
    GError *err = NULL;

    if (!vdo_stream_start(image->vdo_stream, &err)) { 
        syslog(LOG_ERR, "Failed to start video stream: %s", err->message);
        g_clear_error(&err); 
        return FALSE; 
    }
    syslog(LOG_INFO, "Video stream started successfully.");
    return TRUE;
    
}

void get_stream_resolutions(void) {

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

void get_stream_rotation(image_t *image) {
    GError* error = NULL;

    g_autoptr(VdoMap) info = vdo_stream_get_info(image->vdo_stream, &error);
    if (error) {
        panic("%s: vdo_stream_get_info failed: %s", __func__, error->message);
    }
    syslog(LOG_INFO, "Current stream rotation: %u", vdo_map_get_uint32(info, "rotation", 0));
    
}
void get_video_channels(void) {

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