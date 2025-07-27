#include <bbox.h>
#include <glib-unix.h>
#include <glib.h>
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


static double xpos = 0.0;  // starting x position
static const double box_width = 0.1;
static const double y = 0.3;
static const double height = 0.1;
static int dir = -1;



__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsyslog(LOG_ERR, format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}


static gboolean update_bbox(gpointer user_data) {
    (void)user_data;

    // Draw on a multiple views - 4 (4u): 1(1u), 2(2u), 3(3u) and 4(4u)
    bbox_t* bbox = bbox_new(4u, 1u, 2u, 3u, 4u);
    if (!bbox)
        panic("Failed creating: %s", strerror(errno));

    
    bbox_coordinates_frame_normalized(bbox);

    bbox_clear(bbox);  // Remove all old bounding-boxes

    // Create all needed colors [These operations are slow!]
    //const bbox_color_t red   = bbox_color_from_rgb(0xff, 0x00, 0x00);
    //const bbox_color_t blue  = bbox_color_from_rgb(0x00, 0x00, 0xff);
    //const bbox_color_t green = bbox_color_from_rgb(0x00, 0xff, 0x00);
    const bbox_color_t yellow = bbox_color_from_rgb(0xff, 0xff, 0x00);  // Yellow

    bbox_style_corners(bbox);                      
    bbox_thickness_medium(bbox);                     
    bbox_color(bbox, yellow);                         

    xpos += dir * 0.02;

    // Change direction at bounds
    if (xpos + box_width >= 1.0) {
        xpos = 1.0 - box_width;  // clamp
        dir = -1;  // switch to left
    } else if (xpos <= 0.0) {
        xpos = 0.0;
        dir = 1;  // switch to right
    }

    bbox_rectangle(bbox, xpos, y, xpos + box_width, y + height);

    // Draw all queued geometry simultaneously
    if (!bbox_commit(bbox, 0u))
        panic("Failed committing: %s", strerror(errno));

    sleep(1);
    bbox_destroy(bbox);
    
    return G_SOURCE_CONTINUE;  // keep the timer running
}

static void clear(void) {
    // Draw on a single channel: 1
    bbox_t* bbox = bbox_new(1u, 1u);
    if (!bbox)
        panic("Failed creating: %s", strerror(errno));

    bbox_clear(bbox);  // Remove all old bounding-boxes

    // Clear everything simultaneously
    if (!bbox_commit(bbox, 0u))
        panic("Failed committing: %s", strerror(errno));

    
    sleep(1);
    bbox_destroy(bbox);
}
static gboolean signal_handler(gpointer loop) {
    g_main_loop_quit((GMainLoop*)loop);
    
    clear();
    syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");

    return G_SOURCE_REMOVE;
}

int main(void) {

    GMainLoop *loop = NULL;
    openlog(NULL, LOG_PID | LOG_CONS, LOG_USER);
    loop = g_main_loop_new(NULL, FALSE);
    // Handle SIGTERM/SIGINT
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);

    // Add update loop every 100ms
    g_timeout_add(100, update_bbox, NULL);
    

    g_main_loop_run(loop);

    clear();

    return EXIT_SUCCESS;
}
