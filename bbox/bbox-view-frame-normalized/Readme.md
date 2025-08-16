# Draw view with scene normalized coordinates


## Description

Draw a fixed red rectangle with frame normalization. In this case we are using GMainLoop to keep the rendering permanent, and destroy it only when exiting the application.

## Create a bbox on channel 1

```c
bbox_t* bbox = bbox_view_new(1u);

```

## Set frame normalized
---

```c
bbox_coordinates_frame_normalized(bbox);

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
docker build --tag bbox-view-framen --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-view-framen):/opt/app ./build
```