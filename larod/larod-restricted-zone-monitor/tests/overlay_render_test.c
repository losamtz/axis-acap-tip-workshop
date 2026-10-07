#include "overlay_render.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t pixel(cairo_surface_t* surface, int x, int y) {
    cairo_surface_flush(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    return *(uint32_t*)(data + y * stride + x * 4);
}

int main(void) {
    /* Include padding: it must stay transparent, rather than stretching geometry. */
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 672, 384);
    Detections detections = {.count = 1};
    detections.people[0] = (Person){
        .left = 0.25,
        .top = 0.25,
        .right = 0.5,
        .bottom = 0.75,
        .confidence = 0.78,
        .inside = false,
    };
    Zone zone = {0.1, 0.1, 0.8, 0.8};
    overlay_render(surface, 640, 360, zone, true, false, &detections);
    assert(cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
    assert(pixel(surface, 160, 150) != 0); /* Person outline. */
    assert(pixel(surface, 200, 160) == 0); /* Outline does not mask the person. */
    assert(pixel(surface, 660, 100) == 0); /* Alignment padding. */
    assert(pixel(surface, 166, 78) != 0);  /* Label background above the box. */
    uint32_t green = pixel(surface, 160, 150);
    assert(((green >> 8) & 255) > ((green >> 16) & 255));

    detections.people[0].inside = true;
    overlay_render(surface, 640, 360, zone, true, true, &detections);
    uint32_t red = pixel(surface, 160, 150);
    assert(((red >> 16) & 255) > ((red >> 8) & 255));

    detections.count = 0;
    overlay_render(surface, 640, 360, zone, true, false, &detections);
    assert(pixel(surface, 160, 150) == 0);
    assert(pixel(surface, 166, 78) == 0); /* Previous text/background cleared. */
    overlay_render(surface, 640, 360, zone, false, false, &detections);
    for (int y = 0; y < 384; y++) {
        for (int x = 0; x < 672; x++) {
            assert(pixel(surface, x, y) == 0);
        }
    }
    cairo_surface_destroy(surface);
    puts("Overlay rendering: outlines, labels, alarm color, padding, and clearing passed");
    return 0;
}
