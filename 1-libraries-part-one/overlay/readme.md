# Overlay

In this chapter we will make use of axoverlay to draw boxes, text and finally set an image/logo in the stream. 

We will start with boxes creating several apps: 

Then we will draw text in the following apps:

We will go through some of the setting relevant to the cameras, mostly "normalized", that will scale in different resolutions.

Note:
It is preferable to use Palette color space for large overlays like plain boxes, to lower the memory usage. More detailed overlays like text overlays, should instead use ARGB32 color space.





## Cairo settings

# Cairo Compositing Operators Overview

This table summarizes some of the most commonly used compositing operators in the [Cairo graphics library](https://cairographics.org/operators/). These operators determine how new drawings (source) interact with existing content (destination).

## Common Operators

| Operator                        | Visual Effect    | Description                                                                                                           |
| -------------------------------|------------------|-----------------------------------------------------------------------------------------------------------------------|
| `CAIRO_OPERATOR_CLEAR`          | 🧽 Erase          | Clears everything to full transparency (like erasing pixels).                                                         |
| `CAIRO_OPERATOR_SOURCE`         | 🎯 Replace        | Completely replaces the destination with the source, ignoring what's already there.                                   |
| `CAIRO_OPERATOR_OVER` (default) | 📄 Stack          | Draws the source **over** the destination using alpha blending. This is like putting a semi-transparent image on top. |
| `CAIRO_OPERATOR_IN`             | 📥 Mask           | Draws the source only where there’s destination content.                                                              |
| `CAIRO_OPERATOR_OUT`            | 📤 Negative Mask  | Draws the source only **where there's no** destination content.                                                       |
| `CAIRO_OPERATOR_ATOP`           | 🔁 Clip-Overlay   | Keeps the destination, draws source **only where** destination exists.                                                |
| `CAIRO_OPERATOR_ADD`            | ➕ Brighten        | Adds color values — can brighten overlapping pixels.                                                                  |
| `CAIRO_OPERATOR_MULTIPLY`       | 🌘 Darken         | Multiplies source and destination colors — makes result darker.                                                       |
| `CAIRO_OPERATOR_SCREEN`         | 🌕 Lighten        | Inverse multiply — result is lighter.                                                                                 |

## Notes

- The default operator used by Cairo is `CAIRO_OPERATOR_OVER`.
- For overlays (like in ACAP or UI rendering), `CAIRO_OPERATOR_SOURCE` is often preferred to fully replace pixels.
- Some operators like `ADD`, `MULTIPLY`, and `SCREEN` are useful for blending or special effects.

For a complete list and detailed explanations, visit the [official Cairo operators documentation](https://cairographics.org/operators/).




# Cairo Operator Visual Summary

A quick visual reference for some common Cairo compositing operators and their effects:

| Operator   | Visual                      |
|------------|-----------------------------|
| `OVER`     | ✅ See-through blend         |
| `SOURCE`   | ⛔ No blend; full overwrite  |
| `CLEAR`    | ❌ Transparent (wipes area)  |
| `ADD`      | ✨ Makes it brighter         |
| `MULTIPLY` | 🌑 Darker if colors overlap |

---

### Notes

- `OVER` is the default operator and does alpha blending.
- `SOURCE` replaces pixels fully, ignoring what's beneath.
- `CLEAR` erases pixels to transparency.
- `ADD` brightens overlapping pixels.
- `MULTIPLY` darkens overlapping pixels.

For more details, see [Cairo Operators Documentation](https://cairographics.org/operators/).

# Overlay Position Types

## The different position types:

### Corners (fixed positions):

- `AXOVERLAY_TOP_LEFT`
- `AXOVERLAY_TOP_RIGHT`
- `AXOVERLAY_BOTTOM_LEFT`
- `AXOVERLAY_BOTTOM_RIGHT`

For these four positions, any x and y coordinates you provide are ignored because the position is fixed at the corresponding corner of the video frame.

---

### Custom Normalized Position:

- `AXOVERLAY_CUSTOM_NORMALIZED`

When using this, the overlay position is defined by x and y values normalized between -1 and 1.

- `-1` means the far left (for x) or bottom (for y)
- `1` means the far right (for x) or top (for y)

This allows placing the overlay anywhere in the frame using a normalized coordinate system.

---

### Custom Source Position:

- `AXOVERLAY_CUSTOM_SOURCE`

Here, the overlay position is specified relative to the video source itself, not the whole video frame.

This means the coordinates are absolute pixel values ranging between 0 and the maximum width/height of the source.

This is especially useful if the video is rotated or if digital pan-tilt-zoom (DPTZ) is used, because then the overlay stays locked to the scene rather than moving with the video frame.

---

## Summary

| Position Type               | Coordinates Usage               | Positioning Behavior                     |
|----------------------------|--------------------------------|-----------------------------------------|
| `AXOVERLAY_TOP_LEFT`        | x, y ignored                   | Fixed top-left corner                    |
| `AXOVERLAY_TOP_RIGHT`       | x, y ignored                   | Fixed top-right corner                   |
| `AXOVERLAY_BOTTOM_LEFT`     | x, y ignored                   | Fixed bottom-left corner                 |
| `AXOVERLAY_BOTTOM_RIGHT`    | x, y ignored                   | Fixed bottom-right corner                |
| `AXOVERLAY_CUSTOM_NORMALIZED`| Normalized x, y between -1 and 1| Custom position in normalized coords    |
| `AXOVERLAY_CUSTOM_SOURCE`   | Absolute pixel x, y relative to source | Custom position relative to video source |

---


# Normalized Coordinates in Axis ACAP Overlay

## What are Normalized Coordinates?

Instead of specifying overlay positions using absolute pixel values (e.g., 100 pixels from the left), normalized coordinates scale positions to a fixed range between **-1** and **1**, regardless of the video frame's resolution or size.

This means your overlay positioning becomes resolution-independent and consistent across different video sizes.

## Coordinate System Overview

- The coordinate system is centered in the middle of the video frame.
- The **x-axis** runs horizontally from **-1** (far left edge) to **1** (far right edge).
- The **y-axis** runs vertically from **-1** (bottom edge) to **1** (top edge).

## Visual Representation

---
       y = 1 (top)
         |
 (-1,1)  |  (1,1)
         |  
---------+--------- x = 1 (right)
         |  
 (-1,-1) |  (1,-1)
         |
       y = -1 (bottom)

---

- **x = -1** → far left edge of the frame  
- **x = 0** → horizontal center  
- **x = 1** → far right edge  
- **y = -1** → bottom edge  
- **y = 0** → vertical center  
- **y = 1** → top edge  

## Why Use Normalized Coordinates?

- **Resolution independence:** The same coordinates work on any video size or resolution.  
- **Consistent positioning:** (0, 0) is always the center.  
- **Easy corner placement:**  
  - (-1, 1) → top-left corner  
  - (1, -1) → bottom-right corner  
- **Smooth positioning:** Use decimal values to position anywhere in between, e.g., (0.5, -0.3).

## Summary Table

| Coordinate Value | Meaning            | Position on Frame              |
|------------------|--------------------|-------------------------------|
| -1               | Minimum normalized  | Left (for x), Bottom (for y)  |
| 0                | Centered           | Center                        |
| 1                | Maximum normalized  | Right (for x), Top (for y)     |
