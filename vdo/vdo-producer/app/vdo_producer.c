// vdo_producer.c — NV12 producer + BBox overlay (robust view selection)
// Build PKGS: glib-2.0 gobject-2.0 vdostream bbox


#include <glib-object.h>
#include <glib-unix.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <vdo-stream.h>
#include <vdo-map.h>
#include <vdo-frame.h>
#include <vdo-types.h>
#include <bbox.h>

/* Choose a write-capable buffer access enum available in your SDK */
#if defined(VDO_BUFFER_ACCESS_ANY_RW)
  #define BUF_ACCESS VDO_BUFFER_ACCESS_ANY_RW
#elif defined(VDO_BUFFER_ACCESS_CPU_RW)
  #define BUF_ACCESS VDO_BUFFER_ACCESS_CPU_RW
#elif defined(VDO_BUFFER_ACCESS_ANY_WR)
  #define BUF_ACCESS VDO_BUFFER_ACCESS_ANY_WR
#elif defined(VDO_BUFFER_ACCESS_CPU_WR)
  #define BUF_ACCESS VDO_BUFFER_ACCESS_CPU_WR
#else
  #define BUF_ACCESS VDO_BUFFER_ACCESS_NONE
#endif

static void die(const char *msg, GError *err) {
  g_printerr("%s: %s\n", msg, err ? err->message : "unknown");
  if (err) g_clear_error(&err);
  closelog();
  exit(1);
}

int main(void) {
  openlog("vdo_producer", LOG_PID, LOG_USER);

  /* Config */
  const guint WIDTH = 640, HEIGHT = 360, FPS = 30, FRAMES = 300;

  /* --- 1) Pick a usable BBox view (channel) --- */
  int candidates[] = {1, 0, 2, 3, 4, 5, 6, 7};
  int chosen_view = -1;
  bbox_t *bbox = NULL;

  for (guint i = 0; i < G_N_ELEMENTS(candidates); ++i) {
    int v = candidates[i];
    bbox = bbox_view_new((bbox_channel_t)v);
    if (bbox) { chosen_view = v; break; }
  }
  if (!bbox) {
    g_printerr("bbox_view_new: no usable view (try 0)\n");
    die("bbox_view_new", NULL);
  }
  syslog(LOG_INFO, "Using BBox view/channel: %d", chosen_view);

  /* BBox basic styling/config */
  bbox_coordinates_frame_normalized(bbox);        // x1..y2 in [0..1]
  bbox_video_output(bbox, true);                  // make it appear on video outputs
  bbox_style_outline(bbox);
  bbox_thickness_thin(bbox);
  bbox_color(bbox, bbox_color_from_rgb(0xff, 0x00, 0x00)); // red

  /* --- 2) Create NV12 producer bound to the same channel --- */
  GError *err = NULL;
  VdoMap *s = vdo_map_new();
  vdo_map_set_uint32(s, "format",          VDO_FORMAT_YUV);
  vdo_map_set_string(s, "subformat",       "nv12");
  vdo_map_set_uint32(s, "width",           WIDTH);
  vdo_map_set_uint32(s, "height",          HEIGHT);
  vdo_map_set_uint32(s, "framerate",       FPS);
  vdo_map_set_uint32(s, "buffer.strategy", VDO_BUFFER_STRATEGY_EXPLICIT);
  vdo_map_set_uint32(s, "buffer.access",   BUF_ACCESS);
  vdo_map_set_uint32(s, "buffer.count",    4);
  vdo_map_set_uint32(s, "channel",         (guint)chosen_view);  // <-- match BBox view
  // Optional: show overlays on this stream if your platform requires:
  // vdo_map_set_string(s, "overlays", "application");

  VdoStream *st = vdo_stream_new(s, NULL, &err);
  g_object_unref(s);
  if (!st) { bbox_destroy(bbox); die("vdo_stream_new", err); }

  /* Optional: PRODUCE intent (some platforms infer it on first enqueue) */
  VdoMap *intent = vdo_map_new();
  vdo_map_set_uint32(intent, "intent", VDO_INTENT_PRODUCE);
  if (!vdo_stream_attach(st, intent, &err)) g_clear_error(&err);
  g_object_unref(intent);

  if (!vdo_stream_start(st, &err)) { bbox_destroy(bbox); die("vdo_stream_start", err); }

  /* Query applied strides */
  guint y_stride = WIDTH, uv_stride = WIDTH;
  VdoMap *info = vdo_stream_get_info(st, &err);
  if (info) {
    y_stride  = vdo_map_get_uint32(info, "plane.0.stride", WIDTH);
    uv_stride = vdo_map_get_uint32(info, "plane.1.stride", WIDTH);
    g_object_unref(info);
  } else {
    g_clear_error(&err);
  }
  const gsize Y_SIZE  = (gsize)y_stride  * HEIGHT;
  const gsize UV_SIZE = (gsize)uv_stride * (HEIGHT / 2);
  const gsize NEED_SZ = Y_SIZE + UV_SIZE;

  /* --- 3) Produce frames + commit overlay each frame --- */
  for (guint i = 0; i < FRAMES; ++i) {
    VdoBuffer *buf = vdo_stream_buffer_alloc(st, NULL, &err);
    if (!buf) { g_clear_error(&err); break; }

    vdo_frame_set_frame_type(buf, VDO_FRAME_TYPE_YUV);  // NV12/YUV
    vdo_frame_set_size(buf, NEED_SZ);

    /* Access memory (prefer direct pointer; fallback to memmap) */
    guint8 *base = (guint8*)vdo_buffer_get_data(buf);
    if (!base) base = (guint8*)vdo_frame_memmap(buf);
    if (!base) { g_object_unref(buf); break; }

    /* Fill background (dark gray) */
    memset(base,        0x10, Y_SIZE);
    memset(base+Y_SIZE, 0x80, UV_SIZE);

    /* Moving box in normalized coords (0..1) */
    double t = (i % FPS) / (double)FPS;  // 0..1 over ~1s
    float x1 = 0.10f + 0.60f*(float)t, y1 = 0.20f;
    float x2 = x1 + 0.20f,             y2 = 0.50f;

    bbox_clear(bbox);
    bbox_rectangle(bbox, x1, y1, x2, y2);

    /* Commit overlay; 0 = “apply now” (YOLOv5 sample pattern).
       If you want timestamp sync, pass vdo_frame_get_timestamp(buf). */
    if (!bbox_commit(bbox, 0)) {
      g_object_unref(buf);
      break;
    }

    /* Timestamp + enqueue */
    vdo_frame_set_timestamp(buf, g_get_monotonic_time()); // µs
    if (!vdo_stream_buffer_enqueue(st, buf, &err)) {
      g_clear_error(&err);
      g_object_unref(buf);   // still ours on failure
      break;
    }

    g_usleep(1000000 / (FPS ? FPS : 30));
  }

  /* Teardown */
  bbox_destroy(bbox);
  vdo_stream_stop(st);
  g_object_unref(st);
  syslog(LOG_INFO, "Producer + BBox finished.");
  closelog();
  return 0;
}
