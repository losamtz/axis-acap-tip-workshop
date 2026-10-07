# Larod restricted-zone monitor

A workshop application that detects **people in real camera frames** and reports
sustained presence inside a rectangular industrial exclusion zone. It uses the
[official Axis object-detection example](https://github.com/AxisCommunications/acap-native-sdk-examples/tree/3025a4e9db74f1af5bb878d4efc82ac0e300a792/object-detection)
as its inference foundation. See [UPSTREAM.md](UPSTREAM.md) for source provenance.

The web page uses yellow primary buttons, outlined secondary buttons, gray panels,
and a toggle matching the workshop's AXIS-style applications. It includes optional
live video with labeled person boxes, actual model input, status, and persistent settings.

## What students learn

```text
VDO frame → optional cpu-proc preprocessing → larod SSD inference
          → person detections → zone policy → Overlay2 + AXEvent + web status
```

The input/output tensor setup is intentionally recognizable from upstream:

1. `model_provider_new()` loads the model and calls `setup_tensors()` to obtain
   both model input descriptors and output tensors.
2. The temporary input describes dimensions and pitch; it is then destroyed.
   Output buffers are allocated and mapped once.
3. After VDO reports its actual format, resolution, pitch, and buffer count,
   `model_provider_update_image_metadata()` creates camera-input tensors and
   sets up preprocessing if needed.
4. The first use of each VDO buffer binds and tracks its fd. Later frames reuse
   those tensors. The preprocessing and inference requests are created lazily
   and reused, with their camera input selected for each frame.
5. The application returns the VDO buffer before publishing status or drawing.

“Set up together” does not mean that a model input descriptor already owns a
camera image. That distinction is a useful part of this exercise.

## Difference from the other examples

| Example | Input and decision |
| --- | --- |
| VDO inspection-zone monitor | Samples luminance and compares it with an empty reference; detects visual change without knowing what caused it. |
| BBox restricted-zone demo | Moves simulated boxes to teach coordinates, zone rules, and rendering. |
| This application | Runs a pretrained neural network on actual frames and tests detected people against a zone. No empty-scene calibration. |

The bundled Coral SSD MobileNet v2 COCO model uses four float outputs: locations,
classes, scores, and detection count. This decoder assumes **class 0 is person**.
It checks output types/capacities and rejects invalid counts before reading boxes.
It is not a generic decoder for arbitrary TFLite models. Forklifts, helmets,
pallets, and defects need an appropriate model and potentially another decoder.

## Build and copy only the EAP

Run from this directory. Select the chip in the camera's specifications; `aarch64`
alone does not distinguish ARTPEC-8 from ARTPEC-9.

```sh
docker build --tag larod-restricted-zone-monitor \
  --build-arg ARCH=aarch64 --build-arg CHIP=artpec8 .
mkdir -p build
container_id=$(docker create --platform linux/amd64 larod-restricted-zone-monitor)
docker cp "$container_id":/opt/app/Larod_Restricted_Zone_artpec8_1_0_0_aarch64.eap ./build/
docker rm "$container_id"
```

For ARTPEC-9 use `CHIP=artpec9` and copy
`Larod_Restricted_Zone_artpec9_1_0_0_aarch64.eap` instead. The Docker container is an
amd64 **build environment**; `ARCH` selects the camera binary architecture.

The default SDK is **12.11.0**, which stamps **AXIS OS 12.11.72** as the minimum
package version. Do not lower that manifest requirement manually. Use a compatible
camera OS. Alternative manifests follow upstream for `CHIP=cpu` and `CHIP=edgetpu`;
choose a supported SDK/architecture/backend combination for the target camera.
CPU inference may be too slow for the three-second freshness requirement.

Install the unsigned package using your workshop's normal signing/installation
procedure, start the application, and open its settings page. Model loading can
take several minutes. Startup and backend errors appear in the application log.

## Try the industrial scenario

1. Aim the camera at a pedestrian exclusion area beside a loading dock or conveyor.
   Create a cropped view area in the AXIS interface if needed. Select it under
   **Inference view area** and save. The stream switches without restarting the app.
2. Start preview. Set the zone's left, top, width, and height as percentages of
   the displayed image. Click **Save**; the page confirms running values.
3. Walk outside the yellow rectangle: person boxes should be green.
4. Put the bottom-center of your person box inside the rectangle: the box turns
   amber and the activation timer starts.
5. Remain detected for the activation delay: the box turns red and the alarm
   becomes active.
6. Leave the zone: the alarm clears after the clear delay. A short missed
   detection during an alarm does not immediately clear it.
7. Disable monitoring: boxes clear and the event becomes unavailable/inactive.

The bottom-center is an approximate foot position in image coordinates, not a
measured floor location. Keep the selected view's crop/rotation consistent with the preview;
switch away and back after changing imaging geometry. The initial VDO rate is 5 fps, and the
actual processed rate depends on the device and model.

| Setting | Default | Meaning |
| --- | --- | --- |
| ViewArea | 1 | Existing view area to analyze; saving switches the stream live |
| Enabled | yes | Enable policy and overlays |
| Zone | 30,20,40,60 | Left, top, width, height in percent |
| Confidence | 60 | Minimum person confidence in percent |
| ActivationSeconds | 3 | Uninterrupted detected presence before alarm |
| ClearSeconds | 2 | Uninterrupted absence needed to clear an active alarm |

A settings change resets timing and waits for fresh evidence. Before activation,
a missed detection restarts entry timing. After activation, the clear delay
bridges short gaps. This measures **zone occupancy**, not an individual's dwell
or a unique-person count: two people can hand over occupancy without an empty gap.

## Events and status

The stateful event topic is:

```text
CameraApplicationPlatform / LarodRestrictedZone / PersonPresence
```

`active` is the alarm state. `available` indicates that monitoring is enabled and
there is a recent valid result. Initial, disabled, and detected stale/error states
publish `active=false, available=false`; that combination does **not** assert an
empty zone. Successful updates are sent only when these values change. Failed
sends are retried from the main loop. Use the camera's event/action interface to
inspect the topic and select the alarm condition.

`status.cgi` is an admin-only FastCGI endpoint. Its worker serves immutable
snapshots; it does not access tensors, the zone state, or AXParameter directly.
The page notices an unchanged heartbeat after three seconds and shows Unknown,
even if the HTTP worker can still respond. A frame older than three seconds is
not accepted as evidence. Stream interruptions trigger a retry of the selected view. If the app exits after
a fatal model error, restart it after
resolving the underlying camera issue. Fatal process failures cannot guarantee a
final event update, so downstream integrations also need availability supervision.

Settings use the camera's authenticated `param.cgi` interface and the
`root.Larod_restricted_zone.*` group. A multi-parameter write is not a transaction:
the page checks running values and reports unconfirmed/partial saves honestly.

Live preview uses the active view area's same-origin H.264 MP4 HTTP stream. It starts only when
requested and releases the stream when stopped. The browser/camera must support
this preview format. Status and the actual model-input snapshot remain usable without video.

## Read the code in this order

| File | Responsibility |
| --- | --- |
| [app/main.c](app/main.c) | Initialization, frame loop, buffer release, cleanup |
| [app/views.c](app/views.c) | Discover view areas and resolve their VDO channel IDs |
| [app/model.c](app/model.c) | Official-style tensor setup, mapping, tracking, reusable jobs |
| [app/model_preprocessing.c](app/model_preprocessing.c) | Optional resize/color conversion using cpu-proc |
| [app/detections.c](app/detections.c) | Bounds-checked SSD decoding into named person records |
| [app/zone_policy.c](app/zone_policy.c) | Pure geometry and timing, independent of camera APIs |
| [app/monitor.c](app/monitor.c) | Persistent settings, freshness, status snapshots |
| [app/overlay.c](app/overlay.c) | Zone and person boxes; commits zero-detection clears |
| [app/event.c](app/event.c) | Stateful alarm declaration and transition publication |
| [app/status_server.c](app/status_server.c) | Read-only FastCGI snapshot worker |

`stream.c`, `channel_util.c`, `img_util.c`, and `argparse.c` preserve the upstream
acquisition/support structure. The positional threshold in the upstream command
line remains for compatibility; the running policy uses the **Confidence**
parameter instead. Source is formatted for classroom reading, with no bundled or
minified JavaScript.

## Verification

Run the repeatable tests without a camera:

```sh
docker build -f tests/Dockerfile -t larod-restricted-zone-tests .
```

Or run the JavaScript tests with Node and compile the pure C modules with a host
C compiler:

```sh
node --test tests/*.test.cjs
cc -std=c11 -Wall -Wextra -Werror -I app tests/test_policy.c \
  app/zone_policy.c app/detections.c -lm -o /tmp/larod-zone-tests
/tmp/larod-zone-tests
```

Tests cover foot-point geometry, activation/clear boundaries, interrupted entry,
zero-delay behavior, invalid SSD counts/capacities, class filtering, clearing old
detections, settings readback, HTTP failures, frozen status, and preview lifecycle.
Build verification does not establish detection accuracy, camera overlay alignment,
or event delivery on hardware. Validate those with the walk-through above.

This is an analytics workshop demonstration, not a certified machinery safety
interlock. Occlusion, small people, lighting, and the generic pretrained model can
produce misses or false detections.

## Selecting a zoomed view area

1. Create the desired cropped view in the camera's AXIS interface.
2. Open the application page. Available views refresh every five seconds.
3. Select **Inference view area** and click **Save**.
4. Wait for the selected view to become active; no app restart is required.
5. Start preview and confirm the cropped image. Review the zone coordinates and
   save any changes before using the alarm. Disable monitoring while configuring
   if you do not want alarms during setup.

Discovery uses VDO's `view.count` and resolves each `view` descriptor with
`vdo_channel_get_ex()`. The selected **view number** is used for VAPIX's `camera`
argument and the selected view display. The separately resolved **VDO channel ID** is used
for frame capture, format, resolution, and rotation. These numbers need not match
on every platform. See the [VDO channel API](https://developer.axis.com/acap/api/src/api/vdostream/html/vdo-channel_8h.html).

The status distinguishes `requestedView` (saved selection), `activeView` (current
pipeline), `activeChannel` (resolved VDO ID), and `switchingView`. A change clears
old detections, alarm timing, preview snapshots, and overlays. Between frames,
`open_view()` in `app/main.c` stops the old stream and calls
`model_provider_reset_stream()` in `app/model.c` to release its jobs, input
tensors, buffer descriptors, and preprocessing resources. The loaded inference
model and output tensors stay allocated. The new stream is configured from the
selected channel's geometry, and monitoring resumes on its first valid frame.

An already-playing browser preview follows the new active view automatically.
A stopped preview stays stopped. An unavailable view leaves settings reachable,
with retries every five seconds; selecting another view retries immediately.
There is no silent fallback to another image.

After editing the crop or rotation of an already-active view in the camera UI,
select another view and then return to rebuild the stream with the new geometry
(or restart the app). Always review the zone: its coordinates remain relative
to the selected image and are not transformed between views.

The application uses **Overlay2 and Cairo** to burn the zone, thick person boxes,
and **Person 78%** labels into encoded streams from the selected inference view.
Labels are part of the video pixels, so they remain visible in fullscreen, RTSP
clients, and recordings that include application overlays. The separate detection
diagram and browser drawing layer have been removed; Actual model input remains.

The preview requests `overlays=all`. Other viewers and recording profiles must
also enable application overlays (for example, `overlays=all` or
`overlays=application`). Streams using `overlays=off` will not contain drawings.
Use the same view as inference, without additional client-specific crop, mirror,
or rotation overrides. Only newly recorded video contains the overlays.

`overlay.c` watches VDO overlay stream events, checks the resolved channel and
encoded format, and allocates one overlay per matching stream. Raw model input is
excluded. `overlay_render.c` draws the zone, outlines, and confidence text into
transparent ARGB buffers. Closed streams and view changes release their overlays.
Disabled or unavailable monitoring submits a transparent frame to clear drawings.
Detection results update at the monitor tick; this does not implement
frame-exact synchronization between inference and encoding.

This requires Overlay2-capable firmware/hardware (ARTPEC-7/8/9 with the supported
SDK/OS). CPU or EdgeTPU model selection does not remove that overlay requirement.
See the [Overlay2 introduction](https://developer.axis.com/acap/api/src/api/axoverlay_v2/html/introduction.html)
and [stream overlay options](https://developer.axis.com/vapix/network-video/parameter-management/image-api/).

To verify the encoded result independently of this web page, open a stream such as
`rtsp://CAMERA/axis-media/media.amp?camera=2&overlays=all` in your video client
(use your selected view number). Close the application page while leaving the app
running: boxes and labels should continue. Record a short clip from that stream
and check playback. Camera validation is still required for alignment and stream
availability; host renderer tests do not exercise the camera compositor.

## Troubleshooting no person boxes

Watch the live preview for accepted person boxes and confidence labels.
A yellow rectangle alone is the
configured zone with no accepted person boxes.

`PersonPresence active=0 available=1` means a recent valid inference exists and
there is no active zone alarm. It does not mean a person was detected. The page
now shows the number of model candidates across all classes and the highest
returned person confidence before applying the configured threshold. A candidate
is a model hypothesis, not a verified object. The model only returns its retained
output candidates; this is not an exhaustive score for every possible person.

Every ten seconds the log reports the view, resolved channel, raw detection count,
top class ID and score, person candidate count, highest person score, configured
threshold, accepted person count, and inference time. Unavailability transitions
also log their reason. Class 0 is person in the bundled model. Other classes,
including cars, are intentionally excluded from the person-only overlay and alarm.

If a visible person is below the threshold, temporarily try 30% confidence and a
tighter view to compare results. This can increase false positives; choose the
final threshold using the actual scene. If no person candidate is returned, a
lower threshold cannot recover it. Check the selected crop and try a clearly
visible larger person. The model input is 300×300, even when VDO supplies 640×360.
Logs showing successful loading/preprocessing alone cannot establish why a person
was missed; use these diagnostics to narrow that down.

## Inspect the actual model input

Enable **Model input preview (diagnostic)** under Monitoring policy and save.
The **Actual model input** canvas shows a snapshot of the RGB UINT8 tensor copied
immediately before the inference job, after optional preprocessing. It is not a
resized screenshot from the browser video. For the bundled model it is 300×300.

The snapshot updates at most once per second. Check whether it shows the selected
crop, correct colors, recognizable people, and plausible proportions. Compare
with the live view, allowing for different capture times and video latency. This
helps distinguish an input/preprocessing problem from poor model confidence.
It does not automatically establish which candidate corresponds to a visible
person, and it does not change the confidence threshold.

The diagnostic is off by default. It copies rows using the model's RGB row pitch,
respects the tensor fd offset/capacity, and synchronizes CPU reads of DMA buffers.
Mapping failures appear as a preview error without stopping inference. Stale or
unavailable results hide the image. No image is written to disk. RGB bytes are
base64 encoded into the existing admin-only status endpoint; leave the option off
when finished to avoid its extra memory copies and network traffic. Inference
latency measurements include capture overhead on sampled frames.

Source: `input_preview.c` handles tensor mapping and copying; `model.c` captures
before inference; `main.c` transfers only snapshots belonging to successful fresh
results; `monitor.c` publishes them; the browser draws the RGB bytes without an
additional image resize. `ModelInputPreview` is a persistent boolean parameter.


### Diagnostic preview and logs

The **Actual model input** image is opt-in: enable **Model input preview
(diagnostic)** and click **Save**. When disabled, the page displays instructions
instead of an empty canvas. The snapshot is copied from the RGB tensor before
inference, at most once per second; capture errors appear beside it.

Model/tensor details are logged at startup. Stream settings are logged when
opening each view. Inference summaries are limited to one every ten seconds;
view selection, diagnostic-preview changes, and alarm transitions are logged
when they occur. A log excerpt taken later may omit the startup messages.

The app draws outlined boxes and confidence text using Overlay2. It does not configure privacy masks.
Lowering confidence admits more candidates; it does not improve model accuracy.

### ARTPEC-9 view-crop investigation

The reported camera returned `view=3`, `channel=4`, and RGB/VPP frames while
the model-input image still showed the wider scene. Channel identity alone does
not validate the crop. Both builds use the same capture code: request the model's
RGB format, falling back to YUV only if VDO resolution discovery does not provide
the requested format. There is no ARTPEC-9-specific YUV override. The crop mismatch
remains under investigation; these logs do not establish a firmware defect.

Overlay logging reports candidate stream identity and format, then successful
creation. Matching uses explicit stream view identity when available, otherwise
the resolved channel. The status includes `overlayStreams`; zero streams no
longer produces an “Overlay updated” message. The raw inference stream's
`overlays=off` setting is separate from overlay-enabled encoded video streams.
