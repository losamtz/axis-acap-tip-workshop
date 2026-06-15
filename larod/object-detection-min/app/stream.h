#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "vdo-error.h"
#include "vdo-stream.h"
#include "vdo-types.h"

bool channel_util_choose_stream_resolution(unsigned int channel,
                                           VdoResolution req_res,
                                           VdoResolution* chosen_req,
                                           unsigned int rotation,
                                           VdoFormat* chosen_format);


VdoStream* create_new_vdo_stream(unsigned int channel,
                                        VdoFormat format,
                                        VdoResolution res,
                                        unsigned int num_buffers,
                                        const char* image_fit,
                                        double framerate);