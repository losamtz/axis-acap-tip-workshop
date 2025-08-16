# Draw rectangle normalized coordinates


## Description

Draw text using cairo and colorspace ARGB32 with normalized coordinates (initial axoverlay setup).


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
## Build

```bash
docker build --tag draw-text --build-arg ARCH=aarch64 .
```

```bash
docker cp $(docker create draw-text):/opt/app ./build
```