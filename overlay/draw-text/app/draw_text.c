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


typedef struct {
    double r;
    double g;
    double b;
} RGBColor;

static RGBColor color;

static gint animation_timer = -1;
static gint overlay_id_text = -1;
static gint counter         = 10;


static void draw_text(cairo_t* context, const gint pos_x, const gint pos_y) {
    cairo_text_extents_t te;
    cairo_text_extents_t te_length;
    gchar* str        = NULL;
    gchar* str_length = NULL;

    //  Show text in black
    cairo_set_source_rgb(context, color.r, color.g, color.b);
    cairo_select_font_face(context, "serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(context, 32.0);

    // Position the text at a fix centered position
    str_length = g_strdup_printf("Countdown  ");
    cairo_text_extents(context, str_length, &te_length);
    cairo_move_to(context, pos_x - te_length.width / 2, pos_y);
    g_free(str_length);

    // Add the counter number to the shown text
    str = g_strdup_printf("Countdown %i", counter);
    cairo_text_extents(context, str, &te);
    cairo_show_text(context, str);
    g_free(str);
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



/***** Callback functions ****************************************************/

/**
 * brief A callback function called when an overlay needs adjustments.
 *
 * This function is called to let developers make adjustments to
 * the size and position of their overlays for each stream. This callback
 * function is called prior to rendering every time when an overlay
 * is rendered on a stream, which is useful if the resolution has been
 * updated or rotation has changed.
 *
 * param id Overlay id.
 * param stream Information about the rendered stream.
 * param postype The position type.
 * param overlay_x The x coordinate of the overlay.
 * param overlay_y The y coordinate of the overlay.
 * param overlay_width Overlay width.
 * param overlay_height Overlay height.
 * param user_data Optional user data associated with this overlay.
 */
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

/**
 * brief A callback function called when an overlay needs to be drawn.
 *
 * This function is called whenever the system redraws an overlay. This can
 * happen in two cases, axoverlay_redraw() is called or a new stream is
 * started.
 *
 * param rendering_context A pointer to the rendering context.
 * param id Overlay id.
 * param stream Information about the rendered stream.
 * param postype The position type.
 * param overlay_x The x coordinate of the overlay.
 * param overlay_y The y coordinate of the overlay.
 * param overlay_width Overlay width.
 * param overlay_height Overlay height.
 * param user_data Optional user data associated with this overlay.
 */
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


    syslog(LOG_INFO, "Render callback for camera: %i", stream->camera);
    syslog(LOG_INFO, "Render callback for overlay: %i x %i", overlay_width, overlay_height);
    syslog(LOG_INFO, "Render callback for stream: %i x %i", stream->width, stream->height);
    syslog(LOG_INFO, "Render callback for rotation: %i", stream->rotation);

    if (id == overlay_id_text) {
        //  Show text in black
        draw_text(rendering_context, overlay_width / 2, overlay_height / 2);
    } else {
        syslog(LOG_INFO, "Unknown overlay id!");
    }
}

/**
 * brief Callback function which is called when animation timer has elapsed.
 *
 * This function is called when the animation timer has elapsed, which will
 * update the counter, colors and also trigger a redraw of the overlay.
 *
 * param user_data Optional callback user data.
 */
static gboolean update_overlay_cb(gpointer user_data) {
    /* Silence compiler warnings for unused parameters/arguments */
    (void)user_data;

    GError* error = NULL;

    // Countdown
    counter = counter < 1 ? 10 : counter - 1;

    if (counter >= 0 && counter <= 3) 
        color = (RGBColor){ 1.0, 0.0, 0.0}; // red
    else if (counter > 3 && counter <= 7)
        color = (RGBColor){ 0.0, 0.0, 1.0}; // blue
    else
        color = (RGBColor){0.0, 1.0, 0.0}; // Green

    // Request a redraw of the overlay
    axoverlay_redraw(&error);
    if (error != NULL) {
        /*
         * If redraw fails then it is likely due to that overlayd has
         * crashed. Don't exit instead wait for overlayd to restart and
         * for axoverlay to restore the connection.
         */
        syslog(LOG_ERR, "Failed to redraw overlay (%d): %s", error->code, error->message);
        g_error_free(error);
    }

    return G_SOURCE_CONTINUE;
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


    
    // Create an text overlay using ARGB32 color space
    struct axoverlay_overlay_data data_text;
    setup_axoverlay_data(&data_text);
    data_text.width      = camera_width;
    data_text.height     = camera_height;
    data_text.colorspace = AXOVERLAY_COLORSPACE_ARGB32;
    overlay_id_text      = axoverlay_create_overlay(&data_text, NULL, &error_text);
    if (error_text != NULL) {
        syslog(LOG_ERR, "Failed to create second overlay: %s", error_text->message);
        g_error_free(error_text);
        return 1;
    }

    // Draw overlays
    axoverlay_redraw(&error);
    if (error != NULL) {
        syslog(LOG_ERR, "Failed to draw overlays: %s", error->message);
        axoverlay_destroy_overlay(overlay_id_text, &error_text);
        axoverlay_cleanup();
        g_error_free(error);
        g_error_free(error_text);
        return 1;
    }

    // Start animation timer
    animation_timer = g_timeout_add_seconds(1, update_overlay_cb, NULL);

    // Enter main loop
    g_main_loop_run(loop);

    axoverlay_destroy_overlay(overlay_id_text, &error_text);
    if (error_text != NULL) {
        syslog(LOG_ERR, "Failed to destroy second overlay: %s", error_text->message);
        g_error_free(error_text);
        return 1;
    }

    // Release library resources
    axoverlay_cleanup();

    // Release the animation timer
    g_source_remove(animation_timer);

    // Release main loop
    g_main_loop_unref(loop);

    return 0;
}