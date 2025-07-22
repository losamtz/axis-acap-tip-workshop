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
 * - draw rectangle or circle depending on channel normalized -
 *
 * This application demonstrates how the use the API axoverlay, by drawing
 * plain boxes using 4-bit palette color format
 *
 * Colorspace and alignment:
 * 1-bit palette (AXOVERLAY_COLORSPACE_1BIT_PALETTE): 32-byte alignment
 * 4-bit palette (AXOVERLAY_COLORSPACE_4BIT_PALETTE): 16-byte alignment
 * 
 *
 */

#include <axoverlay.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdlib.h>
#include <syslog.h>



#define PALETTE_VALUE_RANGE 255.0


static gint overlay_id         = -1;
static gint circle_color       =  1;
static gint triangle_color     =  3;
static gint rectangle_color    =  4;


static gdouble index2cairo(const gint color_index) {
    return ((color_index << 4) + color_index) / PALETTE_VALUE_RANGE;
}


static void draw_rectangle(cairo_t* context,
                            gfloat center_x,
                            gfloat center_y,
                            gfloat width,
                            gfloat height,
                            gint overlay_width,
                            gint overlay_height,
                            gint color_index,
                            gint line_width) {

    gdouble val = 0;

    val = index2cairo(color_index);
    cairo_set_source_rgba(context, val, val, val, val);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width(context, line_width);

    gdouble pixel_width = width * overlay_width;
    gdouble pixel_height = height * overlay_height;
    gdouble left = (center_x * overlay_width) - (pixel_width / 2);
    gdouble top = (center_y * overlay_height) - (pixel_height / 2);

    cairo_rectangle(context, left, top, pixel_width, pixel_height);
    cairo_stroke(context);
}

static void draw_circle(cairo_t* context,
                           gfloat center_x,
                           gfloat center_y,
                           gfloat radius,
                           gint overlay_width,
                           gint overlay_height,
                           gint color_index,
                           gint line_width) {
    gdouble val = 0;

    val = index2cairo(color_index);
    cairo_set_source_rgba(context, val, val, val, val);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width(context, line_width);

    // Convert normalized coords (0.0–1.0) to actual pixels
    gdouble cx = center_x * overlay_width;
    gdouble cy = center_y * overlay_height;
    gdouble r  = radius * MIN(overlay_width, overlay_height);  // Scale radius to smallest dimension

    // Draw the circle
    cairo_arc(context, cx, cy, r, 0, 2 * G_PI);
    cairo_stroke(context);
}

static void draw_triangle(cairo_t* context,
                          gfloat x1, gfloat y1,
                          gfloat x2, gfloat y2,
                          gfloat x3, gfloat y3,
                          gint overlay_width,
                          gint overlay_height,
                          gint color_index,
                          gint line_width) {

    gdouble val = 0;

    val = index2cairo(color_index);                        
    cairo_set_source_rgba(context, val, val, val, val);   // Set fill color
    cairo_set_line_width(context, line_width);

    cairo_move_to(context, x1 * overlay_width, y1 * overlay_height);   // First point
    cairo_line_to(context, x2 * overlay_width, y2 * overlay_height);   // Second point
    cairo_line_to(context, x3 * overlay_width, y3 * overlay_height);   // Third point
    cairo_close_path(context);        // Closes the triangle back to the first point

    cairo_fill(context);              // Fill the triangle
}

static void setup_axoverlay_data(struct axoverlay_overlay_data* data) {
    axoverlay_init_overlay_data(data);
    data->postype         = AXOVERLAY_CUSTOM_NORMALIZED;
    data->anchor_point    = AXOVERLAY_ANCHOR_CENTER;
    data->x               = 0.0;
    data->y               = 0.0;
    data->scale_to_stream = FALSE;
}


static gboolean setup_palette_color(const int index, const gint r, const gint g, const gint b, const gint a) {
    GError* error = NULL;
    struct axoverlay_palette_color color;

    color.red      = r;
    color.green    = g;
    color.blue     = b;
    color.alpha    = a;
    color.pixelate = FALSE;
    axoverlay_set_palette_color(index, &color, &error);
    if (error != NULL) {
        g_error_free(error);
        return FALSE;
    }

    return TRUE;
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
    (void)postype;
    (void)user_data;
    (void)overlay_x;
    (void)overlay_y;

    gdouble val = FALSE;

    
    
    size_t channel = stream->camera - 1;

    syslog(LOG_INFO, "Rendering overlay on ID=%d, stream ID=%d, and camera=%d", id, stream->id, stream->camera);  
    syslog(LOG_INFO, "Channel: %ld", channel);                            
    syslog(LOG_INFO, "Render callback for camera: %i", stream->camera);
    syslog(LOG_INFO, "Render callback for overlay: %i x %i", overlay_width, overlay_height);
    syslog(LOG_INFO, "Render callback for stream: %i x %i", stream->width, stream->height);
    syslog(LOG_INFO, "Render callback for rotation: %i", stream->rotation);
    

    if (channel == 0) {
    // Clear background
    val = index2cairo(0);
    cairo_set_source_rgba(rendering_context, val, val, val, val);
    cairo_set_operator(rendering_context, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(rendering_context, 0, 0, overlay_width, overlay_height);
    cairo_fill(rendering_context);

    // Draw normalized rectangle centered at (0.5, 0.5), size 0.5 x 0.25
    draw_rectangle(rendering_context,
                               0.5, 0.5,           // center (normalized)
                               0.5, 0.25,          // size (normalized)
                               overlay_width,
                               overlay_height,
                               rectangle_color,
                               3.0);

    } else if (channel == 1) {
        // Draw normalized circle centered at (0.5, 0.5), radius 0.25
        draw_circle(rendering_context,
                    0.5, 0.5,
                    0.25,
                    overlay_width,
                    overlay_height,
                    circle_color,
                    3);

    } else {
        // Draw normalized triangle with points in normalized coordinates
        draw_triangle(rendering_context,
                                0.5, 0.25,   // top
                                0.3, 0.75,   // bottom left
                                0.7, 0.75,   // bottom right
                                overlay_width,
                                overlay_height,
                                triangle_color,
                                3);
    }


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
 * This main function draws one yellow rectangle, using the
 * API axoverlay.
 */
int main(void) {
    // Set XDG cache home to application's localdata directory for fontconfig
    setenv("XDG_CACHE_HOME", "/usr/local/packages/axoverlay/localdata", 1);
    GMainLoop* loop    = NULL;
    GError* error      = NULL;
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

    //  Setup colors
    if (!setup_palette_color(0, 0, 0, 0, 0) || !setup_palette_color(1, 255, 0, 0, 255) ||
        !setup_palette_color(2, 0, 255, 0, 255) || !setup_palette_color(3, 0, 0, 255, 255) || 
        !setup_palette_color(4, 255, 255, 0, 255)) {
        syslog(LOG_ERR, "Failed to setup palette colors");
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
    data.colorspace = AXOVERLAY_COLORSPACE_4BIT_PALETTE;
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