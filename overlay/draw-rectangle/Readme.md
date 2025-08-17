# Draw rectangle

This sample demonstrates how to render a centered rectangle overlay on the video using the Axis axoverlay API with the Cairo image backend and a 4-bit palette (index-based colors). It also shows how to use AXOVERLAY_CUSTOM_NORMALIZED positioning, the adjustment callback (to react to stream/rotation changes), and a clean render path with cairo.

## What this app does

- Initializes **axoverlay** with the **Cairo image backend**.
- Sets up a **4-bit palette** (indices → ARGB via axoverlay_set_palette_color).
- Creates one overlay that covers the full stream (using **custom normalized positioning** and **anchor center**).

- On every render:

    - Clears the overlay buffer (palette index 0).
    - raws a **yellow** rectangle centered in the overlay (¼ width × ¼ height).

- Reacts to rotation / resolution changes via adjustment_cb, keeping the overlay’s drawing area aligned to the stream.

## Key concepts in this sample

- **Positioning & normalization**

    - `data.postype = AXOVERLAY_CUSTOM_NORMALIZED`
    - `data.anchor_point = AXOVERLAY_ANCHOR_CENTER`
    - Normalized position lets the overlay stay consistent across resolutions and rotations.

- **Palette colors**

    - `AXOVERLAY_COLORSPACE_4BIT_PALETTE` (16 indices)
    - Use `axoverlay_set_palette_color(index, {r,g,b,a})`, then convert index→cairo color in index2cairo.

- **Cairo backend**

    - Draw with Cairo (cairo_t*) in render_overlay_cb.
    - Clear, stroke, fill using palette-derived RGBA.

**Adjustment callback**

    - adjustment_cb updates overlay width/height if stream resolution or rotation changes.

## Lab

Open Live View and check:

- A centered rectangle appears on top of the video.
- It remains centered and scales appropriately when you change resolution.
- It remains correct when you rotate the video (orientation/rotation settings).

## Code Flow

### Initialize and setup an overlay_data struct with default values.

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

### Color space settings for text: 4 bit pallette

```c

    struct axoverlay_overlay_data data_text;
    setup_axoverlay_data(&data_text);
    data_text.width      = camera_width;
    data_text.height     = camera_height;
    data_text.colorspace = AXOVERLAY_COLORSPACE_4BIT_PALETTE;
    overlay_id_text      = axoverlay_create_overlay(&data_text, NULL, &error_text);
    
```

### Converts palette color index to cairo color value

```c
#define PALETTE_VALUE_RANGE 255.0

static gdouble index2cairo(const gint color_index) {
    return ((color_index << 4) + color_index) / PALETTE_VALUE_RANGE;
}
```

### Setup palette color

```c

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

```

### Drawing rectangle using cairo and later called by render_cb

```c
static void draw_rectangle(cairo_t* context,
                           gint left,
                           gint top,
                           gint right,
                           gint bottom,
                           gint color_index,
                           gint line_width) {
    gdouble val = 0;

    val = index2cairo(color_index);
    cairo_set_source_rgba(context, val, val, val, val);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width(context, line_width);
    cairo_rectangle(context, left, top, right - left, bottom - top);
    cairo_stroke(context);
}

```

### Build

```bash
docker build --tag draw-rectangle --build-arg ARCH=aarch64 .
```

```bash
docker cp $(docker create draw-rectangle):/opt/app ./build
```