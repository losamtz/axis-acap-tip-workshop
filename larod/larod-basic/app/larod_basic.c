/**
 * larod_basic.c
 *
 * The simplest possible VDO + larod application.
 * Blocking VDO, no preprocessing, tracked tensors, no poll().
 *
 * Only works on backends that accept RGB directly (e.g. a9-dlpu-tflite).
 * VDO delivers RGB at the model's resolution, frames go straight to inference.
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


/* This application is restricted to work with artpec9 as the code only checks for RGB fmt otherwise panics. If A8 is needed,
it would need to fallback to YUV if RGB is not supported and preprocessing should also be implemented */

#define DEVICE_NAME  "a9-dlpu-tflite" /* or "axis-a8-dlpu-tflite"   "cpu-tflite" "a9-dlpu-tflite" or "armnn-cpu-tflite" */
#define MODEL_PATH   "/usr/local/packages/larod_basic/model/model.tflite"

#define PANIC(fmt, ...)                                 \
    do {                                                \
        syslog(LOG_ERR, "FATAL: " fmt, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);                             \
    } while (0)


static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }


/* ══════════════════════════════════════════════
 *
 *  STEP 1 — CONNECT TO LAROD
 *
 * ══════════════════════════════════════════════ */
static larodConnection* larod_connect(void) {
    larodConnection* conn = NULL;
    larodError* error = NULL;

    if(!larodConnect(&conn, &error)) {
        PANIC("larodConnect: %s", error->msg);
    }
    syslog(LOG_INFO, "Connected to larod successfully");
    return conn;
}
// A test to list all backends and their instances (in case there is more than one DLPU).
// This is irrelevant in this example but shows the possibility to read it.

static void print_larod_devices(larodConnection* conn) {

    larodError* error = NULL;
    size_t num_devices = 0;

    const larodDevice** devices = larodListDevices(conn, &num_devices, &error);
    if (num_devices == 0) {
        PANIC("larodListDevices: %s", error->msg);
    }
    for (size_t i = 0; i < num_devices; i++) {
        uint32_t instance;

        const char *name =
            larodGetDeviceName(devices[i], &error);

        larodGetDeviceInstance(devices[i], &instance, &error);

        printf("%zu: %s (instance %u)\n",
            i, name, instance);
    }
}
/* ══════════════════════════════════════════════
 *
 *  STEP 2 — LOAD THE INFERENCE MODEL
 *
 *  Opens the .tflite file, selects the device
 *  (e.g. "a9-dlpu-tflite"), and loads the model.
 *
 * ══════════════════════════════════════════════ */
static larodModel* load_inference_model(larodConnection* conn, int* model_fd_out) {
    larodError*      error = NULL;

    int model_fd = open(MODEL_PATH, O_RDONLY);

    *model_fd_out = model_fd;

    // List devices for multiple sensors, if needed
    print_larod_devices(conn);


    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    larodModel* model = larodLoadModel(conn, model_fd, device,
                                       LAROD_ACCESS_PRIVATE, "", NULL, &error);
    if (!model) {
        PANIC("larodLoadModel: %s", error->msg);
    }
    syslog(LOG_INFO, "Model loaded successfully with backend %s", DEVICE_NAME);
    return model;
}

int main(void) {
    larodConnection* conn  = NULL;
    larodError*      error = NULL;
    int model_fd = -1;

    openlog("larod_basic", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* ── 1. Connect to larod ── */
    conn = larod_connect();

    /* ── 2. Load model ── */
    larodModel* model = load_inference_model(conn, &model_fd);

    /* ── 3. Get model input size ── */
    size_t num_in = 0;
    larodTensor** tmp_in = larodAllocModelInputs(conn, model, 0, &num_in, NULL, &error);
    if (!tmp_in || num_in != 1) {
        PANIC("larodAllocModelInputs: expected one input (%s)",
              error ? error->msg : "unexpected input count");
    }
    const larodTensorDims* dims = larodGetTensorDims(tmp_in[0], &error);
    if (!dims || dims->len != 4) {
        PANIC("Model input requires four NHWC dimensions (%s)",
              error ? error->msg : "unexpected dimensions");
    }
    unsigned int h = dims->dims[1];
    unsigned int w = dims->dims[2];


    const larodTensorPitches* pitches = larodGetTensorPitches(tmp_in[0], &error);
    if (!pitches || pitches->len < 3) {
        PANIC("Invalid model pitch metadata: %s", error ? error->msg : "missing pitches");
    }
    unsigned int model_pitch = pitches->pitches[2];
    if (!w || !h || !model_pitch) PANIC("Model dimensions/pitch must be nonzero");

    syslog(LOG_INFO, "Model input: %ux%u pitch=%u", w, h, model_pitch);
    // Destroy it as it is temporary
    larodDestroyTensors(conn, &tmp_in, num_in, &error);

    /* ── 4. Allocate output tensors + mmap ── */
    size_t num_out = 0;
    larodTensor** out_tensors = larodAllocModelOutputs(conn, model,
                                    LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                    &num_out, NULL, &error);

    if (!out_tensors || num_out != 2) {
        PANIC("Expected two classification outputs: %s",
              error ? error->msg : "unexpected output count");
    }
    void* out_data[2] = {NULL, NULL};
    size_t out_sizes[2] = {0, 0};

    for (size_t i = 0; i < num_out && i < 2; i++) {

        int fd = larodGetTensorFd(out_tensors[i], &error);

        size_t sz = 0;
        if (!larodGetTensorFdSize(out_tensors[i], &sz, &error)) {
            PANIC("larodGetTensorFdSize: %s", error ? error->msg : "No error details");
        }
        if (fd < 0 || sz == 0) PANIC("Invalid inference output fd/size");
        out_sizes[i] = sz;
        out_data[i] = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);

        if (out_data[i] == MAP_FAILED) {
            PANIC("mmap output[%zu]: %s", i, strerror(errno));
        }
    }

    /* ── 5. Create VDO stream (blocking, RGB, model resolution) ── */
    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "channel", 3); // Using channel 2
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_RGB);
    vdo_map_set_uint32(settings, "buffer.count", 2);
    vdo_map_set_double(settings, "framerate", 30.0);
    vdo_map_set_string(settings, "image.fit", "scale");
    VdoPair32u res = { .w = w, .h = h };
    vdo_map_set_pair32u(settings, "resolution", res);
    /* socket.blocking defaults to true — vdo_stream_get_buffer will block */

    GError* vdo_err = NULL;
    VdoStream* stream = vdo_stream_new(settings, NULL, &vdo_err);
    g_object_unref(settings);
    if (!stream) {
        PANIC("vdo_stream_new: %s", vdo_err->message);
    }

    VdoMap* info = vdo_stream_get_info(stream, &vdo_err);
    if (!info) {
        PANIC("vdo_stream_get_info: %s", vdo_err->message);
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
        PANIC("Unsupported VDO buffer type '%s': expected vmem or dmabuf", buffer_type);
    }
    g_object_unref(info);

    if (vdo_fmt != VDO_FORMAT_RGB || vdo_w != w || vdo_h != h) {
        PANIC("VDO stream does not match model input: got fmt=%u %ux%u, expected RGB %ux%u",
              vdo_fmt, vdo_w, vdo_h, w, h);
    }

    if (vdo_pitch != model_pitch) {
        PANIC("VDO pitch %u differs from model pitch %u; direct RGB input requires a match",
              vdo_pitch, model_pitch);
    }
    if (!vdo_stream_start(stream, &vdo_err)) {
        PANIC("vdo_stream_start: %s", vdo_err ? vdo_err->message : "No error details");
    }

    syslog(LOG_INFO, "VDO stream started (blocking, RGB %ux%u pitch=%u)",
           vdo_w, vdo_h, vdo_pitch);

    /* ── 6. Allocate input tensors (one per buffer) ── */
    larodTensor** in_tensors[2] = {NULL, NULL};
    int tensor_fds[2] = {-1, -1};
    int tracked_vdo_fds[2] = {-1, -1};
    int64_t tracked_offsets[2] = {0};
    size_t tracked_capacities[2] = {0};

    for(int i = 0; i < 2; i++) {
        tensor_fds[i]       = -1;
        tracked_vdo_fds[i] = -1;

        in_tensors[i] = larodCreateTensors(1, &error);
        if (!in_tensors[i]) {
            PANIC("larodCreateTensors: %s", error->msg);
        }
        larodTensor* t = in_tensors[i][0];
        if (!larodSetTensorDataType(t, LAROD_TENSOR_DATA_TYPE_UINT8, &error)) {
            PANIC("larodSetTensorDataType: %s", error ? error->msg : "No error details");
        }
        if (!larodSetTensorLayout(t, LAROD_TENSOR_LAYOUT_NHWC, &error)) {
            PANIC("larodSetTensorLayout: %s", error ? error->msg : "No error details");
        }
        if (!larodBuildTensorDims(t, LAROD_TENSOR_LAYOUT_NHWC, vdo_w, vdo_h, 3, &error)) {
            PANIC("larodBuildTensorDims: %s", error ? error->msg : "No error details");
        }
        if (!larodBuildTensorPitches(t, LAROD_TENSOR_LAYOUT_NHWC, vdo_pitch, vdo_h, 3, &error)) {
            PANIC("larodBuildTensorPitches: %s", error ? error->msg : "No error details");
        }
        if (!larodSetTensorFdProps(t, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error)) {
            PANIC("larodSetTensorFdProps: %s", error ? error->msg : "No error details");
        }
    }
    syslog(LOG_INFO, "Created %d input tensors (NHWC RGB %ux%u pitch=%u)", 2, vdo_w, vdo_h, vdo_pitch);

    /* ── 7. Inference job (created lazily) ── */
    larodJobRequest* job = NULL;

    /* ── 8. Main loop ── */
    while (running) {
        VdoBuffer* buf = vdo_stream_get_buffer(stream, &vdo_err);  /* blocks */
        if (!buf) {
            if (!running) break;
            PANIC("vdo_stream_get_buffer: %s", vdo_err ? vdo_err->message : "No error details");
        }

        int vdo_fd = vdo_buffer_get_fd(buf);
        int64_t source_offset = vdo_buffer_get_offset(buf);
        size_t cap = vdo_buffer_get_capacity(buf);
        if (vdo_fd < 0 || source_offset < 0 || cap == 0) {
            PANIC("Invalid VDO buffer descriptor, offset, or capacity");
        }

        /* Find or create tracked slot for this buffer */
        int slot = -1;
        for (int i = 0; i < 2; i++) {
            /* One VMEM descriptor can contain buffers at different offsets. */
            if (tracked_vdo_fds[i] == vdo_fd && tracked_offsets[i] == source_offset &&
                tracked_capacities[i] == cap) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            /* First time seeing this buffer — set up tensor */
            for (int i = 0; i < 2; i++) {
                if (tracked_vdo_fds[i] == -1) { slot = i; break; }
            }
            if (slot < 0) {
                PANIC("No free tracking slots");
            }

            int64_t offset = source_offset;
            int tensor_fd;
            if (convert_vmem) {
                /* Export the VMEM buffer; the resulting DMA-BUF starts at zero.
                 * This changes the memory handle, not the RGB pixel format. */
                tensor_fd = larodConvertVmemFdToDmabuf(vdo_fd, source_offset, &error);
                if (tensor_fd == LAROD_INVALID_FD) {
                    PANIC("larodConvertVmemFdToDmabuf: %s",
                          error ? error->msg : "No error details");
                }
                offset = 0;
            } else {
                /* VDO owns the original fd; keep our own descriptor. */
                tensor_fd = dup(vdo_fd);
                if (tensor_fd < 0) PANIC("dup: %s", strerror(errno));
            }
            tensor_fds[slot] = tensor_fd;

            /* Capacity is measured from the buffer start, but larod's size
             * limit is measured from the beginning of the fd. */
            if ((uint64_t)offset > SIZE_MAX - cap) {
                PANIC("VDO offset + capacity overflows size_t");
            }
            size_t fd_size = (size_t)offset + cap;

            larodTensor* t = in_tensors[slot][0];
            if (!larodSetTensorFd(t, tensor_fd, &error)) {
                PANIC("larodSetTensorFd: %s", error ? error->msg : "No error details");
            }
            if (!larodSetTensorFdOffset(t, offset, &error)) {
                PANIC("larodSetTensorFdOffset: %s", error ? error->msg : "No error details");
            }
            if (!larodSetTensorFdSize(t, fd_size, &error)) {
                PANIC("larodSetTensorFdSize: %s", error ? error->msg : "No error details");
            }
            if (!larodTrackTensor(conn, t, &error)) {
                PANIC("larodTrackTensor: %s", error ? error->msg : "No error details");
            }

            tracked_vdo_fds[slot] = vdo_fd;
            tracked_offsets[slot] = source_offset;
            tracked_capacities[slot] = cap;
            syslog(LOG_INFO,
                   "Tracked buffer slot %d (%s): VDO offset=%zu tensor offset=%zu "
                   "capacity=%zu fd size=%zu",
                   slot, convert_vmem ? "VMEM -> DMA-BUF" : "DMA-BUF",
                   (size_t)source_offset, (size_t)offset, cap, fd_size);
        }

        /* Create or update job */
        if (!job) {
            job = larodCreateJobRequest(model,
                                        in_tensors[slot], 1,
                                        out_tensors, num_out,
                                        NULL, &error);
            if (!job) {
                PANIC("larodCreateJobRequest: %s", error ? error->msg : "No error details");
            }
        } else {
            if (!larodSetJobRequestInputs(job, in_tensors[slot], 1, &error)) {
                PANIC("larodSetJobRequestInputs: %s", error ? error->msg : "No error details");
            }
        }

        /* Run inference */
        if (!larodRunJob(conn, job, &error)) {
            PANIC("larodRunJob: %s", error ? error->msg : "No error details");
        }
        uint8_t* person = (uint8_t*)out_data[0];
        uint8_t* car    = (uint8_t*)out_data[1];
        syslog(LOG_INFO, "Person: %.1f%% — Car: %.1f%%",
               *person / 2.55f, *car / 2.55f);

        vdo_stream_buffer_unref(stream, &buf, &vdo_err);
    }

    /* ── 9. Cleanup ── */
    larodDestroyJobRequest(&job);
    for (int i = 0; i < 2; i++) {
        if (in_tensors[i]) larodDestroyTensors(conn, &in_tensors[i], 1, &error);
        if (tensor_fds[i] >= 0) close(tensor_fds[i]);
    }
    for (size_t i = 0; i < 2; i++) munmap(out_data[i], out_sizes[i]);
    larodDestroyTensors(conn, &out_tensors, num_out, &error);
    larodDestroyModel(&model);
    larodDisconnect(&conn, &error);
    vdo_stream_stop(stream);
    g_object_unref(stream);
    if (model_fd >= 0) {
        close(model_fd);
    }

    syslog(LOG_INFO, "Done");
    closelog();
    return EXIT_SUCCESS;
}
