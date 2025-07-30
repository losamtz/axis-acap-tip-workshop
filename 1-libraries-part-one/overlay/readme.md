# Overlay

The overlay API lets you add and manage graphic elements like text, images, or custom art on the video stream from an Axis camera. You can control these overlays either directly from an ACAP application using the low-level API or remotely with a high-level API via the network.

In this guide, we will walk through the steps of using the axoverlay API in ACAP applications, detailing each feature, and providing examples for better understanding. We will explore how to initialize the system, create a rendering callback, configure the overlay surface, drawing objects and thereby making the learning process straightforward and efficient.

But first, let's start with a bit of background knowledge.

## Different Ways to Draw Graphics on top of a Video Stream


#### Drawing in the Front End vs. In the Back End

Drawing in the front end, like a web interface, offers design flexibility. But, it requires an object detection stream that you can sync with the video which might not always be trivial. Further, if you draw on the front end, your overlays won't show up outside the browser live view. If you need overlays to persist on recordings or in third-party VMS systems, drawing in the back end with the camera APIs might be a good option, this will include the overlay directly in the video stream. The downside is that it may be harder to access the original video without the overlay info and it is harder to create content that is interactive with the user.

#### A Note on the VAPIX APIs

VAPIX is the Axis camera's network APIs. While both VAPIX and SDK APIs serve ACAP applications, their uses vary. The overlay API in VAPIX can include images and text in overlays but is limited in supporting other types like bounding boxes. A significant benefit is that it can be accessed through any device connected to the same network as the camera. SDK API, in contrast, serves ACAP applications in the camera and is only available as a C API.

The VAPIX overlays are designed to be rarely changed and can thus be very slow to take effect. There are dynamic overlays, like e.g. timestamps, where you configure a placeholder that will automatically update. Calling the VAPIX overlay API every second to update the timestamp would not be a good idea. The C APIs are however created in a way that they can be redrawn frequently, they are thus suitable for dynamic content like visualization of the output from an object detector.

A benefit with the VAPIX API is that it has a wide support and is working on e.g. Ambarella CV25 and Ambarella S2L cameras.


#### Choosing Between the Back Ends

When drawing content using the C API in the SDK, there are three different backends. These backends are how you explain to the API what content it should draw on top of the video stream. The supported backends are:

    • Cairo image backend
    • OpenCL backend
    • Raw-pointer backend

Selecting the correct backend for ACAP applications is an important task as it influences overlay rendering performance and capabilities. Some aspects to consider are:

    **• Hardware support**: Cairo is supported by ARTPEC 6, 7, and 8 cameras, while OpenCL is supported by ARTPEC 7 and 8 only.
    **• For basic drawing**: Opt for Cairo when you want to draw basic shapes like lines, squares, text, etc. This is a simple interface with good portability.
    **• For heavy processing**: If your rendering is compute-intensive or demands parallel execution, the OpenCL backend is recommended.
    **• For large data**: To draw pixel masks, segmentation masks, picture-in-picture, etc, opt for the raw pointers. This allows you to write the pixel values directly into the buffer.

## Overlay API Basics

The first step when using the library is to configure your compilation to link to the axoverlay library. You can do this by specifying -laxoverlay to the linker.
Next include the header file for the library:
```c
# include <axoverlay.h>
```
When you have decided for which backend to use, it is good practice to verify that the backend exists for the particular camera the application is running in. You can do that with the axoverlay_is_backend_supported function. E.g. When using the Cairo library you would check:

```c
if (!axoverlay_is_backend_supported(AXOVERLAY_CAIRO_IMAGE_BACKEND)) {
  // Handle the error
  return false;
}
```
If you are going to use the Cairo functions, then you also need to include the appropriate header file:

```c
# include <cairo/cairo.h>

```

## Drawing Simple Shapes Using Cairo


This part of the guide will dive into how you can leverage the axoverlay API in tandem with the Cairo backend. We're going to draw simple shapes, like lines or bounding boxes, that will appear on the camera stream. This procedure comprises several stages, which involve initialization of the library, configuration of the overlay surface, and the utilization of Cairo to draw the shapes. We'll delve into each one of these steps.

#### Initialization and Setup

The first step is to initialize the library. In this step we will select the backend to use (Cairo in this example) and configure the callback functions from the library.

In order to initialize the library using the axoverlay_init function we need to configure an axoverlay_settings struct.The axoverlay API depends on different callbacks for handling the drawing logic, the settings struct is where these are registered. There are multiple callbacks can that be useful to know about, but to keep it simple we will only use the render_callback for now. This is the callback that will be called every time there is a new video stream that should be drawn on. Note that the we will define the render_overlay_cb function soon.

```c
GError* error = NULL;
 
struct axoverlay_settings settings = {0};
axoverlay_init_axoverlay_settings(&settings);

settings.render_callback = render_overlay_cb;
settings.backend = AXOVERLAY_CAIRO_IMAGE_BACKEND;

axoverlay_init(&settings, &error);
if (error != NULL) {
  // handle errors
  g_error_free(error);
  return false;
}
```

#### Configuring the overlay surface

Before we proceed with the explanation of the aforementioned callback, a necessary step we need to make is configuring the characteristics of our overlay. In order to do that, we need to create and configure an axoverlay_overlay_data struct. This structure contains important configurations and settings, like the width, height, anchor point, color space etc., of a specific overlay.

First we create the struct and call the library function axoverlay_init_overlay_data to initialize the defaults.

```c
struct axoverlay_overlay_data data = {0};
axoverlay_init_overlay_data(&data);
```

In the next step we will set the size and location of the overlay. We can chose what kind of coordinates to use, in this example we will opt for normalized coordinates where -1 is to the left or top and 1 is to the right or bottom. Using normalized coordinates makes it easy to place the overlay at a specific location regardless of the stream resolution. We will also set an anchor point, the is the point of the overlay surface that will be placed on the coordinate we specify. Finally we set the X and Y position where the anchor point will be placed.

```c
data.postype = AXOVERLAY_CUSTOM_NORMALIZED;
data.anchor_point = AXOVERLAY_ANCHOR_CENTER;
data.x = 0.0;
data.y = 0.0;
data.width = 300;
data.height = 300;

```

In the example above, we will create an overlay centered on the stream with the size 300x300 pixels. This is visualized in the following image. Note that the stream resolution in this example is 1920x1080, but this might be different for different stream. The overlay will however always be 300x300 pixels, and will thus cover different proportions of the stream.

<IMAGE>

Since the anchor point is in the middle of the overlay surface (as specified by AXOVERLAY_ANCHOR_CENTER), if we would have specified data.x = -1 and data.y = -1, then only one forth of the overlay surface would be visible, as the overlays center would be placed in the top-left corner of the stream. This is visualized in the following image.

<IMAGE>

Next we will configure the color space that will be used. This specifies how we will tell the API which color a specific element will be drawn in. The options fall between using a predeclared color palette or by specifying the color values for each element. Predeclaring a color palette requires a bit extra setup but could potentially be more efficient. For this example we will instead specify the color as an RGBA value for each element. An example of that would be (1, 0, 0, 0.5) which would be a red element with 50% opacity.

```c
data.colorspace = AXOVERLAY_COLORSPACE_ARGB32;

```
The final step is to create an overlay with the configuration we have now created. This is done with the library function axoverlay_create_overlay which takes a reference to the data struct.

```c

int overlay_id = axoverlay_create_overlay(&data, NULL, &error);
if (error != NULL) {
  // Handle errors
  g_error_free(error);
  return false;
}

```

#### Creating the Rendering Callback 

The rendering callback is a function that will be called when a frame is available and the callback is responsible for actually drawing the content onto that frame's overlay surface. It is called every time a new stream is created or when the axoverlay_redraw library function is called. The callback will receive an empty surface.
An example of how the rendering callback function can be defined to draw a rectangle using Cairo is provided below.

```c
static void render_overlay_cb(gpointer rendering_context,
                              const gint id,
                              const struct axoverlay_stream_data* stream,
                              const enum axoverlay_position_type postype,
                              const gfloat overlay_x,
                              const gfloat overlay_y,
                              const gint overlay_width,
                              const gint overlay_height,
                              const gpointer user_data) {
  cairo_t *context = (cairo_t *)rendering_context;

  cairo_set_source_rgba(context, 0, 1, 0, 0.75);
  cairo_set_line_width(context, 5);
  cairo_rectangle(context, 0, 0, 150, 150);
  cairo_stroke(context);
}
```

The callback function will receive a lot of information about the stream that the surface will be overlaid onto. In this example we do not care about this information. Instead we are only drawing a rectangle at position (0, 0) with width 150 pixels and height 150 pixels. It's worth to note that the coordinates here are not normalized, they are instead pixels with (0, 0) in the top-left corner of the overlay surface. The rectangle is drawn with a 5 pixel line width in green with 75% opacity.

<IMAGE>

Whenever a new stream is created this callback function will be called. This draws the overlay graphics on the surface. This overlay surface will then stay ontop of the stream for as long as the stream exists. If you want to redraw the graphics, you need to call the axoverlay_redraw function which will inturn cause the rendering callback to be called again. You could e.g. create a timer function that calls this function a specific number of times every second or you could call it from your main loop.

#### Cleanup

Before exiting the program it is good practice to cleanup the axoverlay related resources. These resources include both the created overlays and the library specific resources.

```c
// Cleanup the overlay
axoverlay_destroy_overlay(overlay_id, &error);
if (error != NULL) {
  // Handle errors
  g_error_free(error);
  return false;
}
  
axoverlay_cleanup();
```

## Summary

n this lesson the axoverlay API that enables the addition and control of graphic elements such as text, images, or custom art on the video stream was introduced. We have covered the basics of overlay rendering and control. The lesson detailed the steps needed for utilizing the Overlay API in AXIS ACAP applications, covering initialization, rendering callbacks, overlay surface configuration, and cleanup procedures. This should make it easy for you to add the first overlay to your own ACAP.

Additionally, we discussed considerations when choosing between drawing graphics in the front end versus the back end, and provides insights into selecting appropriate backends for ACAP applications.

In the next lesson we will look into some more advanced concepts of handling overlays.

## Getting Advanced With Overlays

In the previous lesson we created our first overlay. Apart from understanding how the different functions should be used together, creating an overlay is relatively simple. The process might however become more complex when you start to incorporate it into a real world application. In this lesson we will look briefly at some of the complexities that might arise.

#### How About Scaling?

In our first overlay example we learnt how to create an overlay with a fixed size of 300x300 pixels on top of a video stream. We did however not look into what happens if the size of the stream differs. In the following image, we can see the same overlay rendered by the same ACAP in two different video streams, one stream has selected the resolution 1920x1080 and one stream has selected the resolution 640x360.

<IMAGE>

The 300x300 pixel overlay in a 1920x1080 stream.

<IMAGE>

The 300x300 pixel overlay in a 640x360 pixel stream.


We can clearly see that the overlay differ in size. This is explained by the fixed overlay size measured in pixels, 300 pixels height in a 360 pixel high stream will cover 83% of the stream height. Meanwhile, 300 pixels height in a 1080 high stream will only cover 28% of the stream height.

The result of this is that the box in the low-resolution image covers more of the physical things in the image than the box in the high resolution stream covers. This is a frequent challenge when drawing bounding boxes from a deep learning based object detector. Each box then refers to a specific region in the world, as seen by the deep learning model, and we must make sure that our overlay is rendered in the correct way.

We can do this in three ways:

    1. Only render the overlay on streams with a specific resolution. This means that we will either have a correct overlay or no overlay.
    2. Recalculate the coordinates to fit the resolution of the stream that you are drawing on.
    3. Enable the auto-scaling option so that the backend will do the scaling for us.

Method one can be implemented by using the select_callback that we will soon cover. Method two can be implemented by making use of the input arguments to the render_callback. This works since the render_callback is called for each new stream that is created. Method 3 is the simplest to implement. To do so, add this line to modify the data struct before calling the axoverlay_create_overlay function:

```c
data.scale_to_stream = true;
```

In the following image we can see another example when using this scaling option. The overlay surface has been configured to size 1440x1080 pixels (4:3 aspect ratio). The background is filled with translucent black and a red crosshair is rendered close to the top-left corner.

If we change to a 16:9 aspect ratio stream with 1024x576 resolution, we will see that the overlay is not stretched, but is instead not covering the full width of the stream. Similarly, if we created an overlay with 16:9 aspect ratio we would see that the overlay would actually extend outside the visible view. The overlay surface does therefore always have the same aspect ratio as you created it with and if you want to make sure that the content you draw is always visible, you will need to adjust the position of the content yourself.

We leave it as an exercise for you to see what happens to the location of the cross hair (in relation to the physical object behind it) if you try lower resolution streams with and without the scaling option set. You should see that the overlay surface is down scaled in proportion to the stream after drawing before it is applied to the stream, the drawn content should therefore be on top of the same physical object in the view.

#### Available Callback Functions

As mentioned before, axoverlay is using specific callback functions to handle all the drawing logic within an ACAP application. More specifically, the render_callback, the adjustment_callback and the select_callback. While the developer can adjust the implementation within those in any way that suits their needs, all these need to follow the pre-defined signatures provided in the documentation of the API.

The only callback that is mandatory is the render_callback, this makes sense since the API library would not perform any job if that callback wasn't implemented.

The select_callback, as explained above, can be used to decide if an overlay should be rendered on a specific stream or not. Whenever a new stream is created (e.g. a new browser tab is opened, or a new RTSP client connects), this function will be called. The input arguments contains information such as the rotation of the stream, the resolution of the stream, etc., and the return value of the function is a boolean stating if the overlay should be applied to the stream or not.

In the example from the lesson before, we created an overlay on a specific location with a specific size (300x300 pixels in the center of the stream). The configuration was done before creating the overlay. If you would like to change the position or the size of the overlay surface, you might do so with the adjustment_callback which is called before every rendering callback. Note that this should typically be avoided since doing so frequently is inefficient. In many cases it might be better to create a larger overlay from the beginning and adjusting the position within the overlay where an object is drawn.

#### When is an overlay rendered?

When using the axoverlay API, as mentioned before, an overlay is rendered through the render_callback whenever a new stream is created. In addition, axoverlay_redraw can be called to trigger a refreshment of the overlay which will call the render_callback.
In our simple example, we rendered the overlay only once when the screen was created. After that it remained static. In any cases you might want to draw dynamic content that moves around. You can achieve this by calling the axoverlay_redraw method e.g. once every iteration in your main loop (i.e. every time your deep learning model has been used for inference), or you could use functions such as the GLib g_timeout_add() along with the desired redraw time interval to trigger a time-based redraw.
--

--
## Extra details

Note:
It is preferable to use Palette color space for large overlays like plain boxes, to lower the memory usage. More detailed overlays like text overlays, should instead use ARGB32 color space.

###  Overview: axoverlay and Cairo roles

| Component  | Role                                                                                 |
|------------|--------------------------------------------------------------------------------------|
| **axoverlay** | The Axis-specific API that sets up overlays, handles video stream integration, resolution, positions, and render callbacks. |
| **Cairo**     | The graphics library used to draw on the overlay canvas (text, shapes, lines, images, etc.). |

---

### Typical Flow of Interaction

1. **`axoverlay_init()`**

    You initialize the overlay system with `axoverlay_settings`.

    - You register a `render_callback` which is called when the overlay should be drawn.
    - You can also set position types like `AXOVERLAY_CUSTOM_NORMALIZED`.

2. **`axoverlay_create_overlay()`**

    You create one or more overlays, specifying which stream and position to attach to.

3. **`render_callback()`**

    Every frame (or periodically), Axis calls your render callback, e.g.:

    ```c
    static void render_overlay_cb(gpointer rendering_context,
                                  gint id,
                                  struct axoverlay_stream_data* stream,
                                  enum axoverlay_position_type postype,
                                  gfloat overlay_x,
                                  gfloat overlay_y,
                                  gint overlay_width,
                                  gint overlay_height,
                                  gpointer user_data)
    {
        // This is where you use Cairo to draw on the overlay.
    }
    ```

---

### Cairo Drawing Inside the Callback

You use the passed `rendering_context` (which is a Cairo context: `cairo_t*`) to issue drawing commands:

```c
cairo_set_source_rgba(ctx, r, g, b, a);
cairo_rectangle(ctx, x, y, w, h);
cairo_fill(ctx);
```
These commands draw on a canvas that overlays the video stream.

---

### Execution Order Summary

```c
main() {
    axoverlay_init();                 // 1️⃣ Setup overlay system
    axoverlay_create_overlay();      // 2️⃣ Create overlays (attach to streams)
    ...                              // GLib main loop starts
}

render_callback(...) {
    // 3️⃣ Called automatically when it's time to render the overlay
    cairo_* functions;               // 4️⃣ Use Cairo to draw content
}
```

### How They Work Together

| axoverlay                               | Cairo                            |
|----------------------------------------|---------------------------------|
| Owns the overlay buffer and stream attachment. | Does the drawing itself.         |
| Provides resolution and context in callback.    | Uses that context to render.     |
| Handles scale, rotation, and placement.          | Ignores stream logic. Just draws.|
| Thinks in terms of streams.                        | Thinks in terms of 2D surfaces.  |

✅ Example Sequence

```c
// Main init
axoverlay_settings s = { ... };
axoverlay_init(&s, &error);         // Registers render callback
axoverlay_create_overlay(...);      // For camera stream 1

// Later, in callback (called by Axis)
render_overlay_cb(...) {
    cairo_set_source_rgb(ctx, 1, 1, 0);          // Yellow
    cairo_rectangle(ctx, 0, 0, 100, 50);         // Rectangle
    cairo_fill(ctx);                              // Render
}
```

## Normalized Coordinates in Axis ACAP Overlay

### What are Normalized Coordinates?

Instead of specifying overlay positions using absolute pixel values (e.g., 100 pixels from the left), normalized coordinates scale positions to a fixed range between **-1** and **1**, regardless of the video frame's resolution or size.

This means your overlay positioning becomes resolution-independent and consistent across different video sizes.

### Coordinate System Overview

- The coordinate system is centered in the middle of the video frame.
- The **x-axis** runs horizontally from **-1** (far left edge) to **1** (far right edge).
- The **y-axis** runs vertically from **-1** (bottom edge) to **1** (top edge).

### Visual Representation

---
```
       y = 1 (top)
         |
 (-1,1)  |  (1,1)
         |  
---------+--------- x = 1 (right)
         |  
 (-1,-1) |  (1,-1)
         |
       y = -1 (bottom)
```
---

- **x = -1** → far left edge of the frame  
- **x = 0** → horizontal center  
- **x = 1** → far right edge  
- **y = -1** → bottom edge  
- **y = 0** → vertical center  
- **y = 1** → top edge  

### Why Use Normalized Coordinates?

- **Resolution independence:** The same coordinates work on any video size or resolution.  
- **Consistent positioning:** (0, 0) is always the center.  
- **Easy corner placement:**  
  - (-1, 1) → top-left corner  
  - (1, -1) → bottom-right corner  
- **Smooth positioning:** Use decimal values to position anywhere in between, e.g., (0.5, -0.3).

### Summary Table

| Coordinate Value | Meaning            | Position on Frame              |
|------------------|--------------------|-------------------------------|
| -1               | Minimum normalized  | Left (for x), Bottom (for y)  |
| 0                | Centered           | Center                        |
| 1                | Maximum normalized  | Right (for x), Top (for y)     |


## Overlay Position Types

#### The different position types:

#### Corners (fixed positions):

- `AXOVERLAY_TOP_LEFT`
- `AXOVERLAY_TOP_RIGHT`
- `AXOVERLAY_BOTTOM_LEFT`
- `AXOVERLAY_BOTTOM_RIGHT`

For these four positions, any x and y coordinates you provide are ignored because the position is fixed at the corresponding corner of the video frame.

---

#### Custom Normalized Position:

- `AXOVERLAY_CUSTOM_NORMALIZED`

When using this, the overlay position is defined by x and y values normalized between -1 and 1.

- `-1` means the far left (for x) or bottom (for y)
- `1` means the far right (for x) or top (for y)

This allows placing the overlay anywhere in the frame using a normalized coordinate system.

---

#### Custom Source Position:

- `AXOVERLAY_CUSTOM_SOURCE`

Here, the overlay position is specified relative to the video source itself, not the whole video frame.

This means the coordinates are absolute pixel values ranging between 0 and the maximum width/height of the source.

This is especially useful if the video is rotated or if digital pan-tilt-zoom (DPTZ) is used, because then the overlay stays locked to the scene rather than moving with the video frame.

---

#### Summary

| Position Type               | Coordinates Usage               | Positioning Behavior                     |
|----------------------------|--------------------------------|-----------------------------------------|
| `AXOVERLAY_TOP_LEFT`        | x, y ignored                   | Fixed top-left corner                    |
| `AXOVERLAY_TOP_RIGHT`       | x, y ignored                   | Fixed top-right corner                   |
| `AXOVERLAY_BOTTOM_LEFT`     | x, y ignored                   | Fixed bottom-left corner                 |
| `AXOVERLAY_BOTTOM_RIGHT`    | x, y ignored                   | Fixed bottom-right corner                |
| `AXOVERLAY_CUSTOM_NORMALIZED`| Normalized x, y between -1 and 1| Custom position in normalized coords    |
| `AXOVERLAY_CUSTOM_SOURCE`   | Absolute pixel x, y relative to source | Custom position relative to video source |

---

## Overlay code setup


### Initialize library

```c
    struct axoverlay_settings settings;
    axoverlay_init_axoverlay_settings(&settings);
    settings.render_callback     = render_overlay_cb;
    settings.adjustment_callback = adjustment_cb;
    settings.select_callback     = NULL;
    settings.backend             = AXOVERLAY_CAIRO_IMAGE_BACKEND;
    axoverlay_init(&settings, &error);

```

### Setup the colors

```c
    struct axoverlay_palette_color color;

    color.red      = r;
    color.green    = g;
    color.blue     = b;
    color.alpha    = a;
    color.pixelate = FALSE;
    axoverlay_set_palette_color(index, &color, &error);

```
### Get max resolution for width and height

```c
    gint camera_width = axoverlay_get_max_resolution_width(1, &error);
    gint camera_height = axoverlay_get_max_resolution_width(1, &error);

```

### Create a large overlay using Palette color space

```c
    struct axoverlay_overlay_data data;
    setup_axoverlay_data(&data);
    data.width      = camera_width;
    data.height     = camera_height;
    data.colorspace = AXOVERLAY_COLORSPACE_4BIT_PALETTE;
    gint overlay_id = axoverlay_create_overlay(&data, NULL, &error);

```
#### Initialize an overlay_data struct with default values
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

### Draw overlay

```c
    axoverlay_redraw(&error);

```

