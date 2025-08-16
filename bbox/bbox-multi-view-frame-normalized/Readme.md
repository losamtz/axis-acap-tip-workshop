# Draw multi-view with frame normalized coordinates


## Description

Draw a yellow box moving from axis [0.0,0.3]  to [1.0,0.3] and back in several view areas, simulating object tracking in the scene.

Note: Using object detection models, the coordenates are provided to be able to draw the the boxes. THis case just simulates that scenario.

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
docker build --tag bbox-multi-view-fn --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-multi-view-fn):/opt/app ./build
```