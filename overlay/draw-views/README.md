# Overlay API - Draw views 

This sample shows how to use the Axis axoverlay API with the `Cairo image backend` to render different shapes `per camera channel` using normalized coordinates:

- **Channel 1** → centered rectangle (normalized size 0.5 × 0.25)
- **Channel 2** → centered circle (normalized radius 0.25)
- **Channel 3+** → centered triangle (three normalized points)

It demonstrates:

- Initializing axoverlay and registering **render** and **adjustment** callbacks.
- Creating a full-frame **4-bit palette** overlay ( `AXOVERLAY_COLORSPACE_4BIT_PALETTE`).
- Using `normalized coordinates` for device-independent layout.
- Converting palette indices to Cairo color with a tiny helper.
- Handling rotation & resolution changes per stream.

## What the app does

1. Verifies `AXOVERLAY_CAIRO_IMAGE_BACKEND` support.
2. Initializes axoverlay with:

    - render_overlay_cb — draws a shape based on stream->camera.
    - adjustment_cb — updates overlay width/height on rotation/resolution changes.

3. Sets up a small **palette** (index→ARGB).
4. Creates one large overlay sized to the stream’s **max resolution**.
5. Draws once via `axoverlay_redraw()` and then remains responsive to stream changes.

**Channel mapping (0-based in code, 1-based on device):**

- stream->camera == 1 → rectangle
- stream->camera == 2 → circle
- stream->camera >= 3 → triangle

## Key files / functions

- `setup_axoverlay_data` – normalized position, center anchor, stream scaling off.
- `setup_palette_color` – defines palette entries for 4-bit palette mode.
- `index2cairo` – converts palette index to Cairo [0..1] float.
- `draw_rectangle`, `draw_circle`, `draw_triangle` – helpers that accept normalized inputs and convert to   pixels using current overlay size.

- `adjustment_cb` – swaps width/height for 90°/270° rotation and logs the change.
- `render_overlay_cb` – clears the background and draws the shape for the current stream->camera.


## Lab:

1. Launch the app from the camera’s Apps page, then open Live View(s).
2. Check

- Channel 1 shows a centered rectangle.
- Channel 2 shows a centered circle.
- Channel 3 (and above) shows a centered triangle.
- Changing stream resolution or rotation keeps shapes correctly sized/centered (watch syslog for adjustment logs).
- Shapes render with the palette colors defined in code.

## Normalized coordinates (how this sample uses them)

- All shape positions/sizes use normalized values (0.0 .. 1.0):

    - Center (0.5, 0.5) = middle of the overlay.
    - Width/Height are fractions of overlay size; radius scales with the smaller dimension for consistent look.

- This makes the overlay resolution-independent; the helper converts normalized values → pixels at render time.

## Customization

1. Change shapes by channel:

    - Edit the if (channel == 0) ... else if (channel == 1) ... else ... block in render_overlay_cb.

2. Adjust sizes/positions:

    - Update normalized dims in the draw helpers (e.g., rectangle 0.5 × 0.25, circle radius 0.25).

3. Colors:

    - Modify palette entries in setup_palette_color and use different indices (circle_color, triangle_color, rectangle_color).

--- 

## Code flow

### Select channel in render_cb

```c
size_t channel = stream->camera - 1;

```

### Draw functions
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

```bash
docker build --tag draw-views --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create draw-views):/opt/app ./build
```