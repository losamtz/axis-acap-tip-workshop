#pragma once
#include <vdo-stream.h>
#include <vdo-types.h>
VdoStream* stream_create(unsigned int channel, VdoFormat format, VdoResolution resolution,
                         GError** error);
