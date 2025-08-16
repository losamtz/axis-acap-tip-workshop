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

## Learning Goals

By working through these labs, you will:
- Understand how to attach bounding boxes to **specific views** in multi-view cameras.
- Learn the difference between per-view rendering and synchronizing across multiple views.
- Practice updating bbox coordinates dynamically (moving objects) vs. static overlays (framing views).

---

## Lab Checklist

Sample	                            What to Verify
bbox-multi-view	                    A moving box travels horizontally across all views simultaneously.
bbox-view	                        A static box is drawn that frames the entire selected view.

## Lib flow summary

[bbox-api](./bbox-api.md "Go to bbox flow")