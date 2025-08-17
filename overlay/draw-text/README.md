# Overlay API - Draw text

This sample shows how to use the Axis axoverlay API with the **Cairo image backend** to render animated text over the video stream. It demonstrates:

- Initializing axoverlay (Cairo backend) and registering **render** and **adjustment callbacks**.
- Creating an **ARGB32** overlay sized to the stream’s max resolution.
- Drawing text with Cairo (cairo_show_text) and updating it via a GLib timer (g_timeout_add_seconds).
- Triggering redraws with axoverlay_redraw on every timer tick.

## What the app does

- Verifies AXOVERLAY_CAIRO_IMAGE_BACKEND support.
- Initializes axoverlay and creates one ARGB32 overlay covering the stream.
- Starts a 1-second animation timer that:

    - Counts down from 10 to 0 (then wraps).
    - Changes the text color depending on the counter:

        - 10–8 → green
        - 7–4 → blue
        - 3–0 → red

    - Calls axoverlay_redraw() to re-render.

- In render_overlay_cb, draws centered text like:
Countdown 7

## Key code to read

- `setup_axoverlay_data`: Normalized positioning, anchor center.
- `adjustment_cb`: Updates overlay dimensions on resolution/rotation change.
- `render_overlay_cb`: Uses Cairo to draw the countdown text, centered.
- `update_overlay_cb`: Timer callback that updates the counter/color and requests a redraw.


## Initialize and setup an overlay_data struct with default values.

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

## Color space settings for text: ARGB32

```c

    struct axoverlay_overlay_data data_text;
    setup_axoverlay_data(&data_text);
    data_text.width      = camera_width;
    data_text.height     = camera_height;
    data_text.colorspace = AXOVERLAY_COLORSPACE_ARGB32;
    overlay_id_text      = axoverlay_create_overlay(&data_text, NULL, &error_text);
    
```


## Draw text
---
```c
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
```
---

## Lab: run & verify

1. Start the app from the camera’s Apps page, then open Live View.

2. Check:

-  Text “Countdown N” appears roughly centered.
-  The number decrements every second.
-  The text color band switches as N changes (green → blue → red).
-  Changing resolution or rotation keeps the text centered (watch syslog for adjustment logs).

## Configuration (in code)

Inside draw_text / render_overlay_cb:

- Font: "serif", bold, size 32.0.
- Centering: computed via cairo_text_extents and overlay width/height.
- Color: global RGBColor color set by update_overlay_cb.

To tweak:

1. Change font family/size in draw_text.
2. Adjust position (e.g., overlay_width / 2, overlay_height / 2) to move the label.
3. Modify the interval in g_timeout_add_seconds(1, ...).


## Build

```bash
docker build --tag draw-text --build-arg ARCH=aarch64 .
```

```bash
docker cp $(docker create draw-text):/opt/app ./build
```