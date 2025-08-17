# Overla API - bbox-multi-view-refactor-lab (moving rectangle)

Here’s a cleaned-up version of your sample that:

- removes the sleep(1) (let the GLib timer set the frame rate), and
- creates the bbox handle once at init, then reuses it on every tick (clear → draw → commit), and
- destroys the handle on shutdown.

## What changed vs bbox-multi-view

- Removed sleep(1) from the tick; frame pacing is now controlled by g_timeout_add(TICK_MS, ...).
- Made bbox_t* persistent (g_bbox) so we don’t recreate/destroy per frame.
- Added clear_all() to wipe overlays before shutdown.
- Kept the same animation logic, just moved into a non-blocking, timer-driven loop.

## Lab

Change `#define TICK_MS 33` to 100, to keep ~10 FPS.


## Build

```bash
docker build --tag bbox-multi-view-lab --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create bbox-multi-view-lab):/opt/app ./build
```
