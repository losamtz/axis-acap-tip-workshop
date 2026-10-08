/**
 * larod_preprocessing.c
 *
 * Minimal VDO + larod with preprocessing.
 * VDO delivers 640x360 frames. Preprocessing resizes to model resolution.
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
#include <stdint.h>
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
#define DEVICE_NAME  "axis-a8-dlpu-tflite"
#define MODEL_PATH   "/usr/local/packages/larod_preprocessing/model/model.tflite"
#define VDO_WIDTH    640
#define VDO_HEIGHT   360
#define VDO_FMT      VDO_FORMAT_YUV    /* NV12 input */
#define IMAGE_FIT    "scale"
#define NUM_BUFFERS  2
#define VDO_CHANNEL  1

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

/* Report the failing operation before another larod call can hide its error. */
static void log_larod_error(const char* operation, const larodError* error) {
    syslog(LOG_ERR, "%s: %s", operation, error ? error->msg : "No error details");
}

static int print_larod_devices(larodConnection* conn) {
    larodError* error = NULL;
    size_t num_devices = 0;
    const larodDevice** devices = larodListDevices(conn, &num_devices, &error);
    if (!devices || num_devices == 0) {
        log_larod_error("larodListDevices (no devices)", error);
        larodClearError(&error);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < num_devices; i++) {
        uint32_t instance;
        const char* name = larodGetDeviceName(devices[i], &error);
        if (!name) {
            log_larod_error("larodGetDeviceName", error);
            larodClearError(&error);
            return EXIT_FAILURE;
        }
        if (!larodGetDeviceInstance(devices[i], &instance, &error)) {
            log_larod_error("larodGetDeviceInstance", error);
            larodClearError(&error);
            return EXIT_FAILURE;
        }
        syslog(LOG_INFO, "Device %zu: %s (instance %u)", i, name, instance);
    }
    return EXIT_SUCCESS;
}

int main(void) {
    larodConnection* conn  = NULL;
    larodError*      error = NULL;

    /* Initialize owned resources so any failure can use the same cleanup path. */
    int exit_status = EXIT_FAILURE;
    int model_fd = -1;
    larodModel* model = NULL;
    larodModel* pp_model = NULL;
    larodMap* pp_map = NULL;
    larodTensor** tmp_in = NULL;
    larodTensor** out_tensors = NULL;
    larodTensor** pp_out = NULL;
    size_t tmp_num_in = 0;
    size_t num_out = 0;
    size_t pp_num_out = 0;
    void* out_data[2] = {NULL, NULL};
    size_t out_sizes[2] = {0, 0};
    larodTensor** vdo_tensors[NUM_BUFFERS] = {NULL};
    int tensor_fds[NUM_BUFFERS];
    int tracked_vdo_fds[NUM_BUFFERS];
    int64_t tracked_vdo_offsets[NUM_BUFFERS] = {0};
    size_t tracked_vdo_capacities[NUM_BUFFERS] = {0};
    larodJobRequest* pp_job = NULL;
    larodJobRequest* inf_job = NULL;
    GError* vdo_err = NULL;
    VdoStream* stream = NULL;
    VdoBuffer* buf = NULL;
    bool stream_started = false;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        tensor_fds[i] = -1;
        tracked_vdo_fds[i] = -1;
    }

    openlog("larod_preprocessing", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* ════════════════════════════════════════════
     *  1. CONNECT TO LAROD
     * ════════════════════════════════════════════ */
    if (!larodConnect(&conn, &error)) {
        syslog(LOG_ERR, "larodConnect: %s", error ? error->msg : "No error details");
        goto cleanup;
    }
    if (print_larod_devices(conn) != EXIT_SUCCESS) goto cleanup;
    /* ════════════════════════════════════════════
     *  2. LOAD INFERENCE MODEL + READ METADATA
     * ════════════════════════════════════════════ */
    model_fd = open(MODEL_PATH, O_RDONLY);
    if (model_fd < 0) {
        syslog(LOG_ERR, "open model: %s", strerror(errno));
        goto cleanup;
    }
    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    if (!device) {
        log_larod_error("larodGetDevice (inference)", error);
        goto cleanup;
    }
    model = larodLoadModel(conn, model_fd, device,
                                       LAROD_ACCESS_PRIVATE, "", NULL, &error);
    if (!model) {
        syslog(LOG_ERR, "larodLoadModel: %s", error ? error->msg : "No error details");
        goto cleanup;
    }
    syslog(LOG_INFO, "Model loaded on %s", DEVICE_NAME);

    /* Read model input dimensions + pitch from temporary tensors */
    tmp_in = larodAllocModelInputs(conn, model, 0, &tmp_num_in, NULL, &error);
    if (!tmp_in) {
        log_larod_error("larodAllocModelInputs", error);
        goto cleanup;
    }
    if (tmp_num_in != 1) {
        syslog(LOG_ERR, "Expected one model input, got %zu", tmp_num_in);
        goto cleanup;
    }
    const larodTensorDims* model_dims = larodGetTensorDims(tmp_in[0], &error);
    if (!model_dims) {
        log_larod_error("larodGetTensorDims", error);
        goto cleanup;
    }
    if (model_dims->len != 4) {
        syslog(LOG_ERR, "Expected four NHWC input dimensions, got %zu", (size_t)model_dims->len);
        goto cleanup;
    }
    unsigned int model_w     = model_dims->dims[2];   /* NHWC: [B,H,W,C] */
    unsigned int model_h     = model_dims->dims[1];
    const larodTensorPitches* model_pitches = larodGetTensorPitches(tmp_in[0], &error);
    if (!model_pitches) {
        log_larod_error("larodGetTensorPitches", error);
        goto cleanup;
    }
    if (model_pitches->len < 3) {
        syslog(LOG_ERR, "Model input has insufficient pitch metadata");
        goto cleanup;
    }
    unsigned int model_pitch = model_pitches->pitches[2];
    syslog(LOG_INFO, "Model input: %ux%u pitch=%u", model_w, model_h, model_pitch);
    if (!larodDestroyTensors(conn, &tmp_in, tmp_num_in, &error)) {
        log_larod_error("larodDestroyTensors (temporary inputs)", error);
        goto cleanup;
    }

    /* ════════════════════════════════════════════
     *  3. ALLOCATE INFERENCE OUTPUT TENSORS + MMAP
     * ════════════════════════════════════════════ */
    out_tensors = larodAllocModelOutputs(conn, model,
                                    LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                    &num_out, NULL, &error);
    if (!out_tensors) {
        log_larod_error("larodAllocModelOutputs (inference)", error);
        goto cleanup;
    }
    if (num_out != 2) {
        syslog(LOG_ERR, "Expected two classification outputs, got %zu", num_out);
        goto cleanup;
    }
    for (size_t i = 0; i < num_out; i++) {
        int fd = larodGetTensorFd(out_tensors[i], &error);
        if (fd < 0) {
            log_larod_error("larodGetTensorFd (inference output)", error);
            goto cleanup;
        }
        size_t sz = 0;
        if (!larodGetTensorFdSize(out_tensors[i], &sz, &error)) {
            log_larod_error("larodGetTensorFdSize (inference output)", error);
            goto cleanup;
        }
        if (sz == 0) {
            syslog(LOG_ERR, "Inference output %zu has an empty buffer", i);
            goto cleanup;
        }
        out_data[i] = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
        if (out_data[i] == MAP_FAILED) {
            out_data[i] = NULL;
            syslog(LOG_ERR, "mmap output %zu: %s", i, strerror(errno));
            goto cleanup;
        }
        out_sizes[i] = sz;
    }

    /* ════════════════════════════════════════════
     *  4. CREATE VDO STREAM (blocking, 640x360)
     * ════════════════════════════════════════════ */
    VdoMap* settings = vdo_map_new();
    if (!settings) {
        syslog(LOG_ERR, "vdo_map_new failed");
        goto cleanup;
    }
    vdo_map_set_uint32(settings, "channel", VDO_CHANNEL);
    vdo_map_set_uint32(settings, "format", VDO_FMT);
    vdo_map_set_uint32(settings, "buffer.count", NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate", 2.0);
    vdo_map_set_string(settings, "image.fit", IMAGE_FIT);
    VdoPair32u res = { .w = VDO_WIDTH, .h = VDO_HEIGHT };
    vdo_map_set_pair32u(settings, "resolution", res);

    stream = vdo_stream_new(settings, NULL, &vdo_err);
    g_object_unref(settings);
    if (!stream) {
        syslog(LOG_ERR, "vdo_stream_new: %s", vdo_err ? vdo_err->message : "No error details");
        goto cleanup;
    }

    /* Read back actual VDO stream properties */
    VdoMap* info = vdo_stream_get_info(stream, &vdo_err);
    if (!info) {
        syslog(LOG_ERR, "vdo_stream_get_info: %s",
               vdo_err ? vdo_err->message : "No error details");
        goto cleanup;
    }
    unsigned int vdo_w     = vdo_map_get_uint32(info, "width", 0);
    unsigned int vdo_h     = vdo_map_get_uint32(info, "height", 0);
    unsigned int vdo_pitch = vdo_map_get_uint32(info, "pitch", 0);
    VdoFormat    vdo_fmt   = vdo_map_get_uint32(info, "format", 0);
    const char* buffer_type = vdo_map_get_string(info, "buffer.type", NULL, "unknown");
    bool convert_vmem = g_strcmp0(buffer_type, "vmem") == 0;
    bool is_dmabuf = g_strcmp0(buffer_type, "dmabuf") == 0;
    syslog(LOG_INFO, "VDO buffer.type=%s", buffer_type);
    if (!convert_vmem && !is_dmabuf) {
        syslog(LOG_ERR, "Unsupported VDO buffer type '%s': expected vmem or dmabuf",
               buffer_type);
        g_object_unref(info);
        goto cleanup;
    }
    g_object_unref(info);
    if (!vdo_w || !vdo_h || !vdo_pitch || !model_w || !model_h || !model_pitch) {
        syslog(LOG_ERR, "VDO or model dimensions/pitch are zero");
        goto cleanup;
    }

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

    if (need_pp) {
        const char* in_fmt = (vdo_fmt == VDO_FORMAT_YUV) ? "nv12" : "rgb-interleaved";

        pp_map = larodCreateMap(&error);
        if (!pp_map) {
            log_larod_error("larodCreateMap (preprocessing)", error);
            goto cleanup;
        }
        if (!larodMapSetStr(pp_map, "image.input.format",     in_fmt, &error)) {
            log_larod_error("larodMapSetStr (image.input.format)", error);
            goto cleanup;
        }
        if (!larodMapSetIntArr2(pp_map, "image.input.size",   vdo_w, vdo_h, &error)) {
            log_larod_error("larodMapSetIntArr2 (image.input.size)", error);
            goto cleanup;
        }
        if (!larodMapSetInt(pp_map, "image.input.row-pitch",  vdo_pitch, &error)) {
            log_larod_error("larodMapSetInt (image.input.row-pitch)", error);
            goto cleanup;
        }
        if (!larodMapSetStr(pp_map, "image.output.format",    "rgb-interleaved", &error)) {
            log_larod_error("larodMapSetStr (image.output.format)", error);
            goto cleanup;
        }
        if (!larodMapSetIntArr2(pp_map, "image.output.size",  model_w, model_h, &error)) {
            log_larod_error("larodMapSetIntArr2 (image.output.size)", error);
            goto cleanup;
        }
        if (!larodMapSetInt(pp_map, "image.output.row-pitch", model_pitch, &error)) {
            log_larod_error("larodMapSetInt (image.output.row-pitch)", error);
            goto cleanup;
        }

        const larodDevice* pp_dev = larodGetDevice(conn, "cpu-proc", 0, &error);
        if (!pp_dev) {
            log_larod_error("larodGetDevice (cpu-proc)", error);
            goto cleanup;
        }
        pp_model = larodLoadModel(conn, -1, pp_dev, LAROD_ACCESS_PRIVATE, "", pp_map, &error);
        if (!pp_model) {
            syslog(LOG_ERR, "PP larodLoadModel: %s", error ? error->msg : "No error details");
            goto cleanup;
        }
        larodDestroyMap(&pp_map);

        pp_out = larodAllocModelOutputs(conn, pp_model,
                     LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                     &pp_num_out, NULL, &error);
        if (!pp_out) {
            log_larod_error("larodAllocModelOutputs (preprocessing)", error);
            goto cleanup;
        }

        if (pp_num_out != 1) {
            syslog(LOG_ERR, "Expected one preprocessing output, got %zu", pp_num_out);
            goto cleanup;
        }
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
            goto cleanup;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        vdo_tensors[i] = larodCreateTensors(1, &error);
        if (!vdo_tensors[i]) {
            syslog(LOG_ERR, "larodCreateTensors[%d]: %s", i, error ? error->msg : "No error details");
            goto cleanup;
        }
        larodTensor* t = vdo_tensors[i][0];
        if (!larodSetTensorDataType(t, LAROD_TENSOR_DATA_TYPE_UINT8, &error)) {
            log_larod_error("larodSetTensorDataType", error);
            goto cleanup;
        }
        if (!larodSetTensorLayout(t, vdo_layout, &error)) {
            log_larod_error("larodSetTensorLayout", error);
            goto cleanup;
        }
        if (!larodBuildTensorDims(t, vdo_layout, vdo_w, vdo_h, 3, &error)) {
            log_larod_error("larodBuildTensorDims", error);
            goto cleanup;
        }
        if (!larodBuildTensorPitches(t, vdo_layout, vdo_pitch, vdo_h, 3, &error)) {
            log_larod_error("larodBuildTensorPitches", error);
            goto cleanup;
        }
        if (!larodSetTensorFdProps(t, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error)) {
            log_larod_error("larodSetTensorFdProps", error);
            goto cleanup;
        }
    }
    syslog(LOG_INFO, "Created %d input tensors (%s %ux%u pitch=%u)",
           NUM_BUFFERS, layout_str, vdo_w, vdo_h, vdo_pitch);

    /* ════════════════════════════════════════════
     *  8. START VDO + MAIN LOOP
     * ════════════════════════════════════════════ */
    if (!vdo_stream_start(stream, &vdo_err)) {
        syslog(LOG_ERR, "vdo_stream_start: %s",
               vdo_err ? vdo_err->message : "No error details");
        goto cleanup;
    }
    stream_started = true;

    syslog(LOG_INFO, "Entering inference loop");

    while (running) {
        /* ── Get frame (blocks) ── */
        buf = vdo_stream_get_buffer(stream, &vdo_err);
        if (!buf) {
            if (!running) break;
            syslog(LOG_ERR, "vdo_stream_get_buffer: %s",
                   vdo_err ? vdo_err->message : "No error details");
            goto cleanup;
        }

        int vdo_fd = vdo_buffer_get_fd(buf);
        if (vdo_fd < 0) {
            syslog(LOG_ERR, "vdo_buffer_get_fd returned an invalid descriptor");
            goto cleanup;
        }

        int64_t source_offset = vdo_buffer_get_offset(buf);
        size_t cap = vdo_buffer_get_capacity(buf);
        if (source_offset < 0 || cap == 0) {
            syslog(LOG_ERR, "Invalid VDO buffer offset/capacity");
            goto cleanup;
        }

        /* A VMEM fd can contain multiple buffers at different offsets. */
        int slot = -1;
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (tracked_vdo_fds[i] == vdo_fd &&
                tracked_vdo_offsets[i] == source_offset &&
                tracked_vdo_capacities[i] == cap) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (tracked_vdo_fds[i] == -1) { slot = i; break; }
            }
            if (slot == -1) {
                syslog(LOG_ERR, "No free tensor slot for VDO fd %d", vdo_fd);
                goto cleanup;
            }
            int64_t offset = source_offset;
            int tensor_fd;
            if (convert_vmem) {
                /* Export this VMEM buffer as a DMA-BUF. The exported buffer
                 * starts at zero; the original VMEM offset no longer applies. */
                tensor_fd = larodConvertVmemFdToDmabuf(vdo_fd, source_offset, &error);
                if (tensor_fd == LAROD_INVALID_FD) {
                    log_larod_error("larodConvertVmemFdToDmabuf", error);
                    goto cleanup;
                }
                offset = 0;
            } else {
                /* VDO owns its fd. Keep our own descriptor until cleanup. */
                tensor_fd = dup(vdo_fd);
                if (tensor_fd < 0) {
                    syslog(LOG_ERR, "dup VDO fd (slot %d): %s", slot, strerror(errno));
                    goto cleanup;
                }
            }
            /* Both paths produce an owned descriptor, closed during cleanup. */
            tensor_fds[slot] = tensor_fd;
            /* VDO capacity counts bytes from the buffer's start. Larod's fd
             * size counts from the beginning of the file descriptor, so it
             * must also include the bytes before this buffer. */
            if ((uint64_t)offset > SIZE_MAX - cap) {
                syslog(LOG_ERR, "VDO buffer offset + capacity overflows size_t");
                goto cleanup;
            }
            size_t fd_size = (size_t)offset + cap;
            larodTensor* t = vdo_tensors[slot][0];
            if (!larodSetTensorFd(t, tensor_fd, &error)) {
                log_larod_error("larodSetTensorFd", error);
                goto cleanup;
            }
            if (!larodSetTensorFdOffset(t, offset, &error)) {
                log_larod_error("larodSetTensorFdOffset", error);
                goto cleanup;
            }
            if (!larodSetTensorFdSize(t, fd_size, &error)) {
                log_larod_error("larodSetTensorFdSize", error);
                goto cleanup;
            }
            if (!larodTrackTensor(conn, t, &error)) {
                log_larod_error("larodTrackTensor", error);
                goto cleanup;
            }

            tracked_vdo_fds[slot] = vdo_fd;
            tracked_vdo_offsets[slot] = source_offset;
            tracked_vdo_capacities[slot] = cap;
            syslog(LOG_INFO,
                   "Tracked buffer slot %d (%s): VDO offset=%zu tensor offset=%zu "
                   "capacity=%zu fd size=%zu",
                   slot, convert_vmem ? "VMEM -> DMA-BUF" : "DMA-BUF",
                   (size_t)source_offset, (size_t)offset, cap, fd_size);
        }

        if (need_pp) {
            /* ── Preprocess: VDO frame → model resolution ── */
            if (!pp_job) {
                pp_job = larodCreateJobRequest(pp_model,
                             vdo_tensors[slot], 1,
                             pp_out, pp_num_out,
                             NULL, &error);
                if (!pp_job) {
                    log_larod_error("larodCreateJobRequest (preprocessing)", error);
                    goto cleanup;
                }
            } else {
                if (!larodSetJobRequestInputs(pp_job, vdo_tensors[slot], 1, &error)) {
                    log_larod_error("larodSetJobRequestInputs", error);
                    goto cleanup;
                }
            }
            if (!larodRunJob(conn, pp_job, &error)) {
                syslog(LOG_ERR, "PP failed: %s", error ? error->msg : "No error details");
                goto cleanup;
            }

            /* ── Infer: PP output → model ── */
            if (!inf_job) {
                inf_job = larodCreateJobRequest(model,
                              pp_out, pp_num_out,
                              out_tensors, num_out,
                              NULL, &error);
                if (!inf_job) {
                    log_larod_error("larodCreateJobRequest (inference)", error);
                    goto cleanup;
                }
            }
        } else {
            /* ── Infer directly: VDO frame → model ── */
            if (!inf_job) {
                inf_job = larodCreateJobRequest(model,
                              vdo_tensors[slot], 1,
                              out_tensors, num_out,
                              NULL, &error);
                if (!inf_job) {
                    log_larod_error("larodCreateJobRequest (inference)", error);
                    goto cleanup;
                }
            } else {
                if (!larodSetJobRequestInputs(inf_job, vdo_tensors[slot], 1, &error)) {
                    log_larod_error("larodSetJobRequestInputs", error);
                    goto cleanup;
                }
            }
        }

        if (!larodRunJob(conn, inf_job, &error)) {
            syslog(LOG_ERR, "Inference failed: %s", error ? error->msg : "No error details");
            goto cleanup;
        }

        /* ── Read results ── */
        uint8_t* person = (uint8_t*)out_data[0];
        uint8_t* car    = (uint8_t*)out_data[1];
        syslog(LOG_INFO, "Person: %.1f%% — Car: %.1f%%",
               *person / 2.55f, *car / 2.55f);

        vdo_stream_buffer_unref(stream, &buf, &vdo_err);
        if (vdo_err) {
            syslog(LOG_ERR, "vdo_stream_buffer_unref: %s", vdo_err->message);
            goto cleanup;
        }
    }

    /* ════════════════════════════════════════════
     *  9. CLEANUP
     * ════════════════════════════════════════════ */
    exit_status = EXIT_SUCCESS;

cleanup:
    /* The original error was logged at its source. Cleanup must not overwrite it. */
    larodClearError(&error);
    g_clear_error(&vdo_err);
    if (pp_job) larodDestroyJobRequest(&pp_job);
    if (inf_job) larodDestroyJobRequest(&inf_job);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (vdo_tensors[i]) larodDestroyTensors(conn, &vdo_tensors[i], 1, NULL);
        if (tensor_fds[i] >= 0) close(tensor_fds[i]);
    }
    if (buf) vdo_stream_buffer_unref(stream, &buf, NULL);
    if (stream_started) vdo_stream_stop(stream);
    if (stream) g_object_unref(stream);
    for (size_t i = 0; i < 2; i++) {
        if (out_data[i]) munmap(out_data[i], out_sizes[i]);
    }
    if (tmp_in) larodDestroyTensors(conn, &tmp_in, tmp_num_in, NULL);
    if (pp_out) larodDestroyTensors(conn, &pp_out, pp_num_out, NULL);
    if (out_tensors) larodDestroyTensors(conn, &out_tensors, num_out, NULL);
    if (pp_map) larodDestroyMap(&pp_map);
    if (pp_model) larodDestroyModel(&pp_model);
    if (model) larodDestroyModel(&model);
    if (conn) larodDisconnect(&conn, NULL);
    if (model_fd >= 0) close(model_fd);

    syslog(LOG_INFO, "Stopped (%s)", exit_status == EXIT_SUCCESS ? "normal shutdown" : "error");
    closelog();
    return exit_status;
}
