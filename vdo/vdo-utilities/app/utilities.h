#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"
#include <vdo-channel.h>



typedef struct image {
    VdoFormat vdo_format;
    VdoStream* vdo_stream;
    unsigned int width;
    unsigned int height;
} image_t;


image_t* create_image(unsigned int w, unsigned int h,  VdoFormat format);
void create_stream(image_t *image);
gboolean start_stream(image_t *image);
void get_stream_resolutions(void);
void get_stream_rotation(image_t *image);
void get_video_channels(void);