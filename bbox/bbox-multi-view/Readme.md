# Overla API - bbox-multi-view (moving rectangle)

This sample demonstrates how to draw and animate a bounding box on multiple views of an Axis multi-sensor device using the bbox API. A yellow rectangle moves horizontally back and forth, and is rendered on channels 1, 2, 3, 4.

## What it does

- Creates a bbox handle that targets four views: bbox_new(4u, 1u, 2u, 3u, 4u).
- Enables on-screen video output for the OSD overlay (bbox_video_output(bbox, true)).
- Clears any previous graphics on each update.
- Queues a yellow rectangle with medium thickness and corner style.
- Moves the rectangle horizontally between x = 0.0 and x = 1.0 - width, bouncing at the edges.
- Commits the frame with bbox_commit(bbox, 0u) so all drawing appears atomically.
- Runs in a GLib main loop and stops cleanly on SIGINT/SIGTERM (clears overlays on exit).

## Files overview

- main.c (provided snippet)

    - `update_bbox()` — animation tick: create handle, set style, compute new x-position, draw rect, commit, destroy handle.
    - `clear()` — clears any remaining overlays on channel 1 before exit.
    - `signal_handler()` — stops main loop and calls clear().
    - `main()` — sets up syslog, GLib main loop, signal handlers, and the periodic timer via g_timeout_add(100, update_bbox, NULL).

# Important notes

- The sample calls `g_timeout_add(100, update_bbox, NULL)` (10 FPS), but `update_bbox()` also calls sleep(1). That effectively throttles updates to ~1 FPS and blocks the main loop during sleep.

    - Recommendation: remove the sleep(1) and rely solely on the GLib timer interval to control the animation rate.

- Creating/destroying a bbox handle every tick is simple but not optimal. For smoother animation:
    - Create the handle once during init;
    - Reuse it on each tick (clear → draw → commit);
    - Destroy it on shutdown.

## Lab

1. You can tweak these constants near the top:

```c
static double xpos = 0.0;        // start x
static const double box_width = 0.1;
static const double y = 0.3;
static const double height = 0.1;
static int dir = -1;             // -1 left, +1 right
```

2 And change color/styling:

```c
const bbox_color_t yellow = bbox_color_from_rgb(0xff, 0xff, 0x00);
bbox_style_corners(bbox);
bbox_thickness_medium(bbox);
bbox_color(bbox, yellow);

```
## Create a bbox on channel 1, 2, 3 and 4

```c
bbox_t* bbox = bbox_new(4u, 1u, 2u, 3u, 4u);

```

## If the video channel output selected is not present this call will succeed and not block the application. Good for multiviews or selected a view it is not the main one.

```c

if (!bbox_video_output(bbox, true))
    panic("Failed enabling video-output: %s", strerror(errno));

```

## Set frame normalized
---

```c
bbox_coordinates_scene_normalized(bbox);

```
---

## Clear old bboxes

```c
bbox_clear(bbox);  // Remove all old bounding-boxes
```
---

## Create needed colors

```c
const bbox_color_t red   = bbox_color_from_rgb(0xff, 0x00, 0x00);
const bbox_color_t blue  = bbox_color_from_rgb(0x00, 0x00, 0xff);
const bbox_color_t green = bbox_color_from_rgb(0x00, 0xff, 0x00);
```
---

## Setup a rectangle

```c
bbox_style_outline(bbox);                      // Switch to outline style
bbox_thickness_thin(bbox);                     // Switch to thin lines
bbox_color(bbox, red);                         // Switch to red [This operation is fast!]
bbox_rectangle(bbox, 0.05, 0.05, 0.95, 0.95);  // Draw a thin red outline rectangle

```
---

## Draw the rectangle

```c
bbox_commit(bbox, 0u)

```
---

## Destroy object

```c
bbox_destroy(bbox);
```

---

## Build

```bash
docker build --tag bbox-multi-view --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-multi-view):/opt/app ./build
```