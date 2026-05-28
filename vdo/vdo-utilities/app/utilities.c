
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



void get_stream_resolutions(void) {

    VdoResolutionSet* set = NULL;
    VdoChannel* channel   = NULL;
    GError* error         = NULL;

    channel = vdo_channel_get(1u, &error);

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

void get_stream_rotation(VdoStream *stream) {
    GError* error = NULL;

    g_autoptr(VdoMap) info = vdo_stream_get_info(stream, &error);
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
int get_filtered_channels(void) {
    GError *error = NULL;

    // 2) List channels with specific filter (e.g. only video channels)
    VdoMap *filter = vdo_map_new();
    vdo_map_set_string(filter, "key", "input");

    GList *filtered_channels = vdo_channel_get_filtered(filter, &error);
    if (!filtered_channels)
        panic("%s: Failed finding channels: %s", __func__, (error != NULL) ? error->message : "N/A");

    syslog(LOG_INFO, "Getting filtered video channels...");

    for (GList *list = filtered_channels; list; list = list->next) {
        VdoChannel *channel = list->data;

        g_autoptr(VdoMap) info = vdo_channel_get_info(channel, &error);
        if (!info) {
            syslog(LOG_ERR, "Failed to get channel info: %s", error->message);
            return EXIT_FAILURE;
        }
        guint id = vdo_channel_get_id(channel);
        guint input_id = vdo_map_get_uint32(info, "type.id", 0);
        syslog(LOG_INFO, "Channel id: %u => Input id: %u", id, input_id);
    }
    g_list_free_full(filtered_channels, (GDestroyNotify)g_object_unref);
    g_object_unref(filter);
    return EXIT_SUCCESS;
}