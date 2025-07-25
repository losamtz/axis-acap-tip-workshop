#include <bbox.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

// Select scene or frame normalized coordinates.
//
// Scene coordinate system is normalized to [0,0]-[1,1] and follows the filmed scene,
// i.e. static objects in the world have the same coordinates regardless of global rotation.
//
// Frame coordinate system is normalized and aligned with the camera frame,
// i.e. top-left is [0,0] and bottom-right is [1,1].
static bool scene_normalized = true;

volatile sig_atomic_t running = 1;

static void shutdown(int status) {
    (void)status;
    running = 0;
}

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsyslog(LOG_ERR, format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

// This example illustrates drawing on a single channel.
// The coordinate-space equals the visible area of the chosen channel.
//
//    ┏━━━━━━━━━━━━━━━━━━━━━━━━┓
//    ┃                        ┃
//    ┃ [0,0]                  ┃
//    ┃   ┏━━━━━━━━━━┓         ┃
//    ┃   ┃          ┃         ┃
//    ┃   ┃ Channel1 ┃         ┃
//    ┃   ┃          ┃         ┃
//    ┃   ┗━━━━━━━━━━┛         ┃
//    ┃            [1,1]       ┃
//    ┃                        ┃
//    ┃                        ┃
//    ┗━━━━━━━━━━━━━━━━━━━━━━━━┛
//
// The intended usecase is performing video content analytics on one channel,
// then draw bounding boxes with the same coordinate-space as was used for
// Video Content Analytics (VCA).
static void single_channel(void) {
    // Draw on a single view: 1
    bbox_t* bbox = bbox_view_new(1u);
    if (!bbox)
        panic("Failed creating: %s", strerror(errno));

    // if (scene_normalized)
        //bbox_coordinates_scene_normalized(bbox);
    // else
    bbox_coordinates_frame_normalized(bbox);

    bbox_clear(bbox);  // Remove all old bounding-boxes

    // Create all needed colors [These operations are slow!]
    const bbox_color_t red   = bbox_color_from_rgb(0xff, 0x00, 0x00);
    const bbox_color_t blue  = bbox_color_from_rgb(0x00, 0x00, 0xff);
    const bbox_color_t green = bbox_color_from_rgb(0x00, 0xff, 0x00);

    bbox_style_outline(bbox);                      // Switch to outline style
    bbox_thickness_thin(bbox);                     // Switch to thin lines
    bbox_color(bbox, red);                         // Switch to red [This operation is fast!]
    bbox_rectangle(bbox, 0.05, 0.05, 0.95, 0.95);  // Draw a thin red outline rectangle

    // bbox_style_corners(bbox);                      // Switch to corners style
    // bbox_thickness_thick(bbox);                    // Switch to thick lines
    // bbox_color(bbox, blue);                        // Switch to blue [This operation is fast!]
    // bbox_rectangle(bbox, 0.40, 0.40, 0.60, 0.60);  // Draw thick blue corners

    // bbox_style_corners(bbox);                      // Switch to corners style
    // bbox_thickness_medium(bbox);                   // Switch to medium lines
    // bbox_color(bbox, blue);                        // Switch to blue [This operation is fast!]
    // bbox_rectangle(bbox, 0.30, 0.30, 0.50, 0.50);  // Draw medium blue corners

    // bbox_style_outline(bbox);   // Switch to outline style
    // bbox_thickness_thin(bbox);  // Switch to thin lines
    // bbox_color(bbox, red);      // Switch to red [This operation is fast!]

    // // Draw a thin red quadrilateral
    // bbox_quad(bbox, 0.10, 0.10, 0.30, 0.12, 0.28, 0.28, 0.11, 0.30);

    // // Draw a green polyline
    // bbox_color(bbox, green);  // Switch to green [This operation is fast!]
    // bbox_move_to(bbox, 0.2, 0.2);
    // bbox_line_to(bbox, 0.5, 0.5);
    // bbox_line_to(bbox, 0.8, 0.4);
    // bbox_draw_path(bbox);

    // Draw all queued geometry simultaneously
    if (!bbox_commit(bbox, 0u))
        panic("Failed committing: %s", strerror(errno));

    // if (running)
    //     sleep(5);
    bbox_destroy(bbox);
}


// static void example_clear(void) {
//     // Draw on a single channel: 1
//     bbox_t* bbox = bbox_new(1u, 1u);
//     if (!bbox)
//         panic("Failed creating: %s", strerror(errno));

//     bbox_clear(bbox);  // Remove all old bounding-boxes

//     // Clear everything simultaneously
//     if (!bbox_commit(bbox, 0u))
//         panic("Failed committing: %s", strerror(errno));

//     if (running)
//         sleep(5);
//     bbox_destroy(bbox);
// }

static void init_signals(void) {
    const struct sigaction sa = {
        .sa_handler = shutdown,
    };

    if (sigaction(SIGINT, &sa, NULL) < 0)
        panic("Failed installing SIGINT handler: %s", strerror(errno));

    if (sigaction(SIGTERM, &sa, NULL) < 0)
        panic("Failed installing SIGTERM handler: %s", strerror(errno));
}

int main(void) {
    openlog(NULL, LOG_PID, LOG_USER);

    init_signals();

    single_channel();

    

    return EXIT_SUCCESS;
}