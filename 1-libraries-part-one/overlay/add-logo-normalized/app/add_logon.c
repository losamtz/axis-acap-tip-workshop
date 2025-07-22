/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
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
 * - axoverlay -
 *
 * This application demonstrates how the use the API axoverlay, by drawing
 * plain boxes using 4-bit palette color format and text overlay using
 * ARGB32 color format.
 *
 * Colorspace and alignment:
 * 1-bit palette (AXOVERLAY_COLORSPACE_1BIT_PALETTE): 32-byte alignment
 * 4-bit palette (AXOVERLAY_COLORSPACE_4BIT_PALETTE): 16-byte alignment
 * ARGB32 (AXOVERLAY_COLORSPACE_ARGB32): 16-byte alignment
 *
 */

#include <axoverlay.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdlib.h>
#include <syslog.h>


static gint overlay_id = -1;

/**
 * brief Add a logo using axoverlay and cairo.
 *
 *
 * param context Cairo rendering context.
 * param left Left coordinate (x1).
 * param top Top coordinate (y1).
 * param right Right coordinate (x2).
 * param bottom Bottom coordinate (y2).
 * param color_index Palette color index.
 * param line_width Rectange line width.
 */
static void draw_logo(cairo_t *context,
                      const char *image_path,
                      gfloat pad_x, gfloat pad_y,       // Normalized padding from edge
                      gfloat norm_width,                // Max normalized width
                      gint overlay_width,
                      gint overlay_height) {
    cairo_surface_t *logo = cairo_image_surface_create_from_png(image_path);
    cairo_status_t status = cairo_surface_status(logo);
    if (status != CAIRO_STATUS_SUCCESS) {
        syslog(LOG_ERR, "Failed to load logo image: %s", cairo_status_to_string(status));
        cairo_surface_destroy(logo);
        return;
    }

    gint img_w = cairo_image_surface_get_width(logo);
    gint img_h = cairo_image_surface_get_height(logo);

    gdouble aspect = (gdouble)img_h / img_w;

    // Compute max width in pixels (from normalized width)
    gdouble max_draw_width = norm_width * overlay_width;
    gdouble max_draw_height = max_draw_width * aspect;

    // Compute normalized center (with padding and center alignment)
    gdouble norm_center_x = 1.0 - (max_draw_width / (2.0 * overlay_width)) - pad_x;
    gdouble norm_center_y = 0.0 + (max_draw_height / (2.0 * overlay_height)) + pad_y;

    // Convert to pixel position
    gdouble draw_x = norm_center_x * overlay_width - (max_draw_width / 2.0);
    gdouble draw_y = norm_center_y * overlay_height - (max_draw_height / 2.0);

    // Draw image
    cairo_save(context);
    cairo_translate(context, draw_x, draw_y);
    cairo_scale(context, max_draw_width / img_w, max_draw_height / img_h);
    cairo_set_source_surface(context, logo, 0, 0);
    cairo_paint(context);
    cairo_restore(context);

    cairo_surface_destroy(logo);
}
/**
 * brief Setup an overlay_data struct.
 *
 * This function initialize and setup an overlay_data
 * struct with default values.
 *
 * param data The overlay data struct to initialize.
 */
static void setup_axoverlay_data(struct axoverlay_overlay_data* data) {
    axoverlay_init_overlay_data(data);
    data->postype         = AXOVERLAY_CUSTOM_NORMALIZED;
    data->anchor_point    = AXOVERLAY_ANCHOR_CENTER;
    data->x               = 0.0;
    data->y               = 0.0;
    data->scale_to_stream = FALSE;
}


static void adjustment_cb(gint id,
                          struct axoverlay_stream_data* stream,
                          enum axoverlay_position_type* postype,
                          gfloat* overlay_x,
                          gfloat* overlay_y,
                          gint* overlay_width,
                          gint* overlay_height,
                          gpointer user_data) {
    /* Silence compiler warnings for unused parameters/arguments */
    (void)id;
    (void)postype;
    (void)overlay_x;
    (void)overlay_y;
    (void)user_data;

    /* Set overlay resolution in case of rotation */
    *overlay_width  = stream->width;
    *overlay_height = stream->height;
    if (stream->rotation == 90 || stream->rotation == 270) {
        *overlay_width  = stream->height;
        *overlay_height = stream->width;
    }

    syslog(LOG_INFO,
           "Stream or rotation changed, overlay resolution is now: %i x %i",
           *overlay_width,
           *overlay_height);
    syslog(LOG_INFO,
           "Stream or rotation changed, stream resolution is now: %i x %i",
           stream->width,
           stream->height);
    syslog(LOG_INFO, "Stream or rotation changed, rotation angle is now: %i", stream->rotation);
}


static void render_overlay_cb(gpointer rendering_context,
                              gint id,
                              struct axoverlay_stream_data* stream,
                              enum axoverlay_position_type postype,
                              gfloat overlay_x,
                              gfloat overlay_y,
                              gint overlay_width,
                              gint overlay_height,
                              gpointer user_data) {
    /* Silence compiler warnings for unused parameters/arguments */
    (void)id;
    (void)postype;
    (void)user_data;
    (void)overlay_x;
    (void)overlay_y;

    //gdouble val = FALSE;

    syslog(LOG_INFO, "Render callback for camera: %i", stream->camera);
    syslog(LOG_INFO, "Render callback for overlay: %i x %i", overlay_width, overlay_height);
    syslog(LOG_INFO, "Render callback for stream: %i x %i", stream->width, stream->height);
    syslog(LOG_INFO, "Render callback for rotation: %i", stream->rotation);

    // Draw logo in top right (0.9, 0.1) with normalized size 0.2 x 0.1
    draw_logo(rendering_context,
                "/usr/local/packages/add_logon/axis_tip_logo.png",
                0.02f, 0.02f,   // Padding from right/top in normalized units
                0.1f,           // Desired normalized width
                overlay_width,
                overlay_height);
}



/***** Signal handler functions **********************************************/

/**
 * brief Handles the signals.
 *
 * param loop Loop to quit
 */
static gboolean signal_handler(gpointer loop) {
    g_main_loop_quit((GMainLoop*)loop);
    syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");
    return G_SOURCE_REMOVE;
}

/***** Main function *********************************************************/

/**
 * brief Main function.
 *
 * This main function draws two plain boxes and one text, using the
 * API axoverlay.
 */
int main(void) {
    // Set XDG cache home to application's localdata directory for fontconfig
    setenv("XDG_CACHE_HOME", "/usr/local/packages/axoverlay/localdata", 1);
    GMainLoop* loop    = NULL;
    GError* error      = NULL;
    GError* error_text = NULL;
    gint camera_height = 0;
    gint camera_width  = 0;

    openlog(NULL, LOG_PID, LOG_USER);

    //  Create a glib main loop
    loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);

    if (!axoverlay_is_backend_supported(AXOVERLAY_CAIRO_IMAGE_BACKEND)) {
        syslog(LOG_ERR, "AXOVERLAY_CAIRO_IMAGE_BACKEND is not supported");
        return 1;
    }

    //  Initialize the library
    struct axoverlay_settings settings;
    axoverlay_init_axoverlay_settings(&settings);
    settings.render_callback     = render_overlay_cb;
    settings.adjustment_callback = adjustment_cb;
    settings.select_callback     = NULL;
    settings.backend             = AXOVERLAY_CAIRO_IMAGE_BACKEND;
    axoverlay_init(&settings, &error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to initialize axoverlay: %s", error->message);
        g_error_free(error);
        return 1;
    }

    
    // Get max resolution for width and height
    camera_width = axoverlay_get_max_resolution_width(1, &error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to get max resolution width: %s", error->message);
        g_error_free(error);
        error = NULL;
    }

    camera_height = axoverlay_get_max_resolution_height(1, &error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to get max resolution height: %s", error->message);
        g_error_free(error);
        error = NULL;
    }

    syslog(LOG_INFO, "Max resolution (width x height): %i x %i", camera_width, camera_height);

    // Create a large overlay using Palette color space
    struct axoverlay_overlay_data data;
    setup_axoverlay_data(&data);
    data.width      = camera_width;
    data.height     = camera_height;
    overlay_id      = axoverlay_create_overlay(&data, NULL, &error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to create first overlay: %s", error->message);
        g_error_free(error);
        return 1;
    }
    // Draw overlays
    axoverlay_redraw(&error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to draw overlays: %s", error->message);
        axoverlay_destroy_overlay(overlay_id, &error);
        axoverlay_cleanup();
        g_error_free(error);
        g_error_free(error_text);
        return 1;
    }
    // Enter main loop
    g_main_loop_run(loop);

    // Destroy the overlay
    axoverlay_destroy_overlay(overlay_id, &error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to destroy first overlay: %s", error->message);
        g_error_free(error);
        return 1;
    }
    

    // Release library resources
    axoverlay_cleanup();

    // Release main loop
    g_main_loop_unref(loop);

    return 0;
}