#include <glib-object.h>

#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-types.h"
#include "stream.h"


/* VDO stream settings */
#define VDO_CHANNEL     1
#define VDO_NUM_BUFFERS 2
#define VDO_FRAMERATE   30.0
#define IMAGE_FIT       "crop"     /* scale or crop */

VdoStream* create_new_vdo_stream(bool rgb_backend,
                                    unsigned int* out_w,
                                    unsigned int* out_h,
                                    unsigned int* out_pitch,
                                    unsigned int* out_nbr_bufs,
                                    bool* out_is_dmabuf,
                                    VdoFormat* out_format) {

    g_autoptr(VdoMap) vdo_settings = vdo_map_new();
    g_autoptr(GError) error        = NULL;

    if (!vdo_settings) {
        panic("%s: Failed to create vdo_map", __func__);
    }

    GError* error = NULL;

    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "channel",         VDO_CHANNEL);
    vdo_map_set_uint32(settings, "buffer.count",    VDO_NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate",       VDO_FRAMERATE);
    vdo_map_set_boolean(settings, "socket.blocking", false);
    vdo_map_set_string(settings, "image.fit", IMAGE_FIT);

    VdoPair32u resolution = {
        .w = res.width,
        .h = res.height,
    };
    vdo_map_set_pair32u(vdo_settings, "resolution", resolution);
    // Make it possible to change the framerate for the stream after it is started
    vdo_map_set_boolean(vdo_settings, "dynamic.framerate", true);
    // It is not needed to set buffer.strategy since VDO_BUFFER_STRATEGY_INFINITE is default
    // vdo_map_set_uint32(vdo_settings, "buffer.strategy", VDO_BUFFER_STRATEGY_INFINITE);

    // The number of buffers that vdo will allocate for this stream
    // Normally two buffers are enough and using too many buffers will use
    // more memory in the product
    vdo_map_set_uint32(vdo_settings, "buffer.count", num_buffers);
    // The vdo_stream_get_buffer is non blocking and will return immediately
    // Then we need to poll instead when it is ok to get a buffer
    vdo_map_set_boolean(vdo_settings, "socket.blocking", false);
    vdo_map_set_string(vdo_settings, "image.fit", image_fit);

    // Create a vdo stream using the vdoMap filled in above
    g_autoptr(VdoStream) vdo_stream = vdo_stream_new(vdo_settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s", __func__, error->message);
    }
    syslog(LOG_INFO, "Dump of vdo stream settings map =====");
    vdo_map_dump(vdo_settings);

    return g_steal_pointer(&vdo_stream);
}



bool channel_util_choose_stream_resolution(unsigned int channel_id,
                                           VdoResolution req_res,
                                           VdoResolution* chosen_req,
                                           unsigned int rotation,
                                           VdoFormat* chosen_format) {
    g_autoptr(VdoResolutionSet) set     = NULL;
    g_autoptr(VdoChannel) channel       = NULL;
    g_autoptr(GError) error             = NULL;
    g_autoptr(VdoMap) resolution_filter = vdo_map_new();

    assert(chosen_format);
    assert(chosen_req);

    channel = vdo_channel_get(channel_id, &error);
    if (!channel) {
        panic("%s: Failed vdo_channel_get(): %s", __func__, error->message);
    }

    *chosen_req = req_res;

    if (rotation == 90 || rotation == 270) {
        // To be able to get the wanted resolution the resolution
        // needs to be unrotated then vdo will supply frames that have
        // the resolution img_info->width x img_info->height
        unsigned int tmp_width = req_res.width;
        chosen_req->width      = req_res.height;
        chosen_req->height     = tmp_width;
    }

    // Start to see if the supplied image format is available on this
    // product. If not default to yuv
    vdo_map_set_uint32(resolution_filter, "format", *chosen_format);
    vdo_map_set_string(resolution_filter, "select", "all");
    vdo_map_set_string(resolution_filter, "aspect_ratio", "native");

    set = vdo_channel_get_resolutions(channel, resolution_filter, &error);
    if (!set || set->count == 0) {
        // The supplied format is not supported, default to YUV
        if (set) {
            free(set);
        }
        if (*chosen_format == VDO_FORMAT_YUV) {
            panic("%s: Not possible to get any resolution from vdo for %u",
                  __func__,
                  *chosen_format);
        }
        *chosen_format = VDO_FORMAT_YUV;
        vdo_map_set_uint32(resolution_filter, "format", *chosen_format);
        set = vdo_channel_get_resolutions(channel, resolution_filter, &error);
        if (!set || set->count == 0) {
            panic("%s: Not possible to get any resolution from vdo for %u",
                  __func__,
                  *chosen_format);
        }
    }

    // Find smallest VDO stream resolution that fits the requested size.
    ssize_t best_resolution_idx       = -1;
    unsigned int best_resolution_area = UINT_MAX;
    for (ssize_t i = 0; i < (ssize_t)set->count; ++i) {
        VdoResolution* res = &set->resolutions[i];
        if ((res->width >= chosen_req->width) && (res->height >= chosen_req->height)) {
            unsigned int area = res->width * res->height;
            if (area < best_resolution_area) {
                best_resolution_idx  = i;
                best_resolution_area = area;
            }
        }
    }

    // If we got a reasonable w/h from the VDO channel info we use that
    // for creating the stream. If that info for some reason was empty we
    // fall back to trying to create a stream with client-supplied w/h.
    if (best_resolution_idx >= 0) {
        chosen_req->width  = set->resolutions[best_resolution_idx].width;
        chosen_req->height = set->resolutions[best_resolution_idx].height;
    } else {
        syslog(LOG_WARNING,
               "%s: VDO channel info contains no reslution info. Fallback "
               "to client-requested stream resolution.",
               __func__);
    }
    const char* format_str = "rgb interleaved";
    switch (*chosen_format) {
        case VDO_FORMAT_YUV:
            format_str = "yuv";
            break;
        case VDO_FORMAT_PLANAR_RGB:
            format_str = "planar rgb";
            break;
        case VDO_FORMAT_RGB:
            format_str = "rgb interleaved";
            break;
        default:
            panic("%s Unknown format %u", __func__, *chosen_format);
    }
    syslog(LOG_INFO,
           "%s: We select stream w/h=%u x %u with format %s based on VDO channel info.\n",
           __func__,
           chosen_req->width,
           chosen_req->height,
           format_str);

    return true;
}
