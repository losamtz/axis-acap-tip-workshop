# Overlay API - Add Logo 

This sample shows how to use the Axis axoverlay API with the Cairo image backend to render a PNG logo on top of the video stream. It demonstrates:

- Initializing axoverlay (Cairo backend) and registering render and adjustment callbacks.
- Creating an overlay that covers the stream using custom normalized positioning.
- Loading a PNG and drawing it in the top-right corner with normalized padding and size so it scales with resolution/rotation.

## What the app does

- Checks that AXOVERLAY_CAIRO_IMAGE_BACKEND is supported.
- Initializes axoverlay and creates a single overlay sized to the stream’s max resolution.
- In render_overlay_cb it loads a PNG logo and draws it at top-right:

    - Padding (normalized): pad_x = 0.02, pad_y = 0.02
    - Normalized width: 0.1 (height preserves the image aspect ratio)

- In adjustment_cb, the overlay width/height are updated when stream resolution or rotation changes.

**Image path used by the sample**:
/usr/local/packages/add_logon/axis_tip_logo.png
Make sure this file exists on the device (see “Add your logo in your Dockerfile”).

## Important functions

- `render_overlay_cb`: loads and draws the PNG via Cairo (`cairo_image_surface_create_from_png`).
- `draw_logo`: computes normalized placement/size → pixel coords, maintains aspect, paints with Cairo.
- `adjustment_cb`: updates overlay dimensions if resolution/rotation changed.
- `setup_axoverlay_data`: sets `AXOVERLAY_CUSTOM_NORMALIZED`, anchor center, etc.


## Sample code flow

### Draw logo:

---
```c
static void draw_logo(cairo_t *context,
                      const char *image_path,
                      gfloat pad_x, gfloat pad_y,       // Normalized padding from edge
                      gfloat norm_width,                // Max normalized width
                      gint overlay_width,
                      gint overlay_height) {
    cairo_surface_t *logo = cairo_image_surface_create_from_png(image_path);
    cairo_status_t status = cairo_surface_status(logo);
    if (status != CAIRO_STATUS_SUCCESS) {
        syslog(LOG_ERR, "Failed to load logo image: %s", cairo_status_to_string(status));
        cairo_surface_destroy(logo);
        return;
    }

    gint img_w = cairo_image_surface_get_width(logo);
    gint img_h = cairo_image_surface_get_height(logo);

    gdouble aspect = (gdouble)img_h / img_w;

    // Compute max width in pixels (from normalized width)
    gdouble max_draw_width = norm_width * overlay_width;
    gdouble max_draw_height = max_draw_width * aspect;

    // Compute normalized center (with padding and center alignment)
    gdouble norm_center_x = 1.0 - (max_draw_width / (2.0 * overlay_width)) - pad_x;
    gdouble norm_center_y = 0.0 + (max_draw_height / (2.0 * overlay_height)) + pad_y;

    // Convert to pixel position
    gdouble draw_x = norm_center_x * overlay_width - (max_draw_width / 2.0);
    gdouble draw_y = norm_center_y * overlay_height - (max_draw_height / 2.0);

    // Draw image
    cairo_save(context);
    cairo_translate(context, draw_x, draw_y);
    cairo_scale(context, max_draw_width / img_w, max_draw_height / img_h);
    cairo_set_source_surface(context, logo, 0, 0);
    cairo_paint(context);
    cairo_restore(context);

    cairo_surface_destroy(logo);
}
```

---

## LAB

1. Start the app from the camera’s Apps page (or via CLI). Open Live View.


 - A logo appears at the top-right of the video.
 - The logo scales with different resolutions.
 - The logo stays correctly placed when the stream is rotated.


2. If you change resolution/rotation, watch syslog for the adjustment logs.

---

### Add your logo in your Dockerfile

---
```dockerfile

RUN . /opt/axis/acapsdk/environment-setup* && acap-build . -a 'axis_tip_logo.png'

```
---
### Build

---
```bash
docker build --tag add-logo --build-arg ARCH=aarch64 .
```
---
```bash
docker cp $(docker create add-logo):/opt/app ./build
```
---