# BBox View — Minimal Overlay Sample (GLib main loop)

This sample shows how to draw **bounding boxes** on a camera view using the Axis **BBox** overlay library. It creates a *single* BBox context for **view 1**, draws a red rectangle, and keeps a small GLib main loop running so the overlay persists until you stop the app. On shutdown, it clears the overlay and exits cleanly.


---

## What it demonstrates

- Creating a BBox handle bound to a **view**: `bbox_view_new(1u)`
- Setting visual style: outline, thin, color (red)
- Drawing geometry: `bbox_rectangle(x1, y1, x2, y2)`
- Applying the overlay atomically: `bbox_commit(...)`
- Clearing overlays on shutdown
- Using a **GLib main loop** and UNIX signal handlers for graceful teardown

---

## Code walkthrough

### 1) Panic helper
A tiny logger+exit helper:
```c
__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsyslog(LOG_ERR, fmt, ap); va_end(ap);
    exit(EXIT_FAILURE);
}
```

### 2) Draw on a single view
```c
static void single_channel(void) {
    bbox_t* bbox = bbox_view_new(1u);        // bind to view 1
    if (!bbox) panic("Failed creating: %s", strerror(errno));

    // Coordinates: choose your space. If you want normalized [0..1], enable:
    // bbox_coordinates_frame_normalized(bbox);

    bbox_clear(bbox);                         // start fresh

    const bbox_color_t red = bbox_color_from_rgb(0xff, 0x00, 0x00);
    bbox_style_outline(bbox);
    bbox_thickness_thin(bbox);
    bbox_color(bbox, red);

    // Rectangle corners (x1,y1,x2,y2). If normalized mode is enabled, use [0..1].
    bbox_rectangle(bbox, 0.05, 0.05, 0.95, 0.95);

    if (!bbox_commit(bbox, 0u))              // 0 => apply immediately
        panic("Failed committing: %s", strerror(errno));
}
```
> **Note:** If you do **not** call `bbox_coordinates_frame_normalized(bbox)`, many SDKs interpret coordinates as **pixels**. For normalized [0..1], uncomment the call.

### 3) Clear overlays
```c
static void clear(void) {
    bbox_t* bbox = bbox_new(1u, 1u); // create a fresh handle (view/layer),
    if (!bbox) panic("Failed creating: %s", strerror(errno));

    bbox_clear(bbox);
    if (!bbox_commit(bbox, 0u))
        panic("Failed committing: %s", strerror(errno));
    bbox_destroy(bbox);
}
```

### 4) Signals + GLib loop
```c
static gboolean signal_handler(gpointer loop) {
    g_main_loop_quit((GMainLoop*)loop);
    clear();
    syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");
    return G_SOURCE_REMOVE;
}

int main(void) {
    openlog(NULL, LOG_PID, LOG_USER);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT,  signal_handler, loop);

    single_channel();          // draw once
    g_main_loop_run(loop);     // keep overlay until user stops app

    clear();                   // clear again just-in-case
    return EXIT_SUCCESS;
}
```

---

## Build 

## Build

```bash
docker build --tag bbox-view --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-view):/opt/app ./build
```
---

## Lab ideas

1. **Normalized vs pixels**  
   - Uncomment `bbox_coordinates_frame_normalized(bbox)` and use values in `[0..1]`.  
   - Comment it out and try pixel coordinates (e.g., `bbox_rectangle(bbox, 50, 50, 600, 350)` for a 640×360 stream).

2. **Style variations**  
   - Try `bbox_style_fill(bbox)` and different line thickness (`bbox_thickness_medium`, `bbox_thickness_thick`).  
   - Change color dynamically with `bbox_color(bbox, bbox_color_from_rgb(r,g,b))`.

3. **Multiple shapes**  
   - Queue several rectangles before `bbox_commit(bbox, 0u)`; commit applies all of them atomically.  
   - Add `bbox_line`/`bbox_circle` calls if available in your SDK version.

4. **Animate**  
   - Replace the single draw with a small timer (e.g., `g_timeout_add()`) that updates positions then commits.  
   - Keep redraw rate modest (e.g., 10–15 Hz) to avoid excessive CPU.

5. **Other views**  
   - Try `bbox_view_new(0u)` or higher view IDs depending on your device.  
   - If nothing appears, ensure **application overlays** are enabled for that view/stream.

---



## Clean shutdown

This sample uses a GLib main loop to keep the overlay on screen until you stop the app (e.g., via the ACAP UI or SIGINT). On exit, it **clears** the overlay so the view returns to a clean state.

---


