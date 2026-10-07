# Bounding Box API

BBox draws box graphics into selected camera views. It does not detect or track
objects. The application supplies the coordinates and chooses when to redraw.

## Coordinates

| Mode | Meaning |
| --- | --- |
| `bbox_coordinates_frame_normalized()` | Coordinates follow the displayed frame: top-left (0,0), bottom-right (1,1). |
| `bbox_coordinates_scene_normalized()` | Default scene-normalized coordinates account for global image rotation. |

Scene normalization does not provide world coordinates, compensate for camera
movement, or track objects across cameras. Those tasks require additional
geometry, calibration or tracking logic. Detector coordinates must first be
mapped from the model input (including crop, resize or letterboxing) to the view
where the boxes will be drawn.

For a UI rectangle specified as percentages of the displayed frame, explicitly
use `bbox_coordinates_frame_normalized()`. The restricted-zone demo does this.

See the [BBox API reference](https://developer.axis.com/acap/api/src/api/bbox/html/bbox_8h.html).

## Bbox flow:

### 1 - Select view to draw

#### single view (select which channel)
---
```c
bbox_t* bbox = bbox_view_new(1u);

```
#### Multi view (channel 1 & 2)

```c
bbox_t* bbox = bbox_new(2u, 1u, 2u);

```

If the video channel output selected is not present this call will succeed and not block the application. Good for multiviews or selected a view it is not the main one.

```c

if (!bbox_video_output(bbox, true))
    panic("Failed enabling video-output: %s", strerror(errno));

```
---
### 2 - Select normalization type
---
```c
bbox_coordinates_scene_normalized(bbox);

```
or

```c
bbox_coordinates_frame_normalized(bbox);

```
---

### 3 - Clear old bounding boxes

---

```c
bbox_clear(bbox);
```
---

### 4 - Configure/create colors
---

```c
const bbox_color_t red   = bbox_color_from_rgb(0xff, 0x00, 0x00);
const bbox_color_t blue  = bbox_color_from_rgb(0x00, 0x00, 0xff);
const bbox_color_t green = bbox_color_from_rgb(0x00, 0xff, 0x00);
```
---

### 5 - Draw with bbox

```c

    bbox_style_outline(bbox);                      // Switch to outline style
    bbox_thickness_thin(bbox);                     // Switch to thin lines
    bbox_color(bbox, red);                         // Switch to red [This operation is fast!]
    bbox_rectangle(bbox, 0.05, 0.05, 0.95, 0.95);  // Draw a thin red outline rectangle

    bbox_commit(bbox, 0u)

```

or all bboxes together

```c

    bbox_style_corners(bbox);                      // Switch to corners style
    bbox_thickness_thick(bbox);                    // Switch to thick lines
    bbox_color(bbox, blue);                        // Switch to blue [This operation is fast!]
    bbox_rectangle(bbox, 0.40, 0.40, 0.60, 0.60);  // Draw thick blue corners

    bbox_style_corners(bbox);                      // Switch to corners style
    bbox_thickness_medium(bbox);                   // Switch to medium lines
    bbox_color(bbox, blue);                        // Switch to blue [This operation is fast!]
    bbox_rectangle(bbox, 0.30, 0.30, 0.50, 0.50);  // Draw medium blue corners

    bbox_style_outline(bbox);   // Switch to outline style
    bbox_thickness_thin(bbox);  // Switch to thin lines
    bbox_color(bbox, red);      // Switch to red [This operation is fast!]

    // Draw a thin red quadrilateral
    bbox_quad(bbox, 0.10, 0.10, 0.30, 0.12, 0.28, 0.28, 0.11, 0.30);

    // Draw a green polyline
    bbox_color(bbox, green);  // Switch to green [This operation is fast!]
    bbox_move_to(bbox, 0.2, 0.2);
    bbox_line_to(bbox, 0.5, 0.5);
    bbox_line_to(bbox, 0.8, 0.4);
    bbox_draw_path(bbox);

    // Draw all queued geometry simultaneously
    bbox_commit(bbox, 0u)
```
### 6 - Destroy

```c

    bbox_destroy(bbox);

```