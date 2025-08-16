

### Typical Flow of Interaction

1. **`axoverlay_init()`**

    You initialize the overlay system with `axoverlay_settings`.

    - You register a `render_callback` which is called when the overlay should be drawn.
    - You can also set position types like `AXOVERLAY_CUSTOM_NORMALIZED`.

2. **`axoverlay_create_overlay()`**

    You create one or more overlays, specifying which stream and position to attach to.

3. **`render_callback()`**

    Every frame (or periodically), Axis calls your render callback, e.g.:

    ```c
    static void render_overlay_cb(gpointer rendering_context,
                                  gint id,
                                  struct axoverlay_stream_data* stream,
                                  enum axoverlay_position_type postype,
                                  gfloat overlay_x,
                                  gfloat overlay_y,
                                  gint overlay_width,
                                  gint overlay_height,
                                  gpointer user_data)
    {
        // This is where you use Cairo to draw on the overlay.
    }
    ```

---

### Cairo Drawing Inside the Callback

You use the passed `rendering_context` (which is a Cairo context: `cairo_t*`) to issue drawing commands:

```c
cairo_set_source_rgba(ctx, r, g, b, a);
cairo_rectangle(ctx, x, y, w, h);
cairo_fill(ctx);
```
These commands draw on a canvas that overlays the video stream.

---

### Execution Order Summary

```c
main() {
    axoverlay_init();                 // 1️⃣ Setup overlay system
    axoverlay_create_overlay();      // 2️⃣ Create overlays (attach to streams)
    ...                              // GLib main loop starts
}

render_callback(...) {
    // 3️⃣ Called automatically when it's time to render the overlay
    cairo_* functions;               // 4️⃣ Use Cairo to draw content
}
```

### How They Work Together

| axoverlay                               | Cairo                            |
|----------------------------------------|---------------------------------|
| Owns the overlay buffer and stream attachment. | Does the drawing itself.         |
| Provides resolution and context in callback.    | Uses that context to render.     |
| Handles scale, rotation, and placement.          | Ignores stream logic. Just draws.|
| Thinks in terms of streams.                        | Thinks in terms of 2D surfaces.  |

✅ Example Sequence

```c
// Main init
axoverlay_settings s = { ... };
axoverlay_init(&s, &error);         // Registers render callback
axoverlay_create_overlay(...);      // For camera stream 1

// Later, in callback (called by Axis)
render_overlay_cb(...) {
    cairo_set_source_rgb(ctx, 1, 1, 0);          // Yellow
    cairo_rectangle(ctx, 0, 0, 100, 50);         // Rectangle
    cairo_fill(ctx);                              // Render
}
```

## Normalized Coordinates in Axis ACAP Overlay

### What are Normalized Coordinates?

Instead of specifying overlay positions using absolute pixel values (e.g., 100 pixels from the left), normalized coordinates scale positions to a fixed range between **-1** and **1**, regardless of the video frame's resolution or size.

This means your overlay positioning becomes resolution-independent and consistent across different video sizes.

### Coordinate System Overview

- The coordinate system is centered in the middle of the video frame.
- The **x-axis** runs horizontally from **-1** (far left edge) to **1** (far right edge).
- The **y-axis** runs vertically from **-1** (bottom edge) to **1** (top edge).

### Visual Representation

---
```
       y = 1 (top)
         |
 (-1,1)  |  (1,1)
         |  
---------+--------- x = 1 (right)
         |  
 (-1,-1) |  (1,-1)
         |
       y = -1 (bottom)
```
---

- **x = -1** → far left edge of the frame  
- **x = 0** → horizontal center  
- **x = 1** → far right edge  
- **y = -1** → bottom edge  
- **y = 0** → vertical center  
- **y = 1** → top edge  

### Why Use Normalized Coordinates?

- **Resolution independence:** The same coordinates work on any video size or resolution.  
- **Consistent positioning:** (0, 0) is always the center.  
- **Easy corner placement:**  
  - (-1, 1) → top-left corner  
  - (1, -1) → bottom-right corner  
- **Smooth positioning:** Use decimal values to position anywhere in between, e.g., (0.5, -0.3).

### Summary Table

| Coordinate Value | Meaning            | Position on Frame              |
|------------------|--------------------|-------------------------------|
| -1               | Minimum normalized  | Left (for x), Bottom (for y)  |
| 0                | Centered           | Center                        |
| 1                | Maximum normalized  | Right (for x), Top (for y)     |


## Overlay Position Types

#### The different position types:

#### Corners (fixed positions):

- `AXOVERLAY_TOP_LEFT`
- `AXOVERLAY_TOP_RIGHT`
- `AXOVERLAY_BOTTOM_LEFT`
- `AXOVERLAY_BOTTOM_RIGHT`

For these four positions, any x and y coordinates you provide are ignored because the position is fixed at the corresponding corner of the video frame.

---

#### Custom Normalized Position:

- `AXOVERLAY_CUSTOM_NORMALIZED`

When using this, the overlay position is defined by x and y values normalized between -1 and 1.

- `-1` means the far left (for x) or bottom (for y)
- `1` means the far right (for x) or top (for y)

This allows placing the overlay anywhere in the frame using a normalized coordinate system.

---

#### Custom Source Position:

- `AXOVERLAY_CUSTOM_SOURCE`

Here, the overlay position is specified relative to the video source itself, not the whole video frame.

This means the coordinates are absolute pixel values ranging between 0 and the maximum width/height of the source.

This is especially useful if the video is rotated or if digital pan-tilt-zoom (DPTZ) is used, because then the overlay stays locked to the scene rather than moving with the video frame.

---

#### Summary

| Position Type               | Coordinates Usage               | Positioning Behavior                     |
|----------------------------|--------------------------------|-----------------------------------------|
| `AXOVERLAY_TOP_LEFT`        | x, y ignored                   | Fixed top-left corner                    |
| `AXOVERLAY_TOP_RIGHT`       | x, y ignored                   | Fixed top-right corner                   |
| `AXOVERLAY_BOTTOM_LEFT`     | x, y ignored                   | Fixed bottom-left corner                 |
| `AXOVERLAY_BOTTOM_RIGHT`    | x, y ignored                   | Fixed bottom-right corner                |
| `AXOVERLAY_CUSTOM_NORMALIZED`| Normalized x, y between -1 and 1| Custom position in normalized coords    |
| `AXOVERLAY_CUSTOM_SOURCE`   | Absolute pixel x, y relative to source | Custom position relative to video source |

---

## Overlay code setup


### Initialize library

```c
    struct axoverlay_settings settings;
    axoverlay_init_axoverlay_settings(&settings);
    settings.render_callback     = render_overlay_cb;
    settings.adjustment_callback = adjustment_cb;
    settings.select_callback     = NULL;
    settings.backend             = AXOVERLAY_CAIRO_IMAGE_BACKEND;
    axoverlay_init(&settings, &error);

```

### Setup the colors

```c
    struct axoverlay_palette_color color;

    color.red      = r;
    color.green    = g;
    color.blue     = b;
    color.alpha    = a;
    color.pixelate = FALSE;
    axoverlay_set_palette_color(index, &color, &error);

```
### Get max resolution for width and height

```c
    gint camera_width = axoverlay_get_max_resolution_width(1, &error);
    gint camera_height = axoverlay_get_max_resolution_width(1, &error);

```

### Create a large overlay using Palette color space

```c
    struct axoverlay_overlay_data data;
    setup_axoverlay_data(&data);
    data.width      = camera_width;
    data.height     = camera_height;
    data.colorspace = AXOVERLAY_COLORSPACE_4BIT_PALETTE;
    gint overlay_id = axoverlay_create_overlay(&data, NULL, &error);

```
#### Initialize an overlay_data struct with default values
```c

    static void setup_axoverlay_data(struct axoverlay_overlay_data* data) {
        axoverlay_init_overlay_data(data);
        data->postype         = AXOVERLAY_CUSTOM_NORMALIZED;
        data->anchor_point    = AXOVERLAY_ANCHOR_CENTER;
        data->x               = 0.0;
        data->y               = 0.0;
        data->scale_to_stream = FALSE;
    }


```

### Draw overlay

```c
    axoverlay_redraw(&error);

```

