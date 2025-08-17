#include <bbox.h>
#include <glib-unix.h>
#include <glib.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* ---------------- Animation state ---------------- */
static double xpos = 0.0;           // current left x (normalized 0..1)
static const double box_width = 0.1;
static const double y = 0.3;
static const double height = 0.1;
static int dir = -1;                // -1 left, +1 right

/* --------------- Frame timing (FPS) --------------- */
/* ~30 FPS → 33 ms; 10 FPS → 100 ms */
#define TICK_MS 33

/* --------------- Global resources ---------------- */
static GMainLoop* loop = NULL;
static bbox_t* g_bbox = NULL;       // persistent bbox handle

/* -------------------- Utils ---------------------- */
__attribute__((noreturn)) __attribute__((format(printf, 1, 2)))
static void panic(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_ERR, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

/* Clear everything we’ve drawn, using the persistent handle */
static void clear_all(void) {
    if (!g_bbox) return;

    if (!bbox_clear(g_bbox))
        syslog(LOG_ERR, "bbox_clear failed: %s", strerror(errno));

    if (!bbox_commit(g_bbox, 0u))
        syslog(LOG_ERR, "bbox_commit (clear) failed: %s", strerror(errno));
}

/* ----------------- Animation tick ---------------- */
static gboolean update_bbox(gpointer user_data) {
    (void)user_data;

    if (!g_bbox) return G_SOURCE_CONTINUE;

    // Enable OSD (no-op if already enabled)
    if (!bbox_video_output(g_bbox, true))
        panic("Failed enabling video-output: %s", strerror(errno));

    // Clear previous frame
    if (!bbox_clear(g_bbox))
        panic("bbox_clear failed: %s", strerror(errno));

    // Style & color (set once per frame; consider caching if constant)
    bbox_style_corners(g_bbox);
    bbox_thickness_medium(g_bbox);
    const bbox_color_t yellow = bbox_color_from_rgb(0xff, 0xff, 0x00);
    bbox_color(g_bbox, yellow);

    // Update horizontal position
    xpos += dir * 0.02;
    if (xpos + box_width >= 1.0) {
        xpos = 1.0 - box_width;
        dir = -1;
    } else if (xpos <= 0.0) {
        xpos = 0.0;
        dir = 1;
    }

    // Draw rectangle in normalized coords
    bbox_rectangle(g_bbox, xpos, y, xpos + box_width, y + height);

    // Present this frame atomically on all targeted views
    if (!bbox_commit(g_bbox, 0u))
        panic("bbox_commit failed: %s", strerror(errno));

    // IMPORTANT: no sleep(). The GLib timer cadence controls FPS.
    return G_SOURCE_CONTINUE; // keep timer running
}

/* ----------------- Signal handler ---------------- */
static gboolean sig_handler(gpointer data) {
    (void)data;
    g_main_loop_quit(loop);
    syslog(LOG_INFO, "Stopping (SIGTERM/SIGINT).");
    return G_SOURCE_REMOVE;
}

/* ---------------------- main --------------------- */
int main(void) {
    openlog(NULL, LOG_PID | LOG_CONS, LOG_USER);

    // Create main loop
    loop = g_main_loop_new(NULL, FALSE);
    if (!loop) panic("Failed to create GMainLoop");

    // Handle signals
    g_unix_signal_add(SIGTERM, sig_handler, NULL);
    g_unix_signal_add(SIGINT, sig_handler, NULL);

    // Create a persistent bbox handle once.
    // Here we target 4 views (1..4). Adjust to your device layout.
    g_bbox = bbox_new(4u, 1u, 2u, 3u, 4u);
    if (!g_bbox) panic("bbox_new failed: %s", strerror(errno));

    // Optional: choose normalized coordinate space (uncomment if needed)
    // bbox_coordinates_frame_normalized(g_bbox);
    // or bbox_coordinates_scene_normalized(g_bbox);

    // Prime the OSD
    if (!bbox_video_output(g_bbox, true))
        panic("Failed enabling video-output: %s", strerror(errno));

    // Start animation timer (no blocking sleeps inside update loop)
    g_timeout_add(TICK_MS, update_bbox, NULL);

    // Run
    g_main_loop_run(loop);

    // Shutdown: clear what we drew and destroy resources
    clear_all();

    if (g_bbox) {
        bbox_destroy(g_bbox);
        g_bbox = NULL;
    }

    if (loop) {
        g_main_loop_unref(loop);
        loop = NULL;
    }

    return EXIT_SUCCESS;
}
