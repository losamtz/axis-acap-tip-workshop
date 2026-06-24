# Overla API - bbox-multi-view-refactor-lab (moving rectangle)

This sample shows how to draw and animate a bounding box across multiple views on an Axis device using the BBox helper library. It’s a clean, timer-driven example that reuses a single bbox_t handle and draws in normalized coordinates (0..1), so it adapts to any resolution.

## What this sample does

- Creates one persistent bbox_t handle targeting four views (1..4).
- Enables video overlay (OSD) and draws a yellow rectangle that moves horizontally and bounces at the edges.
- Uses GLib timers (no blocking sleep() inside the render loop).
- Clears overlays on shutdown.


## What changed vs bbox-multi-view

- Removed sleep(1) from the tick; frame pacing is now controlled by g_timeout_add(TICK_MS, ...).
- Made bbox_t* persistent (g_bbox) so we don’t recreate/destroy per frame.
- Added clear_all() to wipe overlays before shutdown.
- Kept the same animation logic, just moved into a non-blocking, timer-driven loop.

## API usage

- **Persistent handle**: create `bbox_t` **once** at init, reuse every frame, destroy on shutdown.
- Frame cadence: use `g_timeout_add(TICK_MS, ...)` to schedule redraws; **do not block** the main loop.
- **Normalized coordinates**: pass [0..1] values to `bbox_rectangle()`, independent of resolution.
- `Atomic commit`: call `bbox_commit()` to present all geometry in one go across the selected views.
- **Clean exit**: wipe overlays with `bbox_clear()` + `bbox_commit()` before destroy.

## Lab

1. Change `#define TICK_MS 33` to 100, to keep ~10 FPS.
2. build acap
3. Open a stream showing the views you targeted (e.g., 2×2 multiview).
4. You should see a yellow box moving left/right on each selected view.

## Extra lab

1. Change the speed

    - Adjust xpos += dir * 0.02; to make the motion faster or slower.
    - Or increase FPS by changing #define TICK_MS 33 to 16 (~60 FPS).

2. Color & style variations

    - Experiment with bbox_color_from_rgb(), bbox_thickness_*(), bbox_style_*().

3. Per-view variations

    - Create two bbox_t handles: one for views (1,2), one for (3,4).
    - Move boxes in opposite directions or use different colors.

4. Shapes

    - Add bbox_circle() or draw multiple rectangles per frame.


## Build

```bash
docker build --tag bbox-multi-view-lab --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-multi-view-lab):/opt/app ./build
```
