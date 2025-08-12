# Video Stream API (VDO)

## Description

The VdoChannel API provides:

- query video channel types
- query supported video resolutions

The VdoStream API provides:

- video and image stream
- video and image capture
- video and image configuration

Available video compression formats through VDO
The table below shows available subformats for corresponding YUV format.

| Sub Formats | Corresponding format |
|-------------|----------------------|
| nv12        | YUV                  |
| y800        | YUV                  |

An application to start a vdo stream can be found at vdostream, where the first argument is a string describing the video compression format. It takes h264 (default), h265, jpeg, nv12, and y800 as inputs. Both nv12 and y800 correspond to YUV format of VDO.



## VdoStream: The Video Stream

Represents an active video stream (e.g., from the camera sensor or encoder).

It's the source or sink of video frames, depending on your use case:

- If you're reading frames (e.g., image processing), you get frames from the stream.
- If you're providing frames (e.g., custom image provider), you push frames to the stream.

# ACAP Video Streaming (libvdo) Course Syllabus

## Overview
Hands-on course for ACAP developers to master the `libvdo` API for real-time video capture and processing on Axis devices.

---

## Course Modules

### Module 1: Introduction & Setup
- **Objective**: Get familiar with `libvdo`, tools, and repo examples.
- **Overview**:
  - Learn about `VdoStream`, `VdoFrame`, `VdoBuffer`, `VdoMap`, and `Vdo Types` :contentReference[oaicite:1]{index=1}.
  - Clone and run `vdostream` example from the `acap-native-sdk-examples` repo :contentReference[oaicite:2]{index=2}.
  - Validate environment setup using Docker SDK image :contentReference[oaicite:3]{index=3}.

**Lab**: Run the `vdostream` example; verify it compiles and captures frames.

---

### Module 2: Stream Control & Frame Capture
- **Objective**: Learn stream life-cycle and frame retrieval.
- **Content**:
  - Build settings using `VdoMap`, create and start a stream (`vdo_stream_new`, `vdo_stream_start`, etc.) :contentReference[oaicite:4]{index=4}.
  - Retrieve frames via `vdo_stream_get_buffer()` and inspect `VdoFrame` metadata :contentReference[oaicite:5]{index=5}.
  
**Lab**: Build your own “pull frames” loop that prints timestamps and keyframe info.

---

### Module 3: Snapshot API & Event FDs
- **Objective**: Capture single frames & handle stream events.
- **Content**:
  - Use `vdo_stream_snapshot()` for JPEG captures :contentReference[oaicite:6]{index=6}.
  - Learn to use `VDO_INTENT_EVENTFD`, retrieve event FDs for non-blocking handling :contentReference[oaicite:7]{index=7}.

**Lab**: Implement snapshot feature; then use `get_event_fd()` and `poll()` to trigger actions on key frames.

---

### Module 4: Dynamic Streaming & Channel Control
- **Objective**: Control stream parameters in real-time.
- **Content**:
  - Use `vdo_stream_set_framerate()` and dynamic flags (`dynamic.framerate`, `dynamic.bitrate`, etc.) :contentReference[oaicite:8]{index=8}.
  - Query channel capabilities using `vdo_channel_get()` and `vdo_channel_get_resolutions()` :contentReference[oaicite:9]{index=9}.

**Lab**: Create a stream that adapts framerate dynamically and reports valid resolutions.

---

### Module 5: Advanced Use Cases—Larod Integration
- **Objective**: Combine video capture with on-device inference.
- **Content**:
  - Explore `vdo-larod` example: capture YUV frames and process with Larod API :contentReference[oaicite:10]{index=10}.

**Lab**: Modify the example to convert YUV data to RGB and feed into a simple inference model.

---

### Module 6: Performance Tuning
- **Objective**: Optimize buffer and stream performance.
- **Content**:
  - Understand `buffer.count`, `buffer.strategy`, forced key frames, and bitrate control settings :contentReference[oaicite:11]{index=11}.

**Lab**: Benchmark latency and frame loss under different buffer strategies.

---

### Module 7: Error Handling & GMainLoop Integration
- **Objective**: Write robust, responsive code.
- **Content**:
  - Implement proper `GError` handling.
  - Integrate streaming into `GMainLoop` for event-driven streaming.

**Lab**: Refactor code into a GLib-friendly main loop; simulate error conditions.

---

### Module 8: Final Project
- **Objective**: Build a complete application combining learned modules.
- **Project**:
  - Capture and display video.
  - Show resolution options.
  - Include snapshot and stream event handling.
  - (Optional) Run inference on captured frames.

---

## Extra Resources
- **Repository examples**: `vdostream`, `vdo-larod`, other C samples :contentReference[oaicite:12]{index=12}.
- **ACAP documentation and streaming API reference** :contentReference[oaicite:13]{index=13}.
- **Known issue**: Memory leak in ARTPEC firmware ≤ 10.10 fixed in 10.11.65 :contentReference[oaicite:14]{index=14}.

---

# Sample readme to add to each lab

## 1) YUV/NV12 for Larod — setup + safe plane read
Heads-up: exact option keys differ slightly across ACAP SDK/VDO versions. Two common patterns are shown. If one doesn’t work on your firmware, try the other and/or print the stream’s negotiated settings.

###A. Configure the stream for NV12
Pattern A (common on newer SDKs):

```c
VdoMap *m = vdo_map_new();
vdo_map_set_string(m, "channel", "0");
vdo_map_set_string(m, "format",  "yuv");      // raw
vdo_map_set_string(m, "yuv.format", "nv12");  // NV12 (Y plane + interleaved UV)
vdo_map_set_int   (m, "width",    640);
vdo_map_set_int   (m, "height",   384);
vdo_map_set_int   (m, "framerate", 10);
```

If neither works, quickly dump the settings to see what the device accepted:

```c
VdoStream *s = vdo_stream_new(m, &err);
// after start:
VdoMap *eff = vdo_stream_get_settings(s);
gchar *txt = vdo_map_to_string(eff);
g_print("Effective settings: %s\n", txt);
g_free(txt);

```
### B. Safely read NV12 planes (with strides)

NV12 layout:

- Plane 0 (Y): height rows, strideY bytes per row
- Plane 1 (UV interleaved): height/2 rows, strideUV bytes per row

You must respect stride (don’t assume stride == width).
Below is a “pattern” that works across SDK variants:

- If the SDK exposes helpers like vdo_frame_get_width/height/stride() for each plane, use them.
- If not, read them from the frame’s meta map (keys often look like "width", "height", "y.stride", "uv.stride").

```c
#include <glib.h>
#include <vdo-stream.h>

typedef struct {
  int width, height;
  int strideY, strideUV;
  const uint8_t *ptrY;
  const uint8_t *ptrUV;
} Nv12View;

// Try to fill Nv12View from known helpers or meta map.
static gboolean nv12_view_from_frame(VdoFrame *f, Nv12View *out) {
  memset(out, 0, sizeof(*out));

  // Option 1: direct helpers (if your SDK has them)
  // (Names vary; if these don’t exist, skip to Option 2)
  // out->width   = vdo_frame_get_width(f);
  // out->height  = vdo_frame_get_height(f);
  // out->strideY = vdo_frame_get_stride(f, 0);
  // out->strideUV= vdo_frame_get_stride(f, 1);
  // out->ptrY    = vdo_frame_get_plane_data(f, 0);
  // out->ptrUV   = vdo_frame_get_plane_data(f, 1);
  // return TRUE;

  // Option 2: from meta map (common across versions)
  const VdoMap *meta = vdo_frame_get_meta(f);
  if (!meta) return FALSE;

  out->width   = vdo_map_get_int(meta, "width");
  out->height  = vdo_map_get_int(meta, "height");
  out->strideY = vdo_map_get_int(meta, "y.stride");   // sometimes "stride.y"
  out->strideUV= vdo_map_get_int(meta, "uv.stride");  // sometimes "stride.uv"

  // The backing memory for planes is the buffer’s data blob; the SDK often
  // provides the offsets too. If present, use them:
  int offY  = vdo_map_get_int(meta, "y.offset");      // else assume 0
  int offUV = vdo_map_get_int(meta, "uv.offset");     // else compute from Y size

  const uint8_t *base = vdo_buffer_get_data(vdo_frame_get_buffer(f));
  if (!base) return FALSE;

  if (offY == 0 && offUV == 0) {
    // Compute fallback offsets conservatively using strides:
    size_t yBytes = (size_t)out->strideY * (size_t)out->height;
    offY  = 0;
    offUV = (int)yBytes;
  }

  out->ptrY  = base + offY;
  out->ptrUV = base + offUV;
  return (out->width > 0 && out->height > 0 && out->strideY > 0 && out->strideUV > 0);
}

// Example: copy NV12 into a tightly-packed tensor buffer for Larod
static void pack_nv12_tight(const Nv12View *nv12, uint8_t *dst) {
  // Copy Y
  for (int r = 0; r < nv12->height; r++) {
    memcpy(dst + r * nv12->width, nv12->ptrY + r * nv12->strideY, nv12->width);
  }
  // Copy UV (height/2 rows), each row is width bytes (UV interleaved)
  uint8_t *dstUV = dst + (size_t)nv12->width * nv12->height;
  for (int r = 0; r < nv12->height/2; r++) {
    memcpy(dstUV + r * nv12->width, nv12->ptrUV + r * nv12->strideUV, nv12->width);
  }
}

// Inside your pull loop
static void process_one_nv12(VdoBuffer *buf) {
  VdoFrame *f = vdo_buffer_get_frame(buf);
  Nv12View v;
  if (!nv12_view_from_frame(f, &v)) {
    g_printerr("NV12 view not available (check meta/keys)\n");
    return;
  }
  // Allocate a tight NV12 blob: Y (w*h) + UV (w*h/2)
  size_t tightSize = (size_t)v.width * v.height * 3 / 2;
  uint8_t *tight = g_malloc(tightSize);
  pack_nv12_tight(&v, tight);

  // …send `tight` to Larod (as tensor or buffer)…
  // larod_set_input(buffer) / invoke … (left out here)

  g_free(tight);
}
```
### Why pack to “tight” NV12?
Larod models often expect contiguous (no padding) tensors. Packing respects device strides while giving Larod exactly width*height + width*height/2 bytes.

--

# 2) README templates with checklists (one per lab)
Use these as README.md files in each lab folder. Each has a Goal, What you’ll learn, Steps, Validation, and Troubleshooting. Add screenshots where noted.

lab1_hello_stream/README.md
### Lab 1 — Hello VdoStream

Goal: Open a stream, start it, and print timestamp/keyframe for ~60 frames.
You’ll learn: Creating VdoStream, pulling buffers, returning buffers.

Steps
Build: meson setup build && meson compile -C build lab1_hello_stream

Run on device: ./lab1_hello_stream

Observe console lines like:
Frame 12 | pts=123456789us | key=1

Screenshot to capture
Terminal output with at least one keyframe logged.

Troubleshooting
If vdo_stream_new fails, print error and verify channel/format.

If you get timeouts, ensure the camera’s stream is enabled and resolution is supported.

--

lab2_snapshot/README.md
### Lab 2 — Snapshot API

Goal: Save a JPEG snapshot to /tmp/snap.jpg.
You’ll learn: Using vdo_stream_snapshot(), writing a buffer to file.

Steps
Build & run: meson compile -C build lab2_snapshot && ./lab2_snapshot

SCP or browse to /tmp/snap.jpg.

Screenshot to capture
Snapshot image (thumbnail) or file listing showing non-zero size.

Troubleshooting
If snapshot times out, bump the timeout to 3000 ms and ensure format=jpeg is acceptable for your channel.

--

lab3_eventfd_poll/README.md
### Lab 3 — Event FD + poll()

Goal: Use the stream’s event file descriptor; log only keyframes.
You’ll learn: poll() integration, non-blocking buffer pulls, keyframe check.

Steps
Build & run: meson compile -C build lab3_eventfd_poll && ./lab3_eventfd_poll

Confirm periodic KEYFRAME pts=... messages.

Screenshot to capture
Terminal showing poll timeouts and keyframe lines.

Troubleshooting
If no keyframes, ensure GOP/keyframe interval is non-infinite on the video configuration.

--

lab4_dynamic_fps/README.md
### Lab 4 — Dynamic FPS

Goal: Change FPS every 3 seconds while the stream is running.
You’ll learn: GMainLoop, GLib timers, vdo_stream_set_framerate() (or dynamic map).

Steps
Build & run: meson compile -C build lab4_dynamic_fps && ./lab4_dynamic_fps

Observe “Framerate set to …” and frames being logged.

Screenshot to capture
Terminal lines showing framerate changes and ongoing frames.

Troubleshooting
If set_framerate isn’t supported, log it and try re-applying a small settings map if your SDK allows it.

--

lab5_caps/README.md
### Lab 5 — Capability Query

Goal: Pick a valid resolution from capabilities and start the stream.
You’ll learn: Reading channel caps, parsing a resolution list, configuring width/height.

Steps
Build & run: meson compile -C build lab5_caps && ./lab5_caps

Confirm “Using WxH” then frames being pulled.

Screenshot to capture
Terminal lines showing the chosen resolution and frame sizes.

Troubleshooting
If you can’t find capability keys, print the entire map and inspect (vdo_map_to_string).

--

lab6_graceful/README.md
### Lab 6 — Graceful Shutdown

Goal: Run a GMainLoop, pull frames, handle SIGINT/SIGTERM cleanly.
You’ll learn: Signal handling, stopping the stream, freeing resources.

Steps
Build & run: meson compile -C build lab6_graceful && ./lab6_graceful

Press Ctrl+C and verify the app exits cleanly without leaks.

Screenshot to capture
Terminal showing frames then a clean shutdown after Ctrl+C.

Troubleshooting
If it doesn’t quit, ensure your signal handler calls g_main_loop_quit.

--

labX_nv12_larod/README.md (optional “Larod bridge” lab)
## Lab X — NV12 to Larod

Goal: Stream NV12 frames and pack them into a tight buffer suitable for Larod.
You’ll learn: Handling strides, plane offsets, packing tensors.

Steps
Configure stream for NV12 (see code).

Pull buffer, build Nv12View, pack to tight NV12.

Feed the packed buffer to your Larod input.

Screenshot to capture
Terminal showing frame dimensions and successful Larod inference (if hooked up).

Troubleshooting
If plane offsets/strides are zero or missing, dump frame meta and adjust key names ("y.stride", "uv.stride", "y.offset", "uv.offset" vary by SDK).
--
