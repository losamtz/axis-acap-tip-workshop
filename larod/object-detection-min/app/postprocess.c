#include "postprocess.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

typedef struct {
    float y_min;
    float x_min;
    float y_max;
    float x_max;
    float score;
    int label;
} box_t;

bbox_t* setup_bbox(uint32_t channel) {
    bbox_t* bbox = bbox_view_new(channel);
    if (!bbox) {
        syslog(LOG_ERR, "Failed to create box drawer");
        return NULL;
    }

    bbox_clear(bbox);
    bbox_style_outline(bbox);
    bbox_thickness_thin(bbox);
    bbox_color(bbox, bbox_color_from_rgb(0xff, 0x00, 0x00));

    return bbox;
}

bool parse_and_postprocess_output_tensors(bbox_t* bbox,
                                          output_buf_t* tensor_outputs,
                                          float confidence_threshold) {
    if (!bbox || !tensor_outputs) {
        return false;
    }

    float* locations = (float*)tensor_outputs[0].data;
    float* classes = (float*)tensor_outputs[1].data;
    float* scores = (float*)tensor_outputs[2].data;
    float* nbr_detections = (float*)tensor_outputs[3].data;

    if (!locations || !classes || !scores || !nbr_detections) {
        syslog(LOG_ERR, "Missing output tensor data");
        return false;
    }

    int number_of_detections = (int)nbr_detections[0];
    if (number_of_detections <= 0) {
        bbox_clear(bbox);
        return bbox_commit(bbox, 0u);
    }

    box_t* boxes = calloc((size_t)number_of_detections, sizeof(*boxes));
    if (!boxes) {
        syslog(LOG_ERR, "calloc boxes: %s", strerror(errno));
        return false;
    }

    for (int i = 0; i < number_of_detections; i++) {
        boxes[i].y_min = locations[4 * i];
        boxes[i].x_min = locations[4 * i + 1];
        boxes[i].y_max = locations[4 * i + 2];
        boxes[i].x_max = locations[4 * i + 3];
        boxes[i].score = scores[i];
        boxes[i].label = (int)classes[i];
    }

    bbox_clear(bbox);
    bbox_coordinates_frame_normalized(bbox);

    for (int i = 0; i < number_of_detections; i++) {
        if (boxes[i].score >= confidence_threshold) {
            syslog(LOG_INFO,
                   "Object %d: class=%d score=%f location=[%f,%f,%f,%f]",
                   i,
                   boxes[i].label,
                   boxes[i].score,
                   boxes[i].x_min,
                   boxes[i].y_min,
                   boxes[i].x_max,
                   boxes[i].y_max);
            bbox_rectangle(bbox, boxes[i].x_min, boxes[i].y_min, boxes[i].x_max, boxes[i].y_max);
        }
    }

    free(boxes);

    if (!bbox_commit(bbox, 0u)) {
        syslog(LOG_ERR, "Failed to commit box drawer");
        return false;
    }

    return true;
}
