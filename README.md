# AXIS ACAP TIP Workshop

Hands-on samples and labs for several ACAP SDK libraries and platform features. Each folder is a bite-size, buildable example focused on one topic (parameters, events, VDO frames, overlays, bbox utils, FastCGI web UI, VAPIX, debugging, etc.). 
GitHub

## Repo map

- bbox/ — Bounding-box helpers and demos
    Learn about frame-normalized vs scene-normalized coordinates, conversions, drawing, and common operations (e.g., IoU).

- event/ — AXEvent publisher/subscriber patterns
    Stateless vs property events, marking source vs data fields, and dropdown (source) declarations + timed sends.

- overlay/ — axoverlay examples
    Create layers, draw primitives/text, manage z-order and alpha, and render dynamic overlays over live video.

- parameter/ — Three ways to work with axparameter

    1. Manifest parameters (declared in package.conf),
    2. Runtime create/update/list/remove via API,
    3. Custom UI that reads/writes parameters from a web page.

- vapix/ — VAPIX HTTP API examples
Calling param.cgi / events / I/O from scripts or tiny clients (GET/POST patterns, auth, and safe parsing).

- vdo/ — libvdo capture & buffer handling
Open channels, pull frames, manage formats, timestamps, and (optionally) hand off to inference pipelines.

- webserver-fastcgi/ — FastCGI web UI samples
Serve a Bootstrap UI, read/write ACAP parameters via endpoints, and wire simple “settings” forms to the device.


Tip: some folders contain multiple labs that build on each other. Open each folder’s own README for exact build/run steps.

## What you’ll learn (high level)

- **Parameters**: declare (manifest), add at runtime, list/remove, and subscribe to callbacks; wire a custom HTML UI (FastCGI) that round-trips to param.cgi/axparameter.
- **Events**: differences between stateless & property events; declaring source (dropdown) vs data (free text), and subscribing with filters.
- **VDO**: grabbing frames efficiently, NV12/RGBA formats, timestamps/metadata, and safe plane access.
- **Overlay**: drawing layers/text/shapes with axoverlay, controlling z-order/opacity, and dynamic update loops.
- **BBox**: converting frame-normalized ↔ scene-normalized, drawing, and simple metrics.
- **Web UI**: FastCGI endpoint patterns + Bootstrap UIs; parameter viewer/editor.
- **VAPIX**: small HTTP calls to system/config APIs (auth, query, set) for glue logic.



## Building & running (quick start)

Exact steps vary by sample. Typical flow:

1) On your dev host / Codespace
git clone https://github.com/losamtz/axis-acap-tip-workshop
cd axis-acap-tip-workshop/

2) Build (most samples provide a Makefile or simple build script)

Build the application: 

```bash
docker build --build-arg ARCH=aarch64 --tag <APP_NAME> .

```
Copy the result from the container image to a local directory build:

```bash
docker cp $(docker create <APP_NAME>):/opt/app ./build

```

3) Go to camera app section and upload *.eap file under sample build forder


Then read the sample’s README for:

Run command (how it is launched on the device).

Logs location (usually via syslog).


## Suggested learning path

- parameter/
Start with manifest vs runtime parameters and callbacks. Then open webserver-fastcgi/ to see a UI that reads/writes parameters.

- vapix/
Glue everything with simple HTTP calls to device APIs (e.g., read/write params, trigger actions).

- event/
Declare & send stateless events; try a dropdown source series (0..100) and subscribe from a small consumer.

- webserver/ 
fastcgi - reverse proxy

- overlay/
Render bounding boxes/text on live video.

- bbox/
convert/draw boxes.

- vdo/
Capture frames, examine NV12 planes safely



## Notes & gotchas

- **GMainLoop**: Event, parameter callbacks, and some libs expect a running GLib main loop. Keep callbacks short.

- **Source vs Data** (events):
Source values are fixed at declare-time (become dropdowns in the UI); Data values are “free text”/variable at send-time.

- **NV12 plane safety**: Always compute strides/offsets correctly; never assume contiguous chroma with the same stride as luma.

- **VAPIX**: Handle auth and URL encoding; parse line-based param.cgi responses defensively.

- **FastCGI**: Keep handlers minimal; validate inputs before writing to parameters.

## Where to look first

- parameter/ → see the three samples (manifest, runtime, custom UI).
- webserver-fastcgi/ → Bootstrap modal UI + FastCGI handlers hooked to parameters.
- vapix/ → tiny scripts/snippets for param.cgi and friends.
- event/ → drop-down source declarations & timed sends; matching subscriber.
- vdo/ → minimal capture loop; NV12 example; timestamps/metadata.
- overlay/ → drawing and dynamic updates.
- bbox/ → coordinate conversions & helpers.

