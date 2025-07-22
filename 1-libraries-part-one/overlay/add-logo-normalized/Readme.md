# Draw rectangle normalized coordinates


## Description

Upload a logo and set in corner positions with normalized coordinates

## Draw logo:

---
```c
static void draw_logo(cairo_t *context,
                        const char *image_path, 
                        gfloat center_x,         // center of the logo, further right - logo_width/2 (x = 1.0 - (width / 2))
                        gfloat center_y,         // center of logo, further top right - logo_height/2 (y = 0.0 + (height / 2))
                        gfloat norm_width,       // 0.2
                        gfloat norm_height,      // 0.1  => 2:1 ratio
                        gint overlay_width,
                        gint overlay_height){

    // Load logo
    cairo_surface_t *logo = cairo_image_surface_create_from_png(image_path);
    cairo_status_t status = cairo_surface_status(logo);
    if (status != CAIRO_STATUS_SUCCESS)
    {
        syslog(LOG_ERR, "Failed to load logo image: %s", cairo_status_to_string(status));
        cairo_surface_destroy(logo);
        return;
    }
    syslog(LOG_INFO, "Logo loaded successfully... %s", cairo_status_to_string(status));

    int logo_width = cairo_image_surface_get_width(logo);
    int logo_height = cairo_image_surface_get_height(logo);

    syslog(LOG_INFO, "Logo dimensions: %d x %d", logo_width, logo_height);

    gdouble draw_width = norm_width * overlay_width;
    gdouble draw_height = norm_height * overlay_height;
    gdouble draw_x = (center_x * overlay_width) - draw_width / 2;
    gdouble draw_y = (center_y * overlay_height) - draw_height / 2;

    // Save current state
    cairo_save(context);

    // Translate to position
    cairo_translate(context, draw_x, draw_y);

    // Scale image to fit target size
    cairo_scale(context, draw_width / logo_width, draw_height / logo_height);

    // Paint image at 0,0 in scaled context
    cairo_set_source_surface(context, logo, 0, 0);
    cairo_paint(context);

    // Restore context
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