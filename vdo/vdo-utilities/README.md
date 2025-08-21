# Video Stream API - VDO Utilities
This lab walks you through a tiny **VDO YUV stream** app that uses the **VdoChannel** API to enumerate channels and resolutions, and the **VdoStream** API to create/start a stream. 

---

## What you’ll build

A small program that:

- Lists **available video channels** (`vdo_channel_get_all`)
- Prints **supported resolutions** for a given channel (`vdo_channel_get_resolutions`)
- Creates/starts a **YUV (NV12) VDO stream** on a chosen channel (`vdo_stream_new`, `vdo_stream_start`)
- Reads basic info like stream **rotation** (`vdo_stream_get_info`)
- Runs until until you stop in the app section

> This version starts a stream but doesn’t produce/consume frames. You can extend it to be a producer (write NV12) or a consumer (read buffers).

---


## App walkthrough

### Functions

#### `get_video_channels(void)`
- Calls `vdo_channel_get_all()` to get a `GList*` of `VdoChannel*`.
- For each channel, uses `vdo_channel_get_id()` to print the id.
- Frees the list with `g_list_free_full(..., g_object_unref)`.

#### `get_stream_resolutions(void)`
- Calls `vdo_channel_get(VDO_CHANNEL, &error)` to get a single channel.
- Creates a filter `VdoMap` with `"aspect_ratio" = "native"`.
- Calls `vdo_channel_get_resolutions(channel, filter, &error)` and prints each `width x height`.
- Frees: `VdoResolutionSet*` via `g_free(set)`, unrefs the channel and filter.

> Tip: You can also filter by `"format" = VDO_FORMAT_YUV` and `"subformat" = "nv12"` if you want YUV-only resolutions.

#### `create_stream(image_t *image, unsigned w, unsigned h)`
- Prepares a `VdoMap` with:
  - `"format" = VDO_FORMAT_YUV`
  - `"subformat" = "nv12"`
  - `"width"`, `"height"`
  - `"channel" = VDO_CHANNEL`
- Creates the stream: `vdo_stream_new(settings, NULL, &error)`
- Starts it: `vdo_stream_start(stream, &error)`
- Stores the `VdoStream*` in `image->vdo_stream` for later use.

#### `create_image(unsigned w, unsigned h, unsigned num_frames, VdoFormat fmt)`
- Allocates and fills a small struct with stream format and frame count.
- Calls `create_stream(...)` to create/start the stream.
- Returns the `image_t*` back to main.

#### `get_stream_rotation(image_t *image)`
- Calls `vdo_stream_get_info(image->vdo_stream, &error)`.
- Reads `"rotation"` from the returned `VdoMap` with `vdo_map_get_uint32(...)`.


---

## Mini Lab Exercises

1) **Enumerate channels**  
   - Change `#define VDO_CHANNEL` to `0` or `1` and observe the difference.
   - Use `get_video_channels()` to see which channels exist on your model.

2) **List resolutions by format**  
   - In `get_stream_resolutions()`, add:
     ```c
     vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
     vdo_map_set_string(settings, "subformat", "nv12");
     ```
   - Compare the results with and without the filter.

3) **Change stream size**  
   - In `main()`, change resolution to a supported size printed in step 2.
   - Rebuild & run; confirm the stream starts.


5) **(Optional) Consume frames**  
   - Switch to a consumer: set `"format" = VDO_FORMAT_YUV`, start the stream, then:
     ```c
     VdoBuffer *buf = vdo_stream_get_buffer(st, &err);
     if (buf) {
       const guint8 *data = vdo_buffer_get_data(buf);
       const gsize   size = vdo_frame_get_size(buf);
       // Inspect data or measure latency...
       g_object_unref(buf);
     }
     ```
   - Ensure you don’t mix producer/consumer buffer APIs in the same stream.

6) **(Optional) Produce frames**  
   - For producer mode, set `"buffer.strategy" = EXPLICIT` & a write-capable `"buffer.access"`, then per frame:
     ```c
     VdoBuffer *buf = vdo_stream_buffer_alloc(st, NULL, &err);
     guint8 *base = (guint8*)vdo_buffer_get_data(buf);
     // Fill NV12 (Y then interleaved UV), set timestamp, enqueue
     vdo_frame_set_timestamp(buf, g_get_monotonic_time());
     vdo_stream_buffer_enqueue(st, buf, &err);
     ```

---


## Build

```bash
docker build --tag vdo-utilities --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-utilities):/opt/app ./build
```




