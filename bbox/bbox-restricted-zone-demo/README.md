# BBox restricted-zone demo

A security-workshop simulation: a generated object rectangle moves across a
restricted area. The app draws real BBox overlays on camera view 1, changes the
object's color on entry, and signals when its dwell time exceeds a limit.
**No person detection, video analysis, or security-event publication is performed.**
The box moves independently of the camera scene.

## Try it

1. Build/install the EAP, sign it if the camera requires signing, and start it.
2. Open Settings or `/local/bbox_restricted_zone/index.html` on the camera.
3. The simulation diagram shows a yellow restricted area and a moving object.
4. Select **Start preview** to see the real BBox output on camera video.
5. Watch the object turn green (outside), amber (inside), then red (dwell exceeded).
6. Change the entry rule from **Object center inside** to **Any positive overlap**.
   Entry occurs earlier and exit later with overlap, for the default rectangle.
7. Disable the demo: the drawing queue is cleared and committed.

The object travels from left to right in 12 seconds, then back in 12 seconds.
Its width is 12% and height is 20% of the view, with its top at 40%.
A zone placed away from this path will never be entered. Large dwell limits may
not be reached during a pass. The default zone and three-second limit demonstrate
all three colors. Saving settings restarts motion and resets dwell and entries.

## How it differs from the VDO inspection example

| | This BBox demo | VDO inspection-zone monitor |
| --- | --- | --- |
| Input | Simulated object coordinates generated from monotonic elapsed time | Real NV12 camera frames |
| Main API task | Draw rectangles into camera video | Acquire frames for pixel analysis |
| Zone decision | Object center or rectangle overlap | Percentage of pixels changed from an empty reference |
| Calibration | None | Ten empty-zone frames |
| Dashboard preview | Simulation diagram plus optional camera video | Sampled grayscale image used by the algorithm |
| What moves the state | Generated rectangle entering/leaving the zone | Brightness changes within the region |
| Video output | Burnt-in boxes on view 1 | Region rectangle on the webpage only |

VDO is an input API; BBox is a drawing API. Neither alone is a person detector.
A later exercise could use a detector's coordinates instead of the simulated
rectangle, or draw a VDO-calculated inspection state. That integration must check
coordinate transforms and whether overlays affect frames used for analysis.

## Build

Run from this directory:

```bash
docker build --tag bbox-restricted-zone-demo --build-arg ARCH=aarch64 .
mkdir -p build
container_id=$(docker create bbox-restricted-zone-demo)
docker cp "$container_id":/opt/app/BBox_Restricted_Zone_Demo_1_0_0_aarch64.eap ./build/
docker rm "$container_id"
```

For another supported architecture, change ARCH and the package filename.
The current SDK 12.11.0 build sets a minimum AXIS OS of 12.11.72. Inspect
the packaged manifest if changing SDK versions. Requires BBox support on the camera and view 1.
Permissions follow the workshop BBox and Parameter examples: video/admin groups,
Graphics2 and Overlay2 D-Bus methods, and an admin-only FastCGI status endpoint.

## Parameters

| Name | Default | Allowed values |
| --- | --- | --- |
| Enabled | yes | yes / no |
| Zone | 35,25,30,50 | Left, top, width, height as integer percentages; positive size within 0–100%. |
| Rule | center | center / overlap |
| DwellSeconds | 3 | 1–60 seconds |

Parameters persist and use `root.Bbox_restricted_zone`. The rectangle is a single
parameter to avoid partial coordinate updates. Saves across different parameters
are not transactional. The dashboard preserves unsaved edits and waits for
matching running values before confirming a save. Invalid saved values prevent
startup; rejected live values may still need correction in parameter storage.

Center-on-boundary counts as inside. Overlap requires positive area; rectangles
that only touch do not overlap. Dwell uses monotonic time and resets on exit,
disable, setting changes and restart. Entry count is per simulation run.

## Drawing and lifecycle

`main.c` creates one persistent handle for view 1 and explicitly selects frame
normalized coordinates. A 100 ms GLib timer calculates motion, clears the queue,
draws the zone and object, and commits them together. Motion follows elapsed time,
not callback counts. Colors are created once; the object uses corner-style lines
to distinguish it from the solid zone outline.

BBox errors appear separately from simulation state. The app retries drawing
after five seconds. A failed commit can leave previous boxes visible; a successful
commit is not proof that a view/stream exists or that every client displays it.
Graceful shutdown attempts to clear the overlay before destroying the handle.
Avoid running other BBox workshop apps on the same view during this exercise.

The optional preview uses `/axis-cgi/media.cgi` with MP4/H.264, camera 1, 10 fps,
and no audio, through the browser's camera login. Device and browser support are
required. Preview buffering can make it lag behind the simulation diagram.
Stopping the preview releases its HTTP stream; it does not stop the demo.

## Tests and teaching exercises

```bash
node --test tests/*.test.cjs
docker build -f tests/Dockerfile --tag bbox-restricted-zone-demo-tests .
```

C tests cover parameter validation, geometric boundaries, entry/exit, re-entry,
dwell, disable and the repeating path. Dashboard tests cover settings submission,
confirmation, invalid zones and disconnection. Test the actual BBox overlay and
media preview on a supported camera after installing.

Exercises: compare entry rules; move the zone outside the path; change dwell;
change object size; replace simulation coordinates with a detector interface;
add Event API publication as a separate exercise.

- `app/zone.c`: configuration, geometry, generated motion and dwell state.
- `app/main.c`: persistent BBox context, parameters, timer and status snapshots.
- `app/status_server.c`: read-only FastCGI snapshot endpoint.
- `app/html/`: AXIS-style dashboard and optional camera preview.

Reference: [Axis BBox API](https://developer.axis.com/acap/api/src/api/bbox/html/bbox_8h.html).
