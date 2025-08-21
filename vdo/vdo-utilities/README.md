# VDO Utilities — Channels, Resolutions & Stream Info (NV12)

This app is a tiny **VDO toolkit** you can use in a workshop to query **video channels**, list **supported resolutions**, create an **NV12 stream**, and read the stream’s **rotation**. It’s intentionally small and easy to extend.

> There is **no model** or overlay here. It’s a utilities app to explore the VDO API safely.

---

## What it does

1. **Enumerates channels** (logical video pipelines) with `vdo_channel_get_all()`
2. **Lists resolutions** for a chosen channel (`aspect_ratio = "native"`)
3. **Creates a stream** with NV12 (YUV 4:2:0) at a chosen resolution
4. **Reads rotation** from the live stream’s info map

The provided `main` builds a stream then immediately tears it down (no frame fetch), which makes it safe for quick checks.

---

## File layout (3 files)

```
app/
├─ utilities.h        # types and function declarations
├─ utilities.c        # implementation (channels, resolutions, stream create/start, rotation)
└─ vdo_utilities.c    # main() – calls the utilities in order
# (panic.h/.c exists in your repo but is not required reading for this README)
```

---



## Runtime flow (in `vdo_utilities.c`)

```c
int main(void) {
    // 1) List channels & resolutions
    get_video_channels();
    get_stream_resolutions();

    // 2) Create an Image descriptor (format + target WxH)
    image_t *img = create_image(640, 360, VDO_FORMAT_YUV);

    // 3) Create stream (NV12)
    create_stream(img);

    // 4) Query rotation (from stream info)
    get_stream_rotation(img);

    // 5) Cleanup
    if (img && img->vdo_stream) g_object_unref(img->vdo_stream);
    g_free(img);
}
```

This version **doesn’t start** the stream or fetch frames; it’s a pure utilities pass. (See *Mini‑Lab* for ideas to extend.)

---

## Data types & ownership

### `image_t`
```c
typedef struct image {
    VdoFormat  vdo_format;   // e.g., VDO_FORMAT_YUV
    VdoStream* vdo_stream;   // created by create_stream()
    unsigned   width, height;
} image_t;
```

- `create_image(w,h,format)` allocates and fills this struct.
- `create_stream(image*)` creates and stores the `VdoStream*` inside.
- **Ownership:** `image->vdo_stream` must be released with `g_object_unref()` when done. The `image_t*` itself must be `g_free()`’d by the caller.

---

## Functions (in `utilities.c`)

### `image_t* create_image(unsigned int w, unsigned int h, VdoFormat format)`
Allocates and initializes the `image_t` container—just metadata, no VDO calls. Logs format.

**Errors:** panics on allocation failure.

---

### `void create_stream(image_t *image)`
Builds a `VdoMap* settings` and sets:
- `"format"    = image->vdo_format` (e.g., `VDO_FORMAT_YUV`)
- `"subformat" = "nv12"`            (NV12 under the YUV umbrella)
- `"width"     = image->width`
- `"height"    = image->height`

Then:
- `VdoStream* s = vdo_stream_new(settings, NULL, &err);`
- On success: `image->vdo_stream = s`, logs “Stream created…”
- Always unrefs `settings`, clears `err`

**Errors:** `panic()` if stream creation fails (e.g., unsupported resolution).

> Tip: If you need a specific channel, add `vdo_map_set_uint32(settings, "channel", 1u);` before `vdo_stream_new(...)`.

---

### `gboolean start_stream(image_t *image)`
Starts the stream:
```c
if (!vdo_stream_start(image->vdo_stream, &err)) return FALSE;
```
Logs success and returns `TRUE`.

**Note:** The current `main` doesn’t call this — it’s provided for your experiments.

---

### `void get_stream_resolutions(void)`
Uses VDO Channel API to list resolutions:
- `VdoChannel* ch = vdo_channel_get(VDO_CHANNEL, &err);`
- Adds filter: `aspect_ratio = "native"`
- `VdoResolutionSet* set = vdo_channel_get_resolutions(ch, map, &err);`
- Loops and logs: `[%zu] WIDTHxHEIGHT`

**Memory:** `g_object_unref(map)`, `g_clear_object(&ch)`, `g_free(set)`, and clear `err`.

**Errors:** `panic()` on channel or set retrieval failure.

---

### `void get_video_channels(void)`
- Gets a list with `vdo_channel_get_all(&err)`
- Logs the channel IDs (and “camera number” = `id + 1`)
- Frees with `g_list_free_full(channels, (GDestroyNotify)g_object_unref)`

---

### `void get_stream_rotation(image_t *image)`
- `VdoMap* info = vdo_stream_get_info(image->vdo_stream, &err)`
- Logs `rotation = vdo_map_get_uint32(info, "rotation", 0)`
- Unrefs `info`, handles `err`

**Heads‑up:** If you reconfigure video in the web UI while running, `vdo_stream_get_info` can briefly fail (e.g., *“Interface down 'FIDO'”*). For a utility app you might `panic()`, but in production treat it as transient and retry.

---

## Notes on NV12

- NV12 is YUV 4:2:0 (Y plane + interleaved UV plane). Here we **don’t** fetch frames or touch bytes — the app only creates/starts streams and queries info. If you later fetch frames, remember to respect **strides** (`plane.0.stride` and `plane.1.stride` from `vdo_stream_get_info`).

---

## Build 


```bash
docker build --tag vdo-consumer --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-consumer):/opt/app ./build
```



--- 
## Lab

1. **Start the stream**
   ```c
   if (!start_stream(img)) { /* handle */ }
   ```
   Then call `vdo_stream_stop(img->vdo_stream)` on teardown.

2. **Fetch & print N frames**
   - After `start_stream`, call `vdo_stream_get_buffer(stream, &err)` a few times.
   - Log `vdo_frame_get_sequence_nbr` and `vdo_frame_get_size`.
   - Return buffer with `vdo_stream_buffer_unref(stream, &buffer, &err)`.

3. **Pick a specific channel**
   - Add `vdo_map_set_uint32(settings, "channel", 1u);` in `create_stream()`.
   - Compare rotation or resolution availability across channels.

4. **Try another resolution**
   - Read the list from `get_stream_resolutions()` and pick an entry.
   - Update `image->width/height`, recreate the stream, observe info in logs.

5. **Log strides & framerate**
   - From `info`, log `"plane.0.stride"`, `"plane.1.stride"`, `"framerate"`.
   - Useful when you later want to write raw NV12 to files.

6. **Handle transient reconfigure**
   - Replace `panic()` in `get_stream_rotation()` with a retry loop that sleeps for a few ms if the interface is down.

---

## Teardown & safety

- If you started the stream: `vdo_stream_stop(image->vdo_stream);`
- Always `g_object_unref(image->vdo_stream);`
- `g_free(image);`
- Clear any `GError*` with `g_clear_error(&err);`
- Avoid using any buffer/stream pointer after unref/stop.

---

## Troubleshooting

- **Stream creation fails**: Resolution/format not supported on that channel; log `err->message` and try another size.
- **Rotation info fails**: Likely transient pipeline reconfigure; treat as retryable (don’t crash in production).

---

