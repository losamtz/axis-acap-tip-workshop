## Cairo settings

### Cairo Compositing Operators Overview

This table summarizes some of the most commonly used compositing operators in the [Cairo graphics library](https://cairographics.org/operators/). These operators determine how new drawings (source) interact with existing content (destination).

### Common Operators

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

#### Notes

- The default operator used by Cairo is `CAIRO_OPERATOR_OVER`.
- For overlays (like in ACAP or UI rendering), `CAIRO_OPERATOR_SOURCE` is often preferred to fully replace pixels.
- Some operators like `ADD`, `MULTIPLY`, and `SCREEN` are useful for blending or special effects.

For a complete list and detailed explanations, visit the [official Cairo operators documentation](https://cairographics.org/operators/).




### Cairo Operator Visual Summary

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



### Grayscale Mapping Using `index2cairo` for Cairo Overlays

This function converts a grayscale color index (typically in the range 0–15) into a normalized floating-point value between 0.0 and 1.0, which can be used with cairo_set_source_rgba().

#### Function Overview

```c
static gdouble index2cairo(const gint color_index) {
    return ((color_index << 4) + color_index) / PALETTE_VALUE_RANGE;
}

```

### Step 1: Bit Manipulation

```c
((color_index << 4) + color_index)
```

* `color_index << 4` shifts the value 4 bits to the left (i.e., multiplies it by 16).
* Adding `color_index` again gives:
  → `color_index * 16 + color_index = color_index * 17`

This is a standard way to scale a **4-bit value (0–15)** to **8-bit grayscale (0–255)**:

| color\_index (4-bit) | 8-bit Value (× 17) | Description    |
| -------------------- | ------------------ | -------------- |
| 0                    | 0                  | Black          |
| 1                    | 17                 | Very dark gray |
| ...                  | ...                | ...            |
| 15                   | 255                | White          |

---

### Step 2: Normalize to \[0.0, 1.0]

```c
/ PALETTE_VALUE_RANGE
```

Defined:

```c
#define PALETTE_VALUE_RANGE 255.0
```

This scales the 8-bit grayscale value to a Cairo-friendly floating-point value between `0.0` and `1.0`.

---

### How It's Used in Drawing

```c
val = index2cairo(color_index);
cairo_set_source_rgba(context, val, val, val, val);
```

This sets the drawing color in Cairo to a **semi-transparent shade of gray**:

* `R = G = B = val`
* `A (opacity) = val`

So:

* `color_index = 0` → fully transparent black
* `color_index = 15` → fully opaque white
* Intermediate values → semi-transparent grays

---

### Visual Mapping Table

| color\_index | 8-bit Gray Value | Normalized val | Visual Description |
| ------------ | ---------------- | -------------- | ------------------ |
| 0            | 0                | 0.000          | Transparent Black  |
| 1            | 17               | 0.067          | Very Dark Gray     |
| 2            | 34               | 0.133          | Dark Gray          |
| 4            | 68               | 0.267          | Gray               |
| 8            | 136              | 0.533          | Medium Gray        |
| 12           | 204              | 0.800          | Light Gray         |
| 15           | 255              | 1.000          | White              |

> (Intermediate values omitted for brevity)

---

### ✅ Summary

The `index2cairo()` function:

* Converts a **4-bit grayscale value** to 8-bit via `index × 17`
* Normalizes it to a float between `0.0` and `1.0`
* Applies it as an **RGBA color in Cairo** for rendering

This allows clean, consistent grayscale overlays on video streams in Axis ACAP using Cairo.


### Coordinate vs Color Normalization in Axis ACAP Overlays

When working with Axis ACAP overlays using Cairo, you’ll encounter two different kinds of normalized values:

1. **Overlay coordinates** (for positioning on screen)
2. **Cairo RGBA values** (for setting color and opacity)

These use **different ranges** and serve **different purposes** — here’s why:

---

### Overlay Coordinate Normalization (`AXOVERLAY_CUSTOM_NORMALIZED`)

- **Range:** `[-1, 1]` for both X and Y
- **Purpose:** To position overlays relative to the **video frame**, independent of resolution
- **Centered system:** `0` represents the center of the frame

#### ✅ Examples:

| Coordinate | Meaning              |
|------------|----------------------|
| (0, 0)     | Center of the frame  |
| (-1, 1)    | Top-left corner      |
| (1, -1)    | Bottom-right corner  |

---

### Cairo RGBA Normalization

- **Function:** `cairo_set_source_rgba(context, R, G, B, A)`
- **Range:** `[0.0, 1.0]` for each channel (Red, Green, Blue, Alpha)
- **Purpose:** Defines **color** and **opacity**
- **Standardized:** Common in graphics libraries like OpenGL, SVG, CSS

#### ✅ Example:
```c
cairo_set_source_rgba(ctx, 1.0, 0.0, 0.0, 0.5); // Semi-transparent red
```

### 🔍 Summary: Why the Difference?

| Feature                    | Purpose             | Normalization Range | Center-Based? | Usage Context         |
|----------------------------|---------------------|----------------------|----------------|------------------------|
| `AXOVERLAY_CUSTOM_NORMALIZED` | Positioning overlays | `[-1, 1]`              | ✅ Yes         | Spatial (video frame) |
| `cairo_set_source_rgba`       | Drawing colors      | `[0.0, 1.0]`           | ❌ No          | Color (graphics API)  |

---

#### ✅ Final Notes

- **Overlay coordinates** define **where** something is drawn.
- **Cairo RGBA values** define **how it looks** (color + transparency).
- The two are used together but normalized **differently** for logical and technical reasons.
