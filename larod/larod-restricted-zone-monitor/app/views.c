#include "views.h"
#include <stdbool.h>
#include <vdo-channel.h>
#include <vdo-map.h>

GArray* views_discover(GError** error) {
    VdoMap* info = vdo_channel_get_info(VDO_CHANNEL_GLOBAL, error);
    if (!info) {
        return NULL;
    }
    guint count = vdo_map_get_uint32(info, "view.count", 0);
    g_object_unref(info);
    GArray* views = g_array_new(FALSE, FALSE, sizeof(CameraView));
    for (guint view = 1; view <= count; view++) {
        VdoMap* description = vdo_map_new();
        vdo_map_set_uint32(description, "view", view);
        GError* view_error = NULL;
        VdoChannel* channel = vdo_channel_get_ex(description, &view_error);
        g_object_unref(description);
        if (channel) {
            CameraView entry = {.view = view, .channel = vdo_channel_get_id(channel)};
            g_array_append_val(views, entry);
            g_object_unref(channel);
        }
        g_clear_error(&view_error);
    }
    return views;
}

bool views_find(const GArray* views, unsigned int view, unsigned int* channel) {
    if (views) {
        for (guint index = 0; index < views->len; index++) {
            const CameraView* entry = &g_array_index(views, CameraView, index);
            if (entry->view == view) {
                *channel = entry->channel;
                return true;
            }
        }
    }
    return false;
}
