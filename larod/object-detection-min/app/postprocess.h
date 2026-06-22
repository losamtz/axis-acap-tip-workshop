#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <bbox.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    void* data;
    size_t size;
} output_buf_t;

bbox_t* setup_bbox(uint32_t channel);
bool parse_and_postprocess_output_tensors(bbox_t* bbox,
                                          output_buf_t* tensor_outputs,
                                          float confidence_threshold);

#endif
