# Bounding Box (bbox) Samples & Labs

This folder contains examples of how to use ACAP’s **Bounding Box (bbox)** utilities with multiple views on Axis devices.  
Each sample demonstrates how bounding boxes can be rendered on different camera views using the **Overlay API** together with `bbox`.

---

## Sample Contents

### 1. **bbox-multi-view/**
- **Objective:** Demonstrates how to manage bounding boxes across **all camera views**.
- **Behavior:** A box moves along the X-axis and is displayed across every available view on the device.
- **What you learn:**
  - How to iterate through all views in a multi-view setup.
  - How to create and update bounding boxes consistently across multiple streams.
  - How to animate a moving box using the Overlay API (bbox).

---

### 2. **bbox-view/**
- **Objective:** Shows how to draw a single large bounding box that frames an **entire view**.
- **Behavior:** A bounding box is created that covers the full extent of the selected view.
- **What you learn:**
  - How to query the view dimensions.
  - How to render a bounding box that encapsulates a full view.
  - How to work with view-specific bbox normalization.

---

### 3. **bbox-multiview-refactor-lab/**
- **Objective:** Refactored version of the moving box sample using **best practices** for smoother animation.
- **Behavior:** A yellow rectangle moves horizontally across multiple views, bouncing off edges.
- **What’s new / improved:**
  - Uses **GLib timers** (`g_timeout_add`) instead of blocking `sleep()` calls, ensuring smooth 30 FPS animation.
  - Creates a **persistent bbox handle** once and reuses it each frame instead of recreating/destroying every tick.
  - Proper cleanup on exit: clears overlays before destroying resources.
- **What you learn:**
  - How to structure bbox-based applications efficiently.
  - How to decouple animation timing from rendering logic.
  - How to manage resources (create once, reuse, destroy on shutdown) for performance.

---

## Learning Goals

By working through these labs, you will:
- Understand how to attach bounding boxes to **specific views** in multi-view cameras.
- Learn the difference between per-view rendering and synchronizing across multiple views.
- Practice updating bbox coordinates dynamically (moving objects) vs. static overlays (framing views).
- Apply **refactoring patterns** for smoother overlays and better resource management.

---

## Lab Checklist

| Sample                        | What to Verify |
|-------------------------------|----------------|
| bbox-multi-view               | A moving box travels horizontally across all views simultaneously. |
| bbox-view                     | A static box is drawn that frames the entire selected view. |
| bbox-multiview-refactor-lab   | A smoothly animated yellow box bounces horizontally, with no stutter and efficient resource use. |

---

## Lib flow summary

[bbox-api](./bbox-api.md "Go to bbox flow")
