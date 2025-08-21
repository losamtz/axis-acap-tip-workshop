/**
 * Copyright (C) 2025, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * - object_detection_bbox_yolov5 -
 *
 * This application loads a larod YOLOv5 model which takes an image as input. The output is
 * YOLOv5-specifically parsed to retrieve values corresponding to the class, score and location of
 * detected objects in the image.
 *
 * The application expects two arguments on the command line in the
 * following order: MODELFILE LABELSFILE.
 *
 * First argument, MODELFILE, is a string describing path to the model.
 *
 * Second argument, LABELSFILE, is a string describing path to the label txt.
 *
 */


#include "imageprovider.h"
#include "panic.h"

#include "vdo-frame.h"
#include "vdo-types.h"
#include <bbox.h>

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <syslog.h>

#define APP_NAME "vdo_producer"

#define MODEL_INPUT_W 640
#define MODEL_INPUT_H 640

volatile sig_atomic_t running = 1;

static void shutdown(int status) {
    (void)status;
    running = 0;
}

typedef struct { 
  float x, y, w, h, score; 
  int label; 
} sim_det_t;


static bbox_t* setup_bbox(void) {
    // Create box drawers
    bbox_t* bbox = bbox_view_new(1u);
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
static void find_corners(float x,
                         float y,
                         float w,
                         float h,
                         int rotation,
                         float* x1,
                         float* y1,
                         float* x2,
                         float* y2) {
    switch (rotation) {
        case 180:
            *x1 = 1.0 - fmax(0.0, x - (w / 2));
            *y1 = 1.0 - fmax(0.0, y - (h / 2));
            *x2 = 1.0 - fmin(1.0, x + (w / 2));
            *y2 = 1.0 - fmin(1.0, y + (h / 2));
            break;
        default:
            *x1 = fmax(0.0, x - (w / 2));
            *y1 = fmax(0.0, y - (h / 2));
            *x2 = fmin(1.0, x + (w / 2));
            *y2 = fmin(1.0, y + (h / 2));
            break;
    }
}

static int simulate_detections(unsigned long frame_idx, sim_det_t* out, int max_out) {
    if (!out || max_out <= 0) return 0;
    int n = (max_out < 3) ? max_out : 3;
    for (int i = 0; i < n; ++i) {
        float phase = fmodf((float)frame_idx * (0.01f + 0.02f*i), 1.0f);
        out[i].x = 0.15f + 0.70f * phase;
        out[i].y = 0.30f + 0.25f * sinf(6.2831853f * phase + i);
        out[i].w = 0.20f - 0.04f * i;
        out[i].h = 0.28f - 0.05f * i;
        out[i].score = 0.85f - 0.10f * i;
        out[i].label = i;
        if (out[i].w < 0.02f) out[i].w = 0.02f;
        if (out[i].h < 0.02f) out[i].h = 0.02f;
        if (out[i].x < 0) out[i].x = 0; if (out[i].x > 1) out[i].x = 1;
        if (out[i].y < 0) out[i].y = 0; if (out[i].y > 1) out[i].y = 1;
    }
    return n;
}


int main(int argc, char** argv) {
    openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_USER);

    // Stop main loop at signal
    signal(SIGTERM, shutdown);
    signal(SIGINT, shutdown);

    unsigned int stream_width  = 0;
    unsigned int stream_height = 0;

    // Choose a valid stream resolution since only certain resolutions are allowed
    choose_stream_resolution(MODEL_INPUT_W, MODEL_INPUT_H, &stream_width, &stream_height);

    // Create an image provider
    img_provider_t* image_provider = create_img_provider(stream_width, stream_height, 2, VDO_FORMAT_YUV);
    syslog(LOG_INFO, "Start fetching video frames from VDO");

    start_frame_fetch(image_provider);

    bbox_t* bbox = setup_bbox();

    unsigned long frame_idx = 0;
    // int valid_detection_count = 0;

    while (running) {
        
        // Get latest frame from image pipeline.
        VdoBuffer* buf = get_last_frame_blocking(image_provider);
        if (!buf) {
            panic("Buffer empty in provider");
        }

        // Get data from latest frame.  We don't need to read pixels; only overlay boxes.
        uint8_t* nv12_data = (uint8_t*)vdo_buffer_get_data(buf);
        (void)nv12_data;

        sim_det_t dets[8];
        int ndets = simulate_detections(frame_idx++, dets, 8);

        bbox_clear(bbox);

        int valid_detection_count = 0;
        for (int i = 0; i < ndets; ++i) {

            float x1, y1, x2, y2;
            int rotation = (int)get_stream_rotation(image_provider);
            find_corners(dets[i].x, dets[i].y, dets[i].w, dets[i].h, rotation, &x1, &y1, &x2, &y2);

            // Log info about object
            syslog(LOG_INFO, "Object detected!, "
                  "Bounding Box: [%.2f, %.2f, %.2f, %.2f]", x1, y1, x2, y2);

            bbox_rectangle(bbox, x1, y1, x2, y2);
        }

        if (!bbox_commit(bbox, 0u)) {
            panic("Failed to commit box drawer");
        }
        // Release frame reference to provider.
        return_frame(image_provider, buf);
    }

    syslog(LOG_INFO, "Stop streaming video from VDO");
    stop_frame_fetch(image_provider);

    // Cleanup
    if (image_provider) {
        destroy_img_provider(image_provider);
    }
    bbox_destroy(bbox);

    syslog(LOG_INFO, "Exit %s", argv[0]);

    return 0;
}