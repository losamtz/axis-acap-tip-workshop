# VDO Workshop — Image Provider + BBox Simulation

This README explains the **image provider** (a thin abstraction on top of VDO), the **concurrency design** (queues, thread, mutex/cond), and how the **app** uses it to draw synthetic bounding boxes via the Axis **BBox** overlay library — *without any ML model*. It also includes a short **lab** so workshop participants can explore and modify the code.

---

## High‑level architecture

```
            ┌──────────────────────────┐
            │         VDO Stream       │
            │  (NV12 frames from ISP)  │
            └────────────┬─────────────┘
                         │ (VdoBuffer*)
                Fetcher Thread (producer)
                         │
         (mutex+cond)    ▼
  ┌───────────────────────────────────────┐
  │  delivered_frames : GQueue            │  <-- frames ready for app
  ├───────────────────────────────────────┤
  │  processed_frames : GQueue            │  <-- frames returned by app
  └───────────────────────────────────────┘
                         ▲
                         │  get_last_frame_blocking() / return_frame()
                         │
                Application (consumer)
         draws BBox overlays & returns frame
```

- The **fetcher thread** pulls `VdoBuffer*` from VDO and **pushes** them to `delivered_frames`.
- The **app** calls `get_last_frame_blocking()` to pop from `delivered_frames`, uses the frame, then calls `return_frame(...)` which **pushes** it to `processed_frames` (handing ownership back to the provider).
- A `pthread_mutex_t` protects both queues. A `pthread_cond_t` wakes the app when a frame arrives.

> Ownership rule: a frame you received **must** be returned via `return_frame()` once processed. The provider then either unrefs the buffer (returning it to VDO) or keeps a ref in `processed_frames` to be recycled (depends on your implementation style).

---

## Image provider API (workshop variant)

Typical header (`imgprovider.h`):
```c
typedef struct img_provider img_provider_t;

/* Create a consumer for NV12 at a given resolution. depth = queue capacity. */
img_provider_t* create_img_provider(unsigned int width,
                                    unsigned int height,
                                    unsigned int depth,
                                    VdoFormat format /* VDO_FORMAT_YUV for NV12 */);

void destroy_img_provider(img_provider_t* p);

/* Start/stop background fetcher that fills delivered_frames. */
void start_frame_fetch(img_provider_t* p);
void stop_frame_fetch(img_provider_t* p);

/* Block until a frame is ready; returns a VdoBuffer* (owned by app until returned). */
VdoBuffer* get_last_frame_blocking(img_provider_t* p);

/* Give the frame back to the provider (will unref or recycle). */
void return_frame(img_provider_t* p, VdoBuffer* buf);

/* Optional helper to read stream rotation (0/90/180/270). */
guint32 get_stream_rotation(img_provider_t* p);
```

### Internal structure (`imgprovider.c`)

```c
struct img_provider {
    // VDO state
    VdoStream* stream;
    guint width, height;

    // Concurrency primitives
    pthread_mutex_t frame_mutex;
    pthread_cond_t  frame_deliver_cond;
    pthread_t       fetcher_thread;
    gboolean        running;

    // Two queues
    GQueue* delivered_frames;  // frames ready for consumer
    GQueue* processed_frames;  // frames returned by consumer

    // (Optional) max queue length to prevent unbounded growth
    guint capacity;
};
```

- `frame_mutex` protects both queues and `running` flag.
- `frame_deliver_cond` wakes the consumer waiting in `get_last_frame_blocking()`.
- `fetcher_thread` continuously fetches frames and enqueues to `delivered_frames`.

### Queue operations (key lines)

**Producer (fetcher thread) delivers a frame:**
```c
// ... after fetching a VdoBuffer* 'buffer' from VDO ...
pthread_mutex_lock(&p->frame_mutex);
g_queue_push_tail(p->delivered_frames, buffer);
pthread_cond_signal(&p->frame_deliver_cond);  // wake any waiting consumer
pthread_mutex_unlock(&p->frame_mutex);
```

**Consumer (app) returns the frame when done:**
```c
void return_frame(img_provider_t* p, VdoBuffer* buffer) {
    pthread_mutex_lock(&p->frame_mutex);
    g_queue_push_tail(p->processed_frames, buffer);
    pthread_mutex_unlock(&p->frame_mutex);
    // In a simple implementation, a janitor path or the fetcher thread
    // will drain processed_frames and g_object_unref() (return to VDO).
}
```

**Consumer (app) waits for the next frame:**
```c
VdoBuffer* get_last_frame_blocking(img_provider_t* p) {
    pthread_mutex_lock(&p->frame_mutex);
    while (g_queue_is_empty(p->delivered_frames) && p->running) {
        pthread_cond_wait(&p->frame_deliver_cond, &p->frame_mutex);
    }
    VdoBuffer* buf = NULL;
    if (!g_queue_is_empty(p->delivered_frames))
        buf = g_queue_pop_tail(p->delivered_frames);
    pthread_mutex_unlock(&p->frame_mutex);
    return buf; // app must later call return_frame(p, buf)
}
```

> **Why `g_queue_pop_tail`?** We favor the **most recent** frame (LIFO on delivered side) for low‑latency viewing, dropping older frames if the app falls behind (common in real‑time pipelines). You can switch to `g_queue_pop_head()` for FIFO behavior.

### Fetcher thread lifecycle

```c
static void* fetcher_main(void* arg) {
    img_provider_t* p = arg;
    GError* err = NULL;

    while (g_atomic_int_get(&p->running)) {
        VdoBuffer* buffer = vdo_stream_get_buffer(p->stream, &err);
        if (!buffer) {
            // Transient: interface reconfigure / pipeline pause
            if (err) { syslog(LOG_WARNING, "get_buffer: %s", err->message); g_clear_error(&err); }
            usleep(5*1000); // back off a bit
            continue;
        }

        pthread_mutex_lock(&p->frame_mutex);

        // Optional: cap queue length (drop oldest if too many pending)
        if (p->capacity && g_queue_get_length(p->delivered_frames) >= p->capacity) {
            VdoBuffer* old = g_queue_pop_head(p->delivered_frames);
            g_object_unref(old); // return to VDO
        }

        g_queue_push_tail(p->delivered_frames, buffer);
        pthread_cond_signal(&p->frame_deliver_cond);
        pthread_mutex_unlock(&p->frame_mutex);
    }
    return NULL;
}
```

**Start/stop:**
```c
void start_frame_fetch(img_provider_t* p) {
    p->running = TRUE;
    pthread_create(&p->fetcher_thread, NULL, fetcher_main, p);
}

void stop_frame_fetch(img_provider_t* p) {
    pthread_mutex_lock(&p->frame_mutex);
    p->running = FALSE;
    pthread_cond_broadcast(&p->frame_deliver_cond);
    pthread_mutex_unlock(&p->frame_mutex);
    pthread_join(p->fetcher_thread, NULL);

    // Drain queues and unref any remaining buffers
    while (!g_queue_is_empty(p->delivered_frames))
        g_object_unref(g_queue_pop_head(p->delivered_frames));
    while (!g_queue_is_empty(p->processed_frames))
        g_object_unref(g_queue_pop_head(p->processed_frames));
}
```

---

## The demo app (no model; synthetic boxes)

Your `vdo_producer` / `object_detection_bbox_sim_*` demo does this:

1. Creates an `img_provider_t` for NV12 frames (e.g., 640×360).
2. Starts fetcher: `start_frame_fetch()`
3. In a loop:
   - `buf = get_last_frame_blocking()`
   - Generate **synthetic detections** (x,y,w,h in normalized 0..1).
   - Convert to corners (respecting rotation if used) and draw via **BBox**:
     ```c
     bbox_clear(bbox);
     bbox_rectangle(bbox, x1, y1, x2, y2);
     bbox_commit(bbox, 0u);
     ```
   - `return_frame(provider, buf)`
4. On shutdown: `stop_frame_fetch()`, destroy provider, destroy BBox.

This shows how a real model pipeline would work without needing larod or a model file.

---

## Ownership & thread‑safety summary

- A `VdoBuffer*` is a GLib object with refcounting.
- The provider **owns** frames while they sit in its queues.
- The app **temporarily owns** a single frame after `get_last_frame_blocking()` and **must** give it back via `return_frame()`.
- All queue operations are guarded by `frame_mutex`.
- `frame_deliver_cond` is signaled when a new frame is pushed into `delivered_frames`.

---

## Lab

1. **Build & run**
   - Ensure your Makefile links: `glib-2.0 gobject-2.0 vdostream bbox`.
   - Deploy the app to your camera (as an ACAP) and start it.
   - You should see red boxes moving on the camera’s video output.

2. **Try FIFO vs. LIFO delivery**
   - In `get_last_frame_blocking()`, switch `g_queue_pop_tail` → `g_queue_pop_head`.
   - Observe latency differences if the app logs work per frame.

3. **Experiment with queue capacity**
   - Add `p->capacity = 3;` and drop oldest frames in the fetcher when full.
   - Simulate a slow consumer by inserting `usleep(100*1000);` in the app loop.

4. **Add per‑frame FPS logging**
   - Measure time between `bbox_commit` calls and print FPS with `syslog(LOG_INFO, ...)`.

5. **Overlay styling**
   - Change from outline to filled boxes:
     ```c
     bbox_style_fill(bbox);
     bbox_color(bbox, bbox_color_from_rgb(0x00, 0xff, 0x00)); // green
     ```

6. **Rotation handling (optional)**
   - Call `int rot = (int)get_stream_rotation(provider);`
   - Update your `find_corners` to handle 90/270 degrees if desired.

7. **Safety: transient pipeline reconfig**
   - If you change rotation or stream settings in the web UI, the VDO pipeline
     can go briefly “interface down”. The fetcher loop should log a warning,
     clear `GError`, and keep retrying — do not `panic` on this condition.

---

## Troubleshooting

- **`vdo_buffer_get_frame` assertion**: Set frame type before getting a frame handle (producer flow). For consumer flow (this app), you don’t need to set frame type; you just read data and return the buffer.
- **“Interface down 'FIDO'”** on `vdo_stream_get_info`: Treat as transient; retry later.
- **No frames**: Ensure your channel / requested resolution is valid. Start with 640×360, NV12.
- **-Werror warnings**: Prefer braces/newlines, silence unused parameters with `(void)argc;` etc.

---



## File/Module recap

- `imgprovider.h/.c` — VDO consumer wrapper (queues, thread, mutex/cond).
- `vdo_provider.c` — workshop demo app (no model, synthetic boxes).
- `panic.h/.c` — small `panic(...)` helper that logs to syslog and exits.


---