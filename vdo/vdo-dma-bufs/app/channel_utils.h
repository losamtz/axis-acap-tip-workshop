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

unsigned int channel_util_get_image_rotation(unsigned int input_channel);
unsigned int channel_util_get_first_input_channel(void);
VdoPair32u channel_util_get_aspect_ratio(unsigned int channel_id);