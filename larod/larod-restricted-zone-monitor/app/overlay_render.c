#include "overlay_render.h"
#include <math.h>
#include <stdio.h>

void overlay_render(cairo_surface_t* surface, unsigned width, unsigned height, Zone zone,
                    bool enabled, bool alarm, const Detections* detections) {
    cairo_t* cr = cairo_create(surface);
    /* Clear every pixel, including alignment padding and old labels. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    if (!enabled) {
        cairo_destroy(cr);
        return;
    }

    double scale = fmax(0.5, width / 1280.0);
    cairo_set_source_rgb(cr, 1, 0.81, 0.20);
    cairo_set_line_width(cr, 2 * scale);
    cairo_rectangle(cr, zone.x * width, zone.y * height, zone.width * width, zone.height * height);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 22 * scale);
    for (size_t index = 0; index < detections->count; index++) {
        const Person* person = &detections->people[index];
        double red = 0.20, green = 0.87, blue = 0.47;
        if (person->inside) {
            red = 1;
            green = alarm ? 0.27 : 0.69;
            blue = alarm ? 0.27 : 0.13;
        }
        double x = person->left * width;
        double y = person->top * height;
        double w = (person->right - person->left) * width;
        double h = (person->bottom - person->top) * height;

        cairo_rectangle(cr, x, y, w, h);
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
        cairo_set_line_width(cr, 8 * scale);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, red, green, blue);
        cairo_set_line_width(cr, 5 * scale);
        cairo_stroke(cr);

        char label[32];
        snprintf(label, sizeof(label), "Person %.0f%%", person->confidence * 100.0);
        cairo_text_extents_t text;
        cairo_text_extents(cr, label, &text);
        double padding = 6 * scale;
        double label_width = fmin(width, text.width + 2 * padding);
        double label_height = fmin(height, 30 * scale);
        double label_x = fmax(0, fmin(x, width - label_width));
        double label_y = fmax(0, fmin(y - label_height, height - label_height));
        cairo_set_source_rgba(cr, 0.04, 0.04, 0.04, 0.9);
        cairo_rectangle(cr, label_x, label_y, label_width, label_height);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, red, green, blue);
        cairo_move_to(cr, label_x + padding - text.x_bearing,
                      label_y + (label_height - text.height) / 2 - text.y_bearing);
        cairo_show_text(cr, label);
    }
    cairo_destroy(cr);
}
