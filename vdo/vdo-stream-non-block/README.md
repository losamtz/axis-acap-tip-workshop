# VDO Consumer — NV12 frames to `/dev/null` (Workshop)

This README explains how the **simple VDO consumer** works: it creates a VDO stream, fetches **NV12** frames, logs basic frame metadata, and writes the raw bytes to `/dev/null`. It also breaks down the functions, buffer lifecycle, error handling, and includes a short **mini‑lab** to experiment safely.

> This app is based on patterns from `acap-native-sdk-examples`. It is intentionally small and uses **no model** or overlays.

---

## High‑level flow

```
setup_video_stream()         -> Prepare VDO settings (YUV/NV12, 640x360)
create_stream()              -> vdo_stream_new() + log stream info
start_stream(dest_f)         -> vdo_stream_start(); for N=10 frames:
  ├─ vdo_stream_get_buffer() -> fetch VdoBuffer*
  ├─ vdo_buffer_get_frame()  -> access VdoFrame* metadata
  ├─ vdo_buffer_get_data()   -> pointer to NV12 bytes
  ├─ fwrite(..., dest_f)     -> write bytes (here: /dev/null)
  └─ vdo_stream_buffer_unref()-> return buffer to VDO
main()                       -> installs SIGINT handler, calls above, teardown
```

**Key idea:** *For each frame* you **get** a buffer, **use** its content & metadata, then **return** it to VDO. Never access the buffer after you unref it.

---

## Functions explained

### `setup_video_stream(void)`
Creates a `VdoMap* settings` and populates essential keys:
- `"format" = VDO_FORMAT_YUV` (we aim for NV12)
- `"subformat" = "NV12"`
- `"width" = 640`, `"height" = 360`

> You can tweak width/height here. NV12 is YUV 4:2:0 with *Y plane* followed by *interleaved UV*.

### `create_stream(void)`
- Calls `vdo_stream_new(settings, NULL, &err)` to get `VdoStream* stream`.
- Reads `VdoMap* info = vdo_stream_get_info(stream, &err)` and logs some fields:
  - `format`, `width`, `height`, `framerate`

> On success, it unrefs `settings` and later `info`. If it fails, it logs `err->message` and returns `FALSE`.

### `print_frame(VdoFrame* frame)`
- Uses `vdo_frame_get_is_last_buffer(frame)` to only log once per frame.
- Logs sequence number, type (`VDO_FRAME_TYPE_YUV` → `"yuv"`), and size (`vdo_frame_get_size`).

### `start_stream(FILE* dest_f)`
- Calls `vdo_stream_start(stream, &err)`
- **Loop** `NFRAMES` (=10):
  - `VdoBuffer* buffer = vdo_stream_get_buffer(stream, &err)`
  - `VdoFrame* frame = vdo_buffer_get_frame(buffer)`
  - Optionally abort early if `shutdown` flag was set by SIGINT.
  - `print_frame(frame)`
  - `void* data = vdo_buffer_get_data(buffer)` → NV12 bytes (Y then interleaved UV)
  - `fwrite(data, vdo_frame_get_size(frame), 1, dest_f)` → here `/dev/null`
  - `vdo_stream_buffer_unref(stream, &buffer, &err)` → **return buffer**
- Returns `TRUE` on success, `FALSE` on any error.

> **Note:** If `vdo_buffer_get_data` returns `NULL` on your platform, you can fall back to `vdo_frame_memmap((VdoFrame*)buffer)` to map a pointer temporarily. Do not use it after unref.

### `handle_sigint(int)`
Sets `shutdown = TRUE` so the main loop can exit gracefully.

### `main(void)`
- Opens syslog, sets `SIGINT` handler.
- Opens destination file (`/dev/null`) for writing.
- Calls `setup_video_stream()` → `create_stream()` → `start_stream(dest_f)`.
- On exit, handles `GError` cleanly and unrefs the stream.

---

## Buffer lifecycle & ownership

1. **Obtain** a buffer: `vdo_stream_get_buffer(stream, &err)`
2. **Inspect** metadata: `VdoFrame* f = vdo_buffer_get_frame(buffer)`
3. **Read** bytes: `void* data = vdo_buffer_get_data(buffer)`
   - Layout for NV12: `Y plane` (size `y_stride * height`) followed by `UV plane` (size `uv_stride * (height/2)`)
4. **Use** the data (e.g., write to file or convert YUV→RGB)
5. **Return** the buffer: `vdo_stream_buffer_unref(stream, &buffer, &err)`
   - After this call, **do not** dereference `buffer`, `f`, or `data`

> This app uses `vdo_stream_buffer_unref(...)` (consumer pattern). In other contexts, `g_object_unref()` may be used; follow the pattern your SDK sample uses for your role (producer vs consumer).

---

## Error handling patterns

- Always check `GError* err` after VDO calls. Log `err->message` and `g_clear_error(&err)` before continuing or returning.
- Use `vdo_error_is_expected(&err)` during teardown (e.g., when shutting down early after a signal).
- Log via `syslog(LOG_ERR, ...)` or `syslog(LOG_INFO, ...)` for consistent system logs on the camera.

---

## NV12 reminder

- **Y plane** first (1 byte per pixel): `Y_SIZE = y_stride * height`
- **UV plane** after Y (half resolution each axis, interleaved U,V): `UV_SIZE = uv_stride * (height/2)`
- Total payload you typically write: `Y_SIZE + UV_SIZE`

The exact strides are available from `vdo_stream_get_info()` with keys like `"plane.0.stride"` and `"plane.1.stride"` if you need them.

---

## Mini‑Lab

1. **Change frame count**
   - Set `#define NFRAMES 60` and rerun; observe log rate and behavior.

2. **Capture to file (host)**
   - Replace `"/dev/null"` with a path like `"/tmp/out.nv12"`.
   - `scp` the file to your host and view with ffplay:
     ```bash
     ffplay -f rawvideo -pixel_format nv12 -video_size 640x360 -framerate 30 /tmp/out.nv12
     ```

3. **Inspect strides**
   - Log `plane.0.stride` and `plane.1.stride` from `vdo_stream_get_info()` and assert that `frame->size >= Y_SIZE + UV_SIZE` before writing.

4. **Try another resolution**
   - Change `width/height` in `setup_video_stream()` to a supported pair (e.g., 1280x720). If you pick an unsupported resolution, `vdo_stream_new()` may adjust or fail — check the info map.

5. **Graceful interrupt**
   - Hit Ctrl‑C while running. Verify the app exits without leaks and without “unexpected” errors in syslog.

6. **Optional: memmap fallback**
   - If `vdo_buffer_get_data()` returns `NULL`, swap to:
     ```c
     uint8_t* data = (uint8_t*)vdo_frame_memmap((VdoFrame*)buffer);
     if (!data) { /* handle error */ }
     ```
   - Remember: do not use mapped data after unref.

---

## Build & run


```bash
docker build --tag vdo-consumer --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-consumer):/opt/app ./build
```


Deploy as an ACAP or run in the SDK environment; the app writes frames to `/dev/null` and logs metadata for each of the `NFRAMES` frames.

---


