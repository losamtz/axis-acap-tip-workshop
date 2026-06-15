#include "postprocess.h"
#include <math.h>
#include <bbox.h>

bbox_t* setup_bbox(uint32_t channel) {
    // Create box drawer for channel
    bbox_t* bbox = bbox_view_new(channel);
    if (!bbox) {
        panic("Failed to create box drawer");
    }

    bbox_clear(bbox);
    const bbox_color_t red = bbox_color_from_rgb(0xff, 0x00, 0x00);

    bbox_style_outline(bbox);   // Switch to outline style
    bbox_thickness_thin(bbox);  // Switch to thin lines
    bbox_color(bbox, red);      // Switch to red

    return bbox;
}

bool parse_and_postprocess_output_tensors(bbox_t* bbox,
                                          model_tensor_output_t* tensor_outputs,
                                          float confidence_threshold,
                                          char** labels,
                                          unsigned int* post_processing_ms) {
    box* boxes = NULL;
    struct timeval start_ts, end_ts;

    // From here this is different dependent on model
    float* locations = (float*)tensor_outputs[0].data;
    float* classes   = (float*)tensor_outputs[1].data;

    bbox_clear(bbox);

    gettimeofday(&start_ts, NULL);

    float* scores            = (float*)tensor_outputs[2].data;
    float* nbr_detections    = (float*)tensor_outputs[3].data;
    int number_of_detections = (int)nbr_detections[0];
    if (number_of_detections == 0) {
        syslog(LOG_INFO, "No object is detected");
        return true;
    }
    boxes = (box*)malloc(sizeof(box) * number_of_detections);
    for (int i = 0; i < number_of_detections; i++) {
        boxes[i].y_min = locations[4 * i];
        boxes[i].x_min = locations[4 * i + 1];
        boxes[i].y_max = locations[4 * i + 2];
        boxes[i].x_max = locations[4 * i + 3];
        boxes[i].score = scores[i];
        boxes[i].label = classes[i];
    }
    gettimeofday(&end_ts, NULL);

    *post_processing_ms = (unsigned int)(((end_ts.tv_sec - start_ts.tv_sec) * 1000) +
                                         ((end_ts.tv_usec - start_ts.tv_usec) / 1000));
    if (*post_processing_ms != 0) {
        syslog(LOG_INFO, "Postprocessing in %u ms", *post_processing_ms);
    }

    for (int i = 0; i < number_of_detections; i++) {
        if (boxes[i].score >= confidence_threshold) {
            float top    = boxes[i].y_min;
            float bottom = boxes[i].y_max;
            float right  = boxes[i].x_max;
            float left   = boxes[i].x_min;

            syslog(LOG_INFO,
                   "Object %d: Classes: %s - Scores: %f - Locations: [%f,%f,%f,%f]",
                   i,
                   labels[boxes[i].label],
                   boxes[i].score,
                   left,
                   top,
                   right,
                   bottom);
            bbox_coordinates_frame_normalized(bbox);
            bbox_rectangle(bbox, left, top, right, bottom);
        }
    }

    if (!bbox_commit(bbox, 0u)) {
        panic("Failed to commit box drawer");
    }
    if (boxes) {
        free(boxes);
    }
    return true;
}