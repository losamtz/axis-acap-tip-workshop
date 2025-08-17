# Overlay API

This folder contains a small suite of Axis ACAP overlay examples that show different ways to draw graphics and text on the video stream, and how to drive overlays dynamically via VAPIX. Each sub-folder is self-contained and can be studied or used as a lab.

| Sample / Lab              | What it demonstrates                                       | Typical use case                               |
| ------------------------- | ---------------------------------------------------------- | ---------------------------------------------- |
| **add-logo**              | Loading an image (logo) and placing it as an overlay       | Branding, watermarking                         |
| **draw-rectangle**        | Drawing a rectangle overlay with color/line params         | simple boxes                   |
| **draw-text**             | Rendering text strings on the stream                       | Status readouts, counters, labels              |
| **drawviews**             | Creating overlays for **multiple views/streams**           | Multi-sensor/multiview products                |
| **dynamic-overlay-vapix** | Creating/updating overlays via **VAPIX** (HTTP) at runtime | Remote control dashboards, scripts, automation or through ACAP|


## How overlays work (quick mental model)

- ACAP app starts → initializes axoverlay and one or more overlay groups
- You create overlay items (image, text, shapes) and attach them to a stream/view
- You can update or remove overlays at runtime
- With VAPIX, you can create/update overlays using HTTP (handy for dashboards and external tools)

# Samples

1) **`add-logo`**

    **Goal**: Load a static image (e.g., PNG) and place it as an overlay.

    What to look for:

    - How the image buffer is loaded and handed to the overlay
    - Positioning/pinning (top-left, bottom-right, fixed offsets)
    - Handling of alpha channel (transparency)

2) **`draw-rectangle`**

    **Goal**: Draw a rectangle overlay with custom color and line width.

    What to look for:

    - Rectangle creation API
    - Coordinates and size (pixels vs normalized, depending on the snippet)
    - Updating the rectangle (e.g., move/resize)

3) **`draw-text`**

    **Goal**: Render a text overlay (e.g., “Hello ACAP” or a counter).

    What to look for:

    - Text style (font size, color), baseline positioning
    - Updating text dynamically (e.g., time tick, FPS, custom label)
    - Clarity/antialiasing considerations

4) **`draw-views`**

    **Goal**: Attach overlays to multiple views (multi-sensor / multi-stream devices).

    What to look for:

    - Enumerating views/streams
    - Creating one overlay per view (e.g., a moving box across X axis on each view)
    - Coordinating updates across views

5) **`dynamic-overlay-vapix`**

    Goal: Create/update overlays via VAPIX (HTTP endpoint - dynamicoverlay.cgi). Overlay settings can also be modified via UI (position, color, size font). This sample is also under **vapix** folder as part of **Vapix API**.

    What to look for:

    - The VAPIX endpoints and payloads used to create, update, get, delete
    - Parameter names (position, text, color) and required encodings

