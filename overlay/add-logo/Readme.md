# Add Logo normalized coordinates


## Description

Upload a logo and set in corner positions with normalized coordinates

## Draw logo:

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

## Add your logo in your Dockerfile

---
```dockerfile

RUN . /opt/axis/acapsdk/environment-setup* && acap-build . -a 'axis_tip_logo.png'

```
---
## Build

---
```bash
docker build --tag add-logo-normal --build-arg ARCH=aarch64 .
```
---
```bash
docker cp $(docker create add-logo-normal):/opt/app ./build
```
---