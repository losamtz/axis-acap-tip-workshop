/**
 * simple_vdo_larod.c
 *
 * Minimal but production-realistic VDO → larod pipeline on Axis cameras.
 *
 * Supports two paths:
 *   - a9-dlpu-tflite: VDO delivers RGB directly, preprocessing only if resize needed
 *   - a8-dlpu-tflite (and others): VDO delivers NV12, preprocessing converts + resizes
 *
 * What it does:
 *   1. Connects to larod, loads a person/car classification model
 *   2. Opens a VDO stream (RGB or NV12 depending on backend)
 *   3. If needed → sets up larod preprocessing
 *   4. Grabs frames via poll() → preprocesses → infers
 *   5. Prints "Person: X% — Car: Y%"
 *
 * Build: see Dockerfile / Makefile
 * Run:   ./simple_vdo_larod
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include "larod.h"
#include "vdo-buffer.h"
#include "vdo-channel.h"
#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>

/* ══════════════════════════════════════════════
 *  Configuration — change DEVICE_NAME for your hardware
 * ══════════════════════════════════════════════ */

/* Inference backend — change to match your device */
#define DEVICE_NAME     "a9-dlpu-tflite"     /* or "axis-a8-dlpu-tflite"      */
#define PP_DEVICE_NAME  "cpu-proc"           /* preprocessing always on CPU   */
#define MODEL_PATH      "/usr/local/packages/vdo_larod_min/model/model.tflite"

/* VDO stream settings */
#define VDO_CHANNEL     1
#define VDO_NUM_BUFFERS 2
#define VDO_FRAMERATE   2.0

/* We'll read these from the model at runtime */
static unsigned int MODEL_WIDTH  = 0;
static unsigned int MODEL_HEIGHT = 0;

/* ══════════════════════════════════════════════
 *  Backend capability detection
 *
 *  a9-dlpu-tflite:        accepts RGB directly from VDO
 *  axis-a8-dlpu-tflite:   needs NV12 → RGB preprocessing
 *  everything else:       needs NV12 → RGB preprocessing
 * ══════════════════════════════════════════════ */

 static bool backend_supports_rgb(const char* device_name) {
    return (strcmp(device_name, "a9-dlpu-tflite") == 0);
 }

/* ══════════════════════════════════════════════
 *  Signal handling
 * ══════════════════════════════════════════════ */

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

/* ══════════════════════════════════════════════
 *  Helper: panic and exit
 * ══════════════════════════════════════════════ */

#define PANIC(fmt, ...) do {                        \
    syslog(LOG_ERR, "FATAL: " fmt, ##__VA_ARGS__);  \
    exit(EXIT_FAILURE);                              \
} while (0)

/* ══════════════════════════════════════════════
 *  Structures
 * ══════════════════════════════════════════════ */

/* One output tensor result */
typedef struct {
    int     fd;        /* tensor fd            */
    void*   data;      /* mmap'd pointer       */
    size_t  size;      /* byte size            */
} output_buf_t;

/* Per-VDO-buffer tracked tensor state */
typedef struct {
    larodTensor** tensors;   /* the input tensor array (len=1) */
    int           duped_fd;  /* dup'd dmabuf fd                */
    int           vdo_fd;    /* original vdo buffer fd (key)   */
} tracked_input_t;

/* ══════════════════════════════════════════════
 *
 *  STEP 1 — CONNECT TO LAROD
 *
 * ══════════════════════════════════════════════ */

static larodConnection* larod_connect(void) {
    larodConnection* conn = NULL;
    larodError* error = NULL;

    if(!larodConnecrt(&conn, &error)) {
        PANIC("larodConnect: %s", error->msg);
    }
    syslog(LOG_INFO, "Connected to larod successfully");
    return conn;
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
    larodError* error = NULL;

    /* Open model file */
    int model_fd = open(MODEL_PATH, O_RDONLY);
    if (model_fd < 0) {
        PANIC("open(%s): %s", MODEL_PATH, strerror(errno));
    }
    *model_fd_out = model_fd;

    /* Get the device handle for our chosen backend */
    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    if (!device) {
        PANIC("larodGetDevice(%s): %s", DEVICE_NAME, error->msg);
    }

    /* Load the model — this may take a while on first load */
    syslog(LOG_INFO, "Loading model on %s (may take a minute)...", DEVICE_NAME);
    larodModel* model = larodLoadModel(conn,
                                       model_fd,
                                       device,
                                       LAROD_ACCESS_PRIVATE,
                                       "person-car model",
                                       NULL,    /* no extra params */
                                       &error);
    if (!model) {
        PANIC("larodLoadModel: %s", error->msg);
    }
    syslog(LOG_INFO, "Model loaded successfully");
    return model;
}
/* ══════════════════════════════════════════════
 *
 *  STEP 3 — READ MODEL INPUT DIMENSIONS
 *
 *  We query the model's input tensor to learn
 *  what width/height/format it expects.
 *  Then we destroy the temporary input tensors.
 *
 * ══════════════════════════════════════════════ */

static void read_model_input_size(larodConnection* conn,
                                  larodModel* model,
                                  unsigned int* pitch_out) {
    larodError* error       = NULL;
    larodTensor** tmp_inputs = NULL;
    size_t num_inputs        = 0;

    /* Allocate temporary input tensors just to read dimensions */
    tmp_inputs = larodAllocModelInputs(conn, model, 0, &num_inputs, NULL, &error);
    if (!tmp_inputs) {
        PANIC("larodAllocModelInputs: %s", error->msg);
    }

    /* Read dimensions: expect NHWC → dims = [batch, height, width, channels] */
    const larodTensorDims* dims = larodGetTensorDims(tmp_inputs[0], &error);
    if (!dims || dims->len != 4) {
        PANIC("Expected 4D input tensor, got %zu dims", dims ? dims->len : 0);
    }
    MODEL_HEIGHT = dims->dims[1];   /* H */
    MODEL_WIDTH  = dims->dims[2];   /* W */

    /* Read pitch (row stride in bytes) */
    const larodTensorPitches* pitches = larodGetTensorPitches(tmp_inputs[0], &error);
    if (!pitches) {
        PANIC("larodGetTensorPitches: %s", error->msg);
    }
    *pitch_out = pitches->pitches[2];

    syslog(LOG_INFO, "Model expects %ux%u input (pitch=%u)", MODEL_WIDTH, MODEL_HEIGHT, *pitch_out);

    /* Done — destroy the temporary tensors */
    larodDestroyTensors(conn, &tmp_inputs, num_inputs, &error);
}


/* ══════════════════════════════════════════════
 *
 *  STEP 4 — CREATE OUTPUT TENSORS + MMAP
 *
 *  larod allocates the output memory for us.
 *  We mmap each output tensor so we can read
 *  the results after inference.
 *
 * ══════════════════════════════════════════════ */

static larodTensor** create_output_tensors(larodConnection* conn,
                                           larodModel* model,
                                           output_buf_t* out_bufs,
                                           size_t* num_outputs) {
    larodError* error = NULL;

    /* Let larod allocate output tensors with read/write + mmap-able memory */
    larodTensor** tensors = larodAllocModelOutputs(conn,
                                                   model,
                                                   LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                                   num_outputs,
                                                   NULL,
                                                   &error);
    if (!tensors) {
        PANIC("larodAllocModelOutputs: %s", error->msg);
    }

    /* For each output tensor: get its fd, size, and mmap it */
    for (size_t i = 0; i < *num_outputs; i++) {
        out_bufs[i].fd = larodGetTensorFd(tensors[i], &error);
        if (out_bufs[i].fd == LAROD_INVALID_FD) {
            PANIC("larodGetTensorFd[%zu]: %s", i, error->msg);
        }

        if (!larodGetTensorFdSize(tensors[i], &out_bufs[i].size, &error)) {
            PANIC("larodGetTensorFdSize[%zu]: %s", i, error->msg);
        }

        out_bufs[i].data = mmap(NULL, out_bufs[i].size, PROT_READ, MAP_SHARED,
                                out_bufs[i].fd, 0);
        if (out_bufs[i].data == MAP_FAILED) {
            PANIC("mmap output[%zu]: %s", i, strerror(errno));
        }

        syslog(LOG_INFO, "Output tensor %zu: fd=%d, size=%zu", i, out_bufs[i].fd, out_bufs[i].size);
    }

    return tensors;
}
/* ══════════════════════════════════════════════
 *
 *  STEP 5 — CREATE VDO STREAM
 *
 *  Opens the camera video stream. We request
 *  YUV (NV12) format since preprocessing will
 *  handle the conversion. Non-blocking + poll.
 *
 * ══════════════════════════════════════════════ */

static VdoStream* create_vdo_stream(unsigned int* out_w,
                                    unsigned int* out_h,
                                    unsigned int* out_pitch,
                                    unsigned int* out_nbr_bufs,
                                    bool* out_is_dmabuf) {
    GError* error = NULL;

    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "channel",         VDO_CHANNEL);
    vdo_map_set_uint32(settings, "format",          VDO_FORMAT_YUV);
    vdo_map_set_uint32(settings, "buffer.count",    VDO_NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate",       VDO_FRAMERATE);
    vdo_map_set_boolean(settings, "socket.blocking", false);

    /* Request the model's resolution — VDO may give us something else */
    VdoPair32u resolution = { .w = MODEL_WIDTH, .h = MODEL_HEIGHT };
    vdo_map_set_pair32u(settings, "resolution", resolution);

    VdoStream* stream = vdo_stream_new(settings, NULL, &error);
    g_object_unref(settings);
    if (!stream) {
        PANIC("vdo_stream_new: %s", error->message);
    }

    /* Read back what VDO actually gave us */
    VdoMap* info = vdo_stream_get_info(stream, &error);
    if (!info) {
        PANIC("vdo_stream_get_info: %s", error->message);
    }
    *out_w         = vdo_map_get_uint32(info, "width", 0);
    *out_h         = vdo_map_get_uint32(info, "height", 0);
    *out_pitch     = vdo_map_get_uint32(info, "pitch", 0);
    *out_nbr_bufs  = vdo_map_get_uint32(info, "buffer.count", VDO_NUM_BUFFERS);

    const char* buf_type = vdo_map_get_string(info, "buffer.type", NULL, "memfd");
    *out_is_dmabuf = (g_strcmp0(buf_type, "vmem") != 0);

    syslog(LOG_INFO, "VDO stream: %ux%u, pitch=%u, buffers=%u, dmabuf=%d",
           *out_w, *out_h, *out_pitch, *out_nbr_bufs, *out_is_dmabuf);

    g_object_unref(info);
    return stream;
}

/* ══════════════════════════════════════════════
 *
 *  STEP 6 — SET UP PREPROCESSING (if needed)
 *
 *  If VDO delivers a different format or size
 *  than the model expects, we create a larod
 *  preprocessing model (cpu-proc / libyuv) to
 *  convert and resize.
 *
 * ══════════════════════════════════════════════ */

static larodModel* setup_preprocessing(larodConnection* conn,
                                        unsigned int vdo_w,
                                        unsigned int vdo_h,
                                        unsigned int vdo_pitch,
                                        unsigned int model_pitch,
                                        larodTensor*** pp_outputs_out,
                                        size_t* pp_num_outputs) {
    larodError* error = NULL;

    syslog(LOG_INFO, "Preprocessing: NV12 %ux%u → RGB %ux%u",
           vdo_w, vdo_h, MODEL_WIDTH, MODEL_HEIGHT);

    /* Build the parameter map describing input and output formats */
    larodMap* map = larodCreateMap(&error);
    if (!map) PANIC("larodCreateMap: %s", error->msg);

    /* Input: what VDO gives us (NV12 / YUV) */
    larodMapSetStr(map, "image.input.format", "nv12", &error);
    larodMapSetIntArr2(map, "image.input.size", vdo_w, vdo_h, &error);
    larodMapSetInt(map, "image.input.row-pitch", vdo_pitch, &error);

    /* Output: what the model needs (RGB interleaved) */
    larodMapSetStr(map, "image.output.format", "rgb-interleaved", &error);
    larodMapSetIntArr2(map, "image.output.size", MODEL_WIDTH, MODEL_HEIGHT, &error);
    larodMapSetInt(map, "image.output.row-pitch", model_pitch, &error);

    /* Load preprocessing as a "model" on cpu-proc (no model file → fd = -1) */
    const larodDevice* pp_device = larodGetDevice(conn, PP_DEVICE_NAME, 0, &error);
    larodModel* pp_model = larodLoadModel(conn, -1, pp_device,
                                          LAROD_ACCESS_PRIVATE, "", map, &error);
    if (!pp_model) {
        PANIC("larodLoadModel(preprocessing): %s", error->msg);
    }
    larodDestroyMap(&map);

    /* Allocate output tensors for preprocessing */
    *pp_outputs_out = larodAllocModelOutputs(conn, pp_model,
                                             LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                             pp_num_outputs, NULL, &error);
    if (!*pp_outputs_out) {
        PANIC("larodAllocModelOutputs(pp): %s", error->msg);
    }

    syslog(LOG_INFO, "Preprocessing model loaded on %s", PP_DEVICE_NAME);
    return pp_model;
}



/* ══════════════════════════════════════════════
 *
 *  STEP 7 — CREATE INPUT TENSORS FOR VDO BUFFERS
 *
 *  We create one input tensor per VDO buffer.
 *  Each tensor describes the image layout
 *  (NV12, width, height, pitch) and is flagged
 *  for DMA-buf access.
 *
 * ══════════════════════════════════════════════ */

static void create_input_tensors(tracked_input_t* tracked,
                                 unsigned int nbr_bufs,
                                 unsigned int vdo_w,
                                 unsigned int vdo_h,
                                 unsigned int vdo_pitch) {
    larodError* error = NULL;

    for (unsigned int i = 0; i < nbr_bufs; i++) {
        tracked[i].vdo_fd   = -1;
        tracked[i].duped_fd = -1;

        /* Create one tensor */
        tracked[i].tensors = larodCreateTensors(1, &error);
        if (!tracked[i].tensors) {
            PANIC("larodCreateTensors[%u]: %s", i, error->msg);
        }

        larodTensor* t = tracked[i].tensors[0];

        /* Tell larod about the data format */
        larodSetTensorDataType(t, LAROD_TENSOR_DATA_TYPE_UINT8, &error);
        larodSetTensorLayout(t, LAROD_TENSOR_LAYOUT_420SP, &error);        /* NV12 */
        larodBuildTensorDims(t, LAROD_TENSOR_LAYOUT_420SP, vdo_w, vdo_h, 3, &error);
        larodBuildTensorPitches(t, LAROD_TENSOR_LAYOUT_420SP, vdo_pitch, vdo_h, 3, &error);
        larodSetTensorFdProps(t, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
    }
    syslog(LOG_INFO, "Created %u input tensors (NV12 %ux%u)", nbr_bufs, vdo_w, vdo_h);
}

/* ══════════════════════════════════════════════
 *
 *  STEP 8 — TRACK A VDO BUFFER
 *
 *  When we see a VDO buffer fd for the first
 *  time, we dup it, convert vmem→dmabuf if
 *  needed, and register ("track") the tensor
 *  with larod so it knows the memory region.
 *
 * ══════════════════════════════════════════════ */

static int track_vdo_buffer(larodConnection* conn,
                            tracked_input_t* tracked,
                            unsigned int nbr_bufs,
                            VdoBuffer* vdo_buf,
                            bool is_dmabuf) {
    larodError* error = NULL;

    int vdo_fd          = vdo_buffer_get_fd(vdo_buf);
    int64_t vdo_offset  = vdo_buffer_get_offset(vdo_buf);
    size_t vdo_capacity = vdo_buffer_get_capacity(vdo_buf);

    /* Check if we've already tracked this fd */
    for (unsigned int i = 0; i < nbr_bufs; i++) {
        if (tracked[i].vdo_fd == vdo_fd) {
            return (int)i;   /* already tracked */
        }
    }

    /* Find the next free slot */
    int slot = -1;
    for (unsigned int i = 0; i < nbr_bufs; i++) {
        if (tracked[i].vdo_fd == -1) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        PANIC("No free tracking slots (max=%u)", nbr_bufs);
    }

    /* Convert vmem → dmabuf if the device doesn't use dmabuf natively */
    int buf_fd = vdo_fd;
    if (!is_dmabuf) {
        buf_fd = larodConvertVmemFdToDmabuf(vdo_fd, vdo_offset, &error);
        if (buf_fd == LAROD_INVALID_FD) {
            PANIC("larodConvertVmemFdToDmabuf: %s", error->msg);
        }
        vdo_offset = 0;   /* offset is baked into the new fd */
    }

    /* Dup the fd so larod can own its own copy */
    int duped = dup(buf_fd);
    if (duped < 0) {
        PANIC("dup: %s", strerror(errno));
    }

    /* Bind the fd to the tensor and register it with larod */
    larodTensor* t = tracked[slot].tensors[0];
    larodSetTensorFd(t, duped, &error);
    larodSetTensorFdOffset(t, vdo_offset, &error);
    larodSetTensorFdSize(t, vdo_capacity, &error);
    larodTrackTensor(conn, t, &error);

    tracked[slot].vdo_fd   = vdo_fd;
    tracked[slot].duped_fd = duped;

    syslog(LOG_INFO, "Tracked VDO buffer slot %d (vdo_fd=%d, duped=%d)", slot, vdo_fd, duped);
    return slot;
}

/* ══════════════════════════════════════════════
 *
 *  MAIN
 *
 * ══════════════════════════════════════════════ */

int main(void) {
    /* ── Local variables ── */
    larodConnection*  conn            = NULL;
    larodModel*       inf_model       = NULL;
    larodModel*       pp_model        = NULL;
    larodTensor**     inf_outputs     = NULL;
    larodTensor**     pp_outputs      = NULL;
    larodJobRequest*  pp_job          = NULL;
    larodJobRequest*  inf_job         = NULL;
    size_t            num_inf_outputs = 0;
    size_t            pp_num_outputs  = 0;
    int               model_fd       = -1;
    unsigned int      model_pitch    = 0;
    output_buf_t      out_bufs[2]    = {{.fd=-1, .data=MAP_FAILED},
                                        {.fd=-1, .data=MAP_FAILED}};
    tracked_input_t   tracked[5]     = {0};   /* MAX_NBR_IMG_PROVIDER_BUFFERS = 5 */
    VdoStream*        vdo_stream     = NULL;
    unsigned int      vdo_w, vdo_h, vdo_pitch, vdo_nbr_bufs;
    bool              vdo_is_dmabuf;

    /* ── Init ── */
    openlog("vdo_larod_min", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);
    syslog(LOG_INFO, "========== Starting vdo_larod_min ==========");

    /* ── Step 1: Connect to larod ── */
    conn = larod_connect();

    /* ── Step 2: Load inference model ── */
    inf_model = load_inference_model(conn, &model_fd);

    /* ── Step 3: Read what the model expects ── */
    read_model_input_size(conn, inf_model, &model_pitch);

    /* ── Step 4: Create + mmap output tensors ── */
    inf_outputs = create_output_tensors(conn, inf_model, out_bufs, &num_inf_outputs);
    syslog(LOG_INFO, "Model has %zu output tensors", num_inf_outputs);

    /* ── Step 5: Create VDO stream ── */
    vdo_stream = create_vdo_stream(&vdo_w, &vdo_h, &vdo_pitch, &vdo_nbr_bufs, &vdo_is_dmabuf);

    /* ── Step 6: Do we need preprocessing? ── */
    bool need_pp = (vdo_w != MODEL_WIDTH || vdo_h != MODEL_HEIGHT);

    if (need_pp) {
        pp_model = setup_preprocessing(conn, vdo_w, vdo_h, vdo_pitch, model_pitch,
                                       &pp_outputs, &pp_num_outputs);
    }
    syslog(LOG_INFO, "Preprocessing: %s", need_pp ? "YES" : "NO (VDO matches model)");

    /* ── Step 7: Create input tensors (one per VDO buffer) ── */
    create_input_tensors(tracked, vdo_nbr_bufs, vdo_w, vdo_h, vdo_pitch);

    /* ── Step 8: Start VDO stream and enter main loop ── */
    GError* vdo_error = NULL;
    if (!vdo_stream_start(vdo_stream, &vdo_error)) {
        PANIC("vdo_stream_start: %s", vdo_error->message);
    }

    int poll_fd = vdo_stream_get_fd(vdo_stream, &vdo_error);
    if (poll_fd < 0) {
        PANIC("vdo_stream_get_fd: %s", vdo_error->message);
    }
    struct pollfd pfd = { .fd = poll_fd, .events = POLLIN };

    syslog(LOG_INFO, "Entering main inference loop");

    while (running) {
        larodError* error = NULL;

        /* ── 8a: Wait for a frame ── */
        int ret;
        do {
            ret = poll(&pfd, 1, -1);
        } while (ret == -1 && errno == EINTR);
        if (ret < 0) PANIC("poll: %s", strerror(errno));

        /* ── 8b: Get the VDO buffer ── */
        VdoBuffer* vdo_buf = vdo_stream_get_buffer(vdo_stream, &vdo_error);
        if (!vdo_buf) {
            if (g_error_matches(vdo_error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
                g_clear_error(&vdo_error);
                continue;
            }
            PANIC("vdo_stream_get_buffer: %s", vdo_error->message);
        }

        /* ── 8c: Track the buffer (first-time setup per VDO fd) ── */
        int slot = track_vdo_buffer(conn, tracked, vdo_nbr_bufs, vdo_buf, vdo_is_dmabuf);
        larodTensor** input = tracked[slot].tensors;

        /* ── 8d: Run preprocessing (if needed) ── */
        if (need_pp) {
            /* Lazy-create the preprocessing job request */
            if (!pp_job) {
                pp_job = larodCreateJobRequest(pp_model,
                                               input, 1,
                                               pp_outputs, pp_num_outputs,
                                               NULL, &error);
                if (!pp_job) PANIC("larodCreateJobRequest(pp): %s", error->msg);
            } else {
                larodSetJobRequestInputs(pp_job, input, 1, &error);
            }

            if (!larodRunJob(conn, pp_job, &error)) {
                PANIC("larodRunJob(pp): %s", error->msg);
            }
        }

        /* ── 8e: Run inference ── */
        /* Input is either the raw VDO tensor or the PP output */
        larodTensor** inf_input    = need_pp ? pp_outputs : input;
        size_t        inf_input_n  = need_pp ? pp_num_outputs : 1;

        if (!inf_job) {
            inf_job = larodCreateJobRequest(inf_model,
                                            inf_input, inf_input_n,
                                            inf_outputs, num_inf_outputs,
                                            NULL, &error);
            if (!inf_job) PANIC("larodCreateJobRequest(inf): %s", error->msg);
        }
        /* No need to update inputs — they don't change after PP is stable */

        if (!larodRunJob(conn, inf_job, &error)) {
            PANIC("larodRunJob(inf): %s", error->msg);
        }

        /* ── 8f: Read results ── */
        if (num_inf_outputs >= 2) {
            uint8_t* person = (uint8_t*)out_bufs[0].data;
            uint8_t* car    = (uint8_t*)out_bufs[1].data;
            syslog(LOG_INFO,
                   "Person: %.1f%% — Car: %.1f%%",
                   (float)*person / 2.55f,
                   (float)*car / 2.55f);
        }

        /* ── 8g: Return VDO buffer ── */
        if (!vdo_stream_buffer_unref(vdo_stream, &vdo_buf, &vdo_error)) {
            if (!vdo_error_is_expected(&vdo_error)) {
                PANIC("buffer_unref: %s", vdo_error->message);
            }
            g_clear_error(&vdo_error);
        }
    }

    /* ══════════════════════════════════════════════
     *  STEP 9 — CLEANUP
     * ══════════════════════════════════════════════ */
    syslog(LOG_INFO, "Shutting down...");

    /* Stop VDO */
    if (vdo_stream) {
        vdo_stream_stop(vdo_stream);
        g_object_unref(vdo_stream);
    }

    /* Unmap output buffers */
    for (int i = 0; i < 2; i++) {
        if (out_bufs[i].data != MAP_FAILED) munmap(out_bufs[i].data, out_bufs[i].size);
    }

    /* Destroy job requests */
    larodDestroyJobRequest(&pp_job);
    larodDestroyJobRequest(&inf_job);

    /* Destroy tracked input tensors */
    larodError* cerr = NULL;
    for (unsigned int i = 0; i < VDO_NUM_BUFFERS; i++) {
        if (tracked[i].tensors) {
            larodDestroyTensors(conn, &tracked[i].tensors, 1, &cerr);
        }
        if (tracked[i].duped_fd >= 0) close(tracked[i].duped_fd);
    }

    /* Destroy output tensors */
    if (pp_outputs)  larodDestroyTensors(conn, &pp_outputs,  pp_num_outputs,  &cerr);
    if (inf_outputs) larodDestroyTensors(conn, &inf_outputs, num_inf_outputs, &cerr);

    /* Destroy models */
    larodDestroyModel(&pp_model);
    larodDestroyModel(&inf_model);

    /* Disconnect */
    larodDisconnect(&conn, &cerr);
    larodClearError(&cerr);

    /* Close model file */
    if (model_fd >= 0) close(model_fd);

    syslog(LOG_INFO, "========== vdo_larod_min exited ==========");
    closelog();
    return EXIT_SUCCESS;
}