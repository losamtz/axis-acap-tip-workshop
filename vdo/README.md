# VDO API 

Here’s a compact “map” of the three VDO samples in this repo, what each one does, the core APIs they exercise, and when you’d pick each.

## vdo-consumer

**Goal:** Smallest possible reader of NV12 frames. Creates a VDO stream, pulls N frames, logs metadata, writes raw bytes to `/dev/null`.

**What it demonstrates**
- Building a `VdoMap` with `format=YUV`, `subformat=NV12`, `width/height`.
- `vdo_stream_new` → `vdo_stream_start` → `vdo_stream_get_buffer`.
- Reading payload (`vdo_buffer_get_data`) and frame meta (`vdo_buffer_get_frame`); logging seq/size.
- Returning buffers with `vdo_stream_buffer_unref`.

**Good for**
- Verifying your toolchain/SDK, sanity-checking resolutions/strides, quick raw dumps.

**Typical PKGs**
- `glib-2.0 gobject-2.0 vdostream`

---

## vdo-producer

**Goal:** Writer/producer path that fills NV12 buffers and shows BBox overlays; no model required—detections are simulated.

**What it demonstrates**
- Producer-side buffer flow with **EXPLICIT** strategy (`buffer.strategy=VDO_BUFFER_STRATEGY_EXPLICIT`, `buffer.access=…`, `buffer.count`).
- Allocating/enqueuing buffers: `vdo_stream_buffer_alloc` → fill Y/UV → set frame type/size/timestamp → `vdo_stream_buffer_enqueue`.
- Querying applied strides via `vdo_stream_get_info` (`plane.0/plane.1` stride), computing payload size.
- Drawing overlays with **BBox** (`bbox_view_new`, `bbox_rectangle`, `bbox_commit`) and optional rotation awareness via `get_stream_rotation`.

**Good for**
- Teaching producer ownership, memory layout of NV12 (Y plane then interleaved UV), and how to drive overlays without ML.


---

## vdo-utilities

**Goal:** Small toolkit to explore the platform: list channels, enumerate supported resolutions, create an NV12 stream, read rotation.

**What it demonstrates**
- Channel enumeration (`vdo_channel_get_all`) and per-channel resolutions (`vdo_channel_get_resolutions` with filters like `aspect_ratio=native`).
- Creating a stream from a simple `image_t { format, width, height }` description.
- Reading live stream info (`rotation`, `width`, `height`, `framerate`) via `vdo_stream_get_info`.

**Good for**
- Discoverability/workshop intros: “what channels are there?”, “which resolutions are valid?”, “what’s the current rotation?”



---

