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

## Next Steps
- Add a **CI pipeline** using `vdostream` sample to verify continuity.
- Create **Mermaid diagrams** for streaming architecture.
- Expand to other APIs: Overlay, Parameter, Event systems (**cross-API labs**).

---

Let me know if you want full sample code for each lab or integrations into a GitHub classroom setup!
::contentReference[oaicite:15]{index=15}
