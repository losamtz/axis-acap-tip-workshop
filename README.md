# AXIS ACAP TIP Workshop

Hands-on samples and labs for several ACAP SDK libraries and platform features. Each folder contains progressive samples to describe the libraries functionalities, buildable focused on one topic (parameters, events, VDO frames, overlays, bbox utils, FastCGI web UI, Civetweb web UI, VAPIX, etc.). 
GitHub

## Repo map

- bbox/ — Bounding-box helpers and demos
    Learn about drawing, and common operations.

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

## What you’ll learn 

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
Capture frames, read them (consumer) or use them as input for larod api and push them back to the pipeline adding bounding boxes (producer).


## Where to look first

- parameter/ → see the three samples (manifest, runtime, custom UI).
- webserver-fastcgi/ → Bootstrap modal UI + FastCGI or civetweb handlers hooked to parameters.
- vapix/ → tiny scripts/snippets for VAPIX api.
- event/ → drop-down source declarations & timed sends; matching subscriber.
- vdo/ → minimal capture loop; NV12 example; timestamps/metadata.
- overlay/ → drawing and dynamic updates.
- bbox/ → coordinate conversions & helpers.

