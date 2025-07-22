# Draw views normalized coordinates


## Description

Draw different shapes for differend existing views when accessing from the web inteface.

Use of palette color space for large overlays like plain boxes, to lower the memory usage.

## Draw functions
---

```c

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
```
---

```c
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
```
---

```c
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
    //cairo_stroke(context);
    cairo_fill(context);              // Fill the triangle
}
```
---

## Build

```
docker build --tag draw-view --build-arg ARCH=aarch64 .
```
```
docker cp $(docker create draw-view):/opt/app ./build
```