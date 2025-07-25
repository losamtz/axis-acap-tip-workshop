# Overlay

This chapter explains the use of axoverlay to draw boxes, text and finally set an image/logo in the stream. In genaral, axoverlay follows a general setup and flow 


We will start with boxes creating several apps: 

Then we will draw text in the following apps:

We will go through some of the setting relevant to the cameras, mostly "normalized", that will scale in different resolutions.

Note:
It is preferable to use Palette color space for large overlays like plain boxes, to lower the memory usage. More detailed overlays like text overlays, should instead use ARGB32 color space.

###  Overview: axoverlay and Cairo roles

| Component  | Role                                                                                 |
|------------|--------------------------------------------------------------------------------------|
| **axoverlay** | The Axis-specific API that sets up overlays, handles video stream integration, resolution, positions, and render callbacks. |
| **Cairo**     | The graphics library used to draw on the overlay canvas (text, shapes, lines, images, etc.). |

---

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

## Cairo settings

### Cairo Compositing Operators Overview

This table summarizes some of the most commonly used compositing operators in the [Cairo graphics library](https://cairographics.org/operators/). These operators determine how new drawings (source) interact with existing content (destination).

### Common Operators

| Operator                        | Visual Effect    | Description                                                                                                           |
| -------------------------------|------------------|-----------------------------------------------------------------------------------------------------------------------|
| `CAIRO_OPERATOR_CLEAR`          | 🧽 Erase          | Clears everything to full transparency (like erasing pixels).                                                         |
| `CAIRO_OPERATOR_SOURCE`         | 🎯 Replace        | Completely replaces the destination with the source, ignoring what's already there.                                   |
| `CAIRO_OPERATOR_OVER` (default) | 📄 Stack          | Draws the source **over** the destination using alpha blending. This is like putting a semi-transparent image on top. |
| `CAIRO_OPERATOR_IN`             | 📥 Mask           | Draws the source only where there’s destination content.                                                              |
| `CAIRO_OPERATOR_OUT`            | 📤 Negative Mask  | Draws the source only **where there's no** destination content.                                                       |
| `CAIRO_OPERATOR_ATOP`           | 🔁 Clip-Overlay   | Keeps the destination, draws source **only where** destination exists.                                                |
| `CAIRO_OPERATOR_ADD`            | ➕ Brighten        | Adds color values — can brighten overlapping pixels.                                                                  |
| `CAIRO_OPERATOR_MULTIPLY`       | 🌘 Darken         | Multiplies source and destination colors — makes result darker.                                                       |
| `CAIRO_OPERATOR_SCREEN`         | 🌕 Lighten        | Inverse multiply — result is lighter.                                                                                 |

#### Notes

- The default operator used by Cairo is `CAIRO_OPERATOR_OVER`.
- For overlays (like in ACAP or UI rendering), `CAIRO_OPERATOR_SOURCE` is often preferred to fully replace pixels.
- Some operators like `ADD`, `MULTIPLY`, and `SCREEN` are useful for blending or special effects.

For a complete list and detailed explanations, visit the [official Cairo operators documentation](https://cairographics.org/operators/).




### Cairo Operator Visual Summary

A quick visual reference for some common Cairo compositing operators and their effects:

| Operator   | Visual                      |
|------------|-----------------------------|
| `OVER`     | ✅ See-through blend         |
| `SOURCE`   | ⛔ No blend; full overwrite  |
| `CLEAR`    | ❌ Transparent (wipes area)  |
| `ADD`      | ✨ Makes it brighter         |
| `MULTIPLY` | 🌑 Darker if colors overlap |

---

### Notes

- `OVER` is the default operator and does alpha blending.
- `SOURCE` replaces pixels fully, ignoring what's beneath.
- `CLEAR` erases pixels to transparency.
- `ADD` brightens overlapping pixels.
- `MULTIPLY` darkens overlapping pixels.

For more details, see [Cairo Operators Documentation](https://cairographics.org/operators/).



### Grayscale Mapping Using `index2cairo` for Cairo Overlays

This function converts a grayscale color index (typically in the range 0–15) into a normalized floating-point value between 0.0 and 1.0, which can be used with cairo_set_source_rgba().

#### Function Overview

```c
static gdouble index2cairo(const gint color_index) {
    return ((color_index << 4) + color_index) / PALETTE_VALUE_RANGE;
}

```

### Step 1: Bit Manipulation

```c
((color_index << 4) + color_index)
```

* `color_index << 4` shifts the value 4 bits to the left (i.e., multiplies it by 16).
* Adding `color_index` again gives:
  → `color_index * 16 + color_index = color_index * 17`

This is a standard way to scale a **4-bit value (0–15)** to **8-bit grayscale (0–255)**:

| color\_index (4-bit) | 8-bit Value (× 17) | Description    |
| -------------------- | ------------------ | -------------- |
| 0                    | 0                  | Black          |
| 1                    | 17                 | Very dark gray |
| ...                  | ...                | ...            |
| 15                   | 255                | White          |

---

### Step 2: Normalize to \[0.0, 1.0]

```c
/ PALETTE_VALUE_RANGE
```

Defined:

```c
#define PALETTE_VALUE_RANGE 255.0
```

This scales the 8-bit grayscale value to a Cairo-friendly floating-point value between `0.0` and `1.0`.

---

### How It's Used in Drawing

```c
val = index2cairo(color_index);
cairo_set_source_rgba(context, val, val, val, val);
```

This sets the drawing color in Cairo to a **semi-transparent shade of gray**:

* `R = G = B = val`
* `A (opacity) = val`

So:

* `color_index = 0` → fully transparent black
* `color_index = 15` → fully opaque white
* Intermediate values → semi-transparent grays

---

### Visual Mapping Table

| color\_index | 8-bit Gray Value | Normalized val | Visual Description |
| ------------ | ---------------- | -------------- | ------------------ |
| 0            | 0                | 0.000          | Transparent Black  |
| 1            | 17               | 0.067          | Very Dark Gray     |
| 2            | 34               | 0.133          | Dark Gray          |
| 4            | 68               | 0.267          | Gray               |
| 8            | 136              | 0.533          | Medium Gray        |
| 12           | 204              | 0.800          | Light Gray         |
| 15           | 255              | 1.000          | White              |

> (Intermediate values omitted for brevity)

---

### ✅ Summary

The `index2cairo()` function:

* Converts a **4-bit grayscale value** to 8-bit via `index × 17`
* Normalizes it to a float between `0.0` and `1.0`
* Applies it as an **RGBA color in Cairo** for rendering

This allows clean, consistent grayscale overlays on video streams in Axis ACAP using Cairo.


### Coordinate vs Color Normalization in Axis ACAP Overlays

When working with Axis ACAP overlays using Cairo, you’ll encounter two different kinds of normalized values:

1. **Overlay coordinates** (for positioning on screen)
2. **Cairo RGBA values** (for setting color and opacity)

These use **different ranges** and serve **different purposes** — here’s why:

---

### Overlay Coordinate Normalization (`AXOVERLAY_CUSTOM_NORMALIZED`)

- **Range:** `[-1, 1]` for both X and Y
- **Purpose:** To position overlays relative to the **video frame**, independent of resolution
- **Centered system:** `0` represents the center of the frame

#### ✅ Examples:

| Coordinate | Meaning              |
|------------|----------------------|
| (0, 0)     | Center of the frame  |
| (-1, 1)    | Top-left corner      |
| (1, -1)    | Bottom-right corner  |

---

### Cairo RGBA Normalization

- **Function:** `cairo_set_source_rgba(context, R, G, B, A)`
- **Range:** `[0.0, 1.0]` for each channel (Red, Green, Blue, Alpha)
- **Purpose:** Defines **color** and **opacity**
- **Standardized:** Common in graphics libraries like OpenGL, SVG, CSS

#### ✅ Example:
```c
cairo_set_source_rgba(ctx, 1.0, 0.0, 0.0, 0.5); // Semi-transparent red
```

### 🔍 Summary: Why the Difference?

| Feature                    | Purpose             | Normalization Range | Center-Based? | Usage Context         |
|----------------------------|---------------------|----------------------|----------------|------------------------|
| `AXOVERLAY_CUSTOM_NORMALIZED` | Positioning overlays | `[-1, 1]`              | ✅ Yes         | Spatial (video frame) |
| `cairo_set_source_rgba`       | Drawing colors      | `[0.0, 1.0]`           | ❌ No          | Color (graphics API)  |

---

#### ✅ Final Notes

- **Overlay coordinates** define **where** something is drawn.
- **Cairo RGBA values** define **how it looks** (color + transparency).
- The two are used together but normalized **differently** for logical and technical reasons.
