/**
 * larod_preprocessing.c
 *
 * Minimal VDO + larod with preprocessing.
 * VDO delivers 720p. Preprocessing resizes to model resolution.
 * Blocking VDO, single file.
 *
 * Input tensors are ALWAYS created manually with larodCreateTensors,
 * matching the original vdo-larod example pattern.
 * They describe the VDO frame layout and are used as input to either:
 *   - the preprocessing model (if VDO size/format ≠ model)
 *   - the inference model directly (if VDO matches model exactly)
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include "larod.h"
#include "vdo-buffer.h"
#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>

/* ── Configuration ── */
#define DEVICE_NAME  "a9-dlpu-tflite"
#define MODEL_PATH   "/usr/local/packages/larod_preprocessing/model/model.tflite"
#define VDO_WIDTH    640
#define VDO_HEIGHT   360
#define VDO_FMT      VDO_FORMAT_YUV    /* or VDO_FORMAT_YUV for NV12 */
#define IMAGE_FIT    "scale"
#define NUM_BUFFERS  2
#define VDO_CHANNEL  2

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

int main(void) {
    larodConnection* conn  = NULL;
    larodError*      error = NULL;

    openlog("larod_preprocessing", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* ════════════════════════════════════════════
     *  1. CONNECT TO LAROD
     * ════════════════════════════════════════════ */
    if (!larodConnect(&conn, &error)) {
        syslog(LOG_ERR, "larodConnect: %s", error->msg);
        return EXIT_FAILURE;
    }

    /* ════════════════════════════════════════════
     *  2. LOAD INFERENCE MODEL + READ METADATA
     * ════════════════════════════════════════════ */
    int model_fd = open(MODEL_PATH, O_RDONLY);
    if (model_fd < 0) {
        syslog(LOG_ERR, "open model: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    larodModel* model = larodLoadModel(conn, model_fd, device,
                                       LAROD_ACCESS_PRIVATE, "", NULL, &error);
    if (!model) {
        syslog(LOG_ERR, "larodLoadModel: %s", error->msg);
        return EXIT_FAILURE;
    }
    syslog(LOG_INFO, "Model loaded on %s", DEVICE_NAME);

    /* Read model input dimensions + pitch from temporary tensors */
    size_t tmp_num_in = 0;
    larodTensor** tmp_in = larodAllocModelInputs(conn, model, 0, &tmp_num_in, NULL, &error);
    const larodTensorDims* model_dims = larodGetTensorDims(tmp_in[0], &error);
    unsigned int model_w     = model_dims->dims[2];   /* NHWC: [B,H,W,C] */
    unsigned int model_h     = model_dims->dims[1];
    const larodTensorPitches* model_pitches = larodGetTensorPitches(tmp_in[0], &error);
    unsigned int model_pitch = model_pitches->pitches[2];
    syslog(LOG_INFO, "Model input: %ux%u pitch=%u", model_w, model_h, model_pitch);
    larodDestroyTensors(conn, &tmp_in, tmp_num_in, &error);

    /* ════════════════════════════════════════════
     *  3. ALLOCATE INFERENCE OUTPUT TENSORS + MMAP
     * ════════════════════════════════════════════ */
    size_t num_out = 0;
    larodTensor** out_tensors = larodAllocModelOutputs(conn, model,
                                    LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                    &num_out, NULL, &error);
    void* out_data[2] = {NULL, NULL};
    for (size_t i = 0; i < num_out && i < 2; i++) {
        int fd = larodGetTensorFd(out_tensors[i], &error);
        size_t sz = 0;
        larodGetTensorFdSize(out_tensors[i], &sz, &error);
        out_data[i] = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    }

    /* ════════════════════════════════════════════
     *  4. CREATE VDO STREAM (blocking, 720p)
     * ════════════════════════════════════════════ */
    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "channel", VDO_CHANNEL);
    vdo_map_set_uint32(settings, "format", VDO_FMT);
    vdo_map_set_uint32(settings, "buffer.count", NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate", 2.0);
    vdo_map_set_string(settings, "image.fit", IMAGE_FIT);
    VdoPair32u res = { .w = VDO_WIDTH, .h = VDO_HEIGHT };
    vdo_map_set_pair32u(settings, "resolution", res);

    GError* vdo_err = NULL;
    VdoStream* stream = vdo_stream_new(settings, NULL, &vdo_err);
    g_object_unref(settings);
    if (!stream) {
        syslog(LOG_ERR, "vdo_stream_new: %s", vdo_err->message);
        return EXIT_FAILURE;
    }

    /* Read back actual VDO stream properties */
    VdoMap* info = vdo_stream_get_info(stream, &vdo_err);
    unsigned int vdo_w     = vdo_map_get_uint32(info, "width", 0);
    unsigned int vdo_h     = vdo_map_get_uint32(info, "height", 0);
    unsigned int vdo_pitch = vdo_map_get_uint32(info, "pitch", 0);
    VdoFormat    vdo_fmt   = vdo_map_get_uint32(info, "format", 0);
    g_object_unref(info);

    syslog(LOG_INFO, "VDO stream: %ux%u pitch=%u fmt=%u", vdo_w, vdo_h, vdo_pitch, vdo_fmt);

    /* ════════════════════════════════════════════
     *  5. DECIDE IF PREPROCESSING IS NEEDED
     *
     *  Same logic as original model.c:
     *    if format differs OR size differs → preprocess
     * ════════════════════════════════════════════ */
    bool need_pp = (vdo_fmt != VDO_FORMAT_RGB ||
                    vdo_w != model_w ||
                    vdo_h != model_h);

    syslog(LOG_INFO, "Preprocessing: %s", need_pp ? "YES" : "NO");

    /* ════════════════════════════════════════════
     *  6. SET UP PREPROCESSING MODEL (if needed)
     * ════════════════════════════════════════════ */
    larodModel*   pp_model = NULL;
    larodTensor** pp_out   = NULL;
    size_t pp_num_out      = 0;

    if (need_pp) {
        const char* in_fmt = (vdo_fmt == VDO_FORMAT_YUV) ? "nv12" : "rgb-interleaved";

        larodMap* pp_map = larodCreateMap(&error);
        larodMapSetStr(pp_map, "image.input.format",     in_fmt, &error);
        larodMapSetIntArr2(pp_map, "image.input.size",   vdo_w, vdo_h, &error);
        larodMapSetInt(pp_map, "image.input.row-pitch",  vdo_pitch, &error);
        larodMapSetStr(pp_map, "image.output.format",    "rgb-interleaved", &error);
        larodMapSetIntArr2(pp_map, "image.output.size",  model_w, model_h, &error);
        larodMapSetInt(pp_map, "image.output.row-pitch", model_pitch, &error);

        const larodDevice* pp_dev = larodGetDevice(conn, "cpu-proc", 0, &error);
        pp_model = larodLoadModel(conn, -1, pp_dev, LAROD_ACCESS_PRIVATE, "", pp_map, &error);
        if (!pp_model) {
            syslog(LOG_ERR, "PP larodLoadModel: %s", error->msg);
            return EXIT_FAILURE;
        }
        larodDestroyMap(&pp_map);

        pp_out = larodAllocModelOutputs(conn, pp_model,
                     LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                     &pp_num_out, NULL, &error);

        syslog(LOG_INFO, "PP: %s %ux%u → RGB %ux%u", in_fmt, vdo_w, vdo_h, model_w, model_h);
    }

    /* ════════════════════════════════════════════
     *  7. CREATE INPUT TENSORS (always manual)
     *
     *  From the original model.c:
     *  "Create one input tensor for each buffer
     *   from the img provider. These input tensors
     *   will be used either:
     *     1. as input to preprocessing
     *     2. as input to the inference if
     *        preprocessing is not needed"
     *
     *  The tensor describes the VDO frame layout.
     *  larodCreateTensors is used in ALL cases —
     *  even when VDO delivers RGB that matches
     *  the model format.
     * ════════════════════════════════════════════ */

    /* Pick layout based on what VDO delivers */
    larodTensorLayout vdo_layout;
    const char* layout_str;
    switch (vdo_fmt) {
        case VDO_FORMAT_YUV:
            vdo_layout = LAROD_TENSOR_LAYOUT_420SP;
            layout_str = "420SP (NV12)";
            break;
        case VDO_FORMAT_RGB:
            vdo_layout = LAROD_TENSOR_LAYOUT_NHWC;
            layout_str = "NHWC (RGB)";
            break;
        case VDO_FORMAT_PLANAR_RGB:
            vdo_layout = LAROD_TENSOR_LAYOUT_NCHW;
            layout_str = "NCHW (planar RGB)";
            break;
        default:
            syslog(LOG_ERR, "Unsupported VDO format %u", vdo_fmt);
            return EXIT_FAILURE;
    }

    larodTensor** vdo_tensors[NUM_BUFFERS];
    int duped_fds[NUM_BUFFERS];
    int tracked_vdo_fds[NUM_BUFFERS];

    for (int i = 0; i < NUM_BUFFERS; i++) {
        duped_fds[i]       = -1;
        tracked_vdo_fds[i] = -1;

        vdo_tensors[i] = larodCreateTensors(1, &error);
        if (!vdo_tensors[i]) {
            syslog(LOG_ERR, "larodCreateTensors[%d]: %s", i, error->msg);
            return EXIT_FAILURE;
        }
        larodTensor* t = vdo_tensors[i][0];
        larodSetTensorDataType(t, LAROD_TENSOR_DATA_TYPE_UINT8, &error);
        larodSetTensorLayout(t, vdo_layout, &error);
        larodBuildTensorDims(t, vdo_layout, vdo_w, vdo_h, 3, &error);
        larodBuildTensorPitches(t, vdo_layout, vdo_pitch, vdo_h, 3, &error);
        larodSetTensorFdProps(t, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
    }
    syslog(LOG_INFO, "Created %d input tensors (%s %ux%u pitch=%u)",
           NUM_BUFFERS, layout_str, vdo_w, vdo_h, vdo_pitch);

    /* ════════════════════════════════════════════
     *  8. START VDO + MAIN LOOP
     * ════════════════════════════════════════════ */
    vdo_stream_start(stream, &vdo_err);

    larodJobRequest* pp_job  = NULL;
    larodJobRequest* inf_job = NULL;

    syslog(LOG_INFO, "Entering inference loop");

    while (running) {
        /* ── Get frame (blocks) ── */
        VdoBuffer* buf = vdo_stream_get_buffer(stream, &vdo_err);
        if (!buf) continue;

        int vdo_fd = vdo_buffer_get_fd(buf);

        /* ── Track buffer (once per VDO buffer fd) ── */
        int slot = -1;
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (tracked_vdo_fds[i] == vdo_fd) { slot = i; break; }
        }
        if (slot == -1) {
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (tracked_vdo_fds[i] == -1) { slot = i; break; }
            }
            int64_t offset = vdo_buffer_get_offset(buf);
            size_t cap     = vdo_buffer_get_capacity(buf);
            int duped      = dup(vdo_fd);

            larodTensor* t = vdo_tensors[slot][0];
            larodSetTensorFd(t, duped, &error);
            larodSetTensorFdOffset(t, offset, &error);
            larodSetTensorFdSize(t, cap, &error);
            larodTrackTensor(conn, t, &error);

            tracked_vdo_fds[slot] = vdo_fd;
            duped_fds[slot] = duped;
            syslog(LOG_INFO, "Tracked buffer slot %d", slot);
        }

        if (need_pp) {
            /* ── Preprocess: VDO frame → model resolution ── */
            if (!pp_job) {
                pp_job = larodCreateJobRequest(pp_model,
                             vdo_tensors[slot], 1,
                             pp_out, pp_num_out,
                             NULL, &error);
            } else {
                larodSetJobRequestInputs(pp_job, vdo_tensors[slot], 1, &error);
            }
            if (!larodRunJob(conn, pp_job, &error)) {
                syslog(LOG_ERR, "PP failed: %s", error->msg);
                larodClearError(&error);
                vdo_stream_buffer_unref(stream, &buf, &vdo_err);
                continue;
            }

            /* ── Infer: PP output → model ── */
            if (!inf_job) {
                inf_job = larodCreateJobRequest(model,
                              pp_out, pp_num_out,
                              out_tensors, num_out,
                              NULL, &error);
            }
        } else {
            /* ── Infer directly: VDO frame → model ── */
            if (!inf_job) {
                inf_job = larodCreateJobRequest(model,
                              vdo_tensors[slot], 1,
                              out_tensors, num_out,
                              NULL, &error);
            } else {
                larodSetJobRequestInputs(inf_job, vdo_tensors[slot], 1, &error);
            }
        }

        if (!larodRunJob(conn, inf_job, &error)) {
            syslog(LOG_ERR, "Inference failed: %s", error->msg);
            larodClearError(&error);
            vdo_stream_buffer_unref(stream, &buf, &vdo_err);
            continue;
        }

        /* ── Read results ── */
        uint8_t* person = (uint8_t*)out_data[0];
        uint8_t* car    = (uint8_t*)out_data[1];
        syslog(LOG_INFO, "Person: %.1f%% — Car: %.1f%%",
               *person / 2.55f, *car / 2.55f);

        vdo_stream_buffer_unref(stream, &buf, &vdo_err);
    }

    /* ════════════════════════════════════════════
     *  9. CLEANUP
     * ════════════════════════════════════════════ */
    larodDestroyJobRequest(&pp_job);
    larodDestroyJobRequest(&inf_job);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (vdo_tensors[i]) larodDestroyTensors(conn, &vdo_tensors[i], 1, &error);
        if (duped_fds[i] >= 0) close(duped_fds[i]);
    }
    if (pp_out) larodDestroyTensors(conn, &pp_out, pp_num_out, &error);
    larodDestroyTensors(conn, &out_tensors, num_out, &error);
    if (pp_model) larodDestroyModel(&pp_model);
    larodDestroyModel(&model);
    larodDisconnect(&conn, &error);
    vdo_stream_stop(stream);
    g_object_unref(stream);
    close(model_fd);

    syslog(LOG_INFO, "Done");
    closelog();
    return EXIT_SUCCESS;
}