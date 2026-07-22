#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include <glib.h>

#include "larod.h"
#include "postprocess.h"
#include "vdo-buffer.h"
#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

/*
 * object_detection_min
 *
 * This example shows the full camera-to-inference path:
 *
 *  1. Connect to larod, the Axis inference service.
 *  2. Load a TensorFlow Lite object-detection model on the selected accelerator.
 *  3. Read the model input tensor shape so the video/preprocessing path can match it.
 *  4. Allocate and mmap output tensors so the CPU can read inference results.
 *  5. Open a VDO camera stream.
 *  6. Add preprocessing when the camera frames do not already match the model input.
 *  7. Describe each VDO frame buffer as a larod input tensor.
 *  8. Track each VDO buffer fd once so larod can reuse it safely.
 *  9. Start the stream and poll for frames.
 * 10. Run optional preprocessing for each frame.
 * 11. Run inference.
 * 12. Parse the SSD model outputs and draw normalized bounding boxes with bbox.
 * 13. Return each VDO buffer so it can be reused.
 * 14. Clean up resources on exit.
 */

/*
 * Configuration
 *
 * DEVICE_NAME selects the inference backend. The a9-dlpu-tflite backend can
 * consume RGB input directly. Other backends commonly need NV12-to-RGB
 * preprocessing on cpu-proc.
 *
 * MODEL_PATH must match where the Dockerfile packages converted_model.tflite.
 */
#define DEVICE_NAME "a9-dlpu-tflite"
#define PP_DEVICE_NAME "cpu-proc"
#define MODEL_PATH "/usr/local/packages/object_detection_min/model/converted_model.tflite"

/*
 * VDO stream settings
 *
 * VDO is the camera frame provider. We request a small stream to keep the
 * example lightweight. The actual frame size and format are read back from VDO
 * after stream creation because VDO may adjust settings to supported values.
 */
#define VDO_CHANNEL 2u
#define VDO_NUM_BUFFERS 2u
#define VDO_FRAMERATE 30.0
#define IMAGE_FIT "crop"
#define VDO_WIDTH 640u
#define VDO_HEIGHT 360u
#define MAX_TRACKED_BUFFERS 5u
#define MAX_OUTPUT_TENSORS 4u

static unsigned int MODEL_WIDTH = 0;
static unsigned int MODEL_HEIGHT = 0;
static volatile sig_atomic_t running = 1;

/*
 * Keep fatal error handling short in the example. Production code would usually
 * clean up before exiting, but this makes the tutorial flow easier to follow.
 */
#define PANIC(fmt, ...) do {                       \
    syslog(LOG_ERR, "FATAL: " fmt, ##__VA_ARGS__); \
    exit(EXIT_FAILURE);                            \
} while (0)

/*
 * One tracked_input_t represents one VDO buffer slot.
 *
 * VDO owns the original frame buffer. larod needs a tensor object describing
 * that same memory. We dup the fd so larod can keep its own reference while VDO
 * continues to recycle buffers.
 */
typedef struct {
    larodTensor** tensors;
    int duped_fd;
    int vdo_fd;
} tracked_input_t;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static bool backend_supports_rgb(const char* device_name) {
    return strcmp(device_name, "a9-dlpu-tflite") == 0;
}

/*
 * STEP 1 - Connect to larod
 *
 * larod is the daemon that owns inference devices and runs model jobs. All
 * model loading, tensor allocation, and job execution goes through this
 * connection.
 */
static larodConnection* larod_connect(void) {
    larodConnection* conn = NULL;
    larodError* error = NULL;

    if (!larodConnect(&conn, &error)) {
        PANIC("larodConnect: %s", error ? error->msg : "unknown error");
    }

    syslog(LOG_INFO, "Connected to larod successfully");
    return conn;
}

/*
 * STEP 2 - Load the inference model
 *
 * The model file is opened as an fd and passed to larod. larodGetDevice chooses
 * the backend, for example the A9 DLPU TFLite backend. larodLoadModel returns a
 * handle used later when creating inference jobs.
 */
static larodModel* load_inference_model(larodConnection* conn, int* model_fd_out) {
    larodError* error = NULL;
    int model_fd = open(MODEL_PATH, O_RDONLY);
    if (model_fd < 0) {
        PANIC("open(%s): %s", MODEL_PATH, strerror(errno));
    }
    *model_fd_out = model_fd;

    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    if (!device) {
        PANIC("larodGetDevice(%s): %s", DEVICE_NAME, error ? error->msg : "unknown error");
    }

    larodModel* model = larodLoadModel(conn,
                                       model_fd,
                                       device,
                                       LAROD_ACCESS_PRIVATE,
                                       "object detection model",
                                       NULL,
                                       &error);
    if (!model) {
        PANIC("larodLoadModel: %s", error ? error->msg : "unknown error");
    }

    syslog(LOG_INFO, "Model loaded successfully on %s", DEVICE_NAME);
    return model;
}

/*
 * STEP 3 - Read model input dimensions
 *
 * Object-detection models usually expect a fixed NHWC tensor:
 *   N = batch, H = height, W = width, C = channels.
 *
 * We allocate temporary model input tensors only so we can inspect the shape and
 * pitches larod reports for this model. The values drive VDO/preprocessing
 * setup, then the temporary tensors are destroyed.
 */
static void read_model_input_size(larodConnection* conn,
                                  larodModel* model,
                                  unsigned int* pitch_out) {
    larodError* error = NULL;
    size_t num_inputs = 0;
    larodTensor** tmp_inputs = larodAllocModelInputs(conn, model, 0, &num_inputs, NULL, &error);
    if (!tmp_inputs || num_inputs == 0) {
        PANIC("larodAllocModelInputs: %s", error ? error->msg : "unknown error");
    }

    const larodTensorDims* dims = larodGetTensorDims(tmp_inputs[0], &error);
    if (!dims || dims->len != 4) {
        PANIC("Expected 4D input tensor, got %zu dims", dims ? dims->len : 0);
    }

    MODEL_HEIGHT = dims->dims[1];
    MODEL_WIDTH = dims->dims[2];

    const larodTensorPitches* pitches = larodGetTensorPitches(tmp_inputs[0], &error);
    if (!pitches || pitches->len != 4) {
        PANIC("larodGetTensorPitches: %s", error ? error->msg : "unknown error");
    }
    *pitch_out = pitches->pitches[2];

    syslog(LOG_INFO, "Model expects %ux%u RGB input with W pitch %u",
           MODEL_WIDTH, MODEL_HEIGHT, *pitch_out);

    larodDestroyTensors(conn, &tmp_inputs, num_inputs, &error);
}

/*
 * STEP 4 - Allocate output tensors and mmap them
 *
 * larod owns tensor memory, but postprocessing runs on the CPU. The MAP flag
 * asks larod for mmap-able fds, and mmap gives this process a pointer to the
 * inference outputs after every job.
 *
 * The Coral SSD MobileNet model used here has four outputs:
 *   0: boxes      [ymin, xmin, ymax, xmax]
 *   1: classes
 *   2: scores
 *   3: number of detections
 */
static larodTensor** create_output_tensors(larodConnection* conn,
                                           larodModel* model,
                                           output_buf_t* out_bufs,
                                           size_t out_bufs_len,
                                           size_t* num_outputs) {
    larodError* error = NULL;
    larodTensor** tensors = larodAllocModelOutputs(conn,
                                                   model,
                                                   LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                                   num_outputs,
                                                   NULL,
                                                   &error);
    if (!tensors) {
        PANIC("larodAllocModelOutputs: %s", error ? error->msg : "unknown error");
    }
    if (*num_outputs > out_bufs_len) {
        PANIC("Model has %zu outputs, but only %zu output buffers are configured",
              *num_outputs, out_bufs_len);
    }

    for (size_t i = 0; i < *num_outputs; i++) {
        out_bufs[i].fd = larodGetTensorFd(tensors[i], &error);
        if (out_bufs[i].fd == LAROD_INVALID_FD) {
            PANIC("larodGetTensorFd: %s", error ? error->msg : "unknown error");
        }
        if (!larodGetTensorFdSize(tensors[i], &out_bufs[i].size, &error)) {
            PANIC("larodGetTensorFdSize: %s", error ? error->msg : "unknown error");
        }
        out_bufs[i].data = mmap(NULL, out_bufs[i].size, PROT_READ, MAP_SHARED, out_bufs[i].fd, 0);
        if (out_bufs[i].data == MAP_FAILED) {
            PANIC("mmap output tensor %zu: %s", i, strerror(errno));
        }
        syslog(LOG_INFO, "Output tensor %zu: fd=%d, size=%zu bytes",
               i, out_bufs[i].fd, out_bufs[i].size);
    }

    return tensors;
}

/*
 * STEP 5 - Create the VDO camera stream
 *
 * VDO provides camera frames. If the inference backend accepts RGB, request RGB.
 * Otherwise request YUV/NV12 and let cpu-proc preprocessing convert it.
 *
 * After stream creation we read back the actual format, size, pitch, number of
 * buffers, and whether the buffers are native DMA buffers.
 */
static VdoStream* create_vdo_stream(bool rgb_backend,
                                    unsigned int* out_w,
                                    unsigned int* out_h,
                                    unsigned int* out_pitch,
                                    unsigned int* out_nbr_bufs,
                                    bool* out_is_dmabuf,
                                    VdoFormat* out_format) {
    GError* error = NULL;
    VdoMap* settings = vdo_map_new();

    vdo_map_set_uint32(settings, "channel", VDO_CHANNEL);
    vdo_map_set_uint32(settings, "buffer.count", VDO_NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate", VDO_FRAMERATE);
    vdo_map_set_boolean(settings, "socket.blocking", false);
    vdo_map_set_string(settings, "image.fit", IMAGE_FIT);

    if (rgb_backend) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_RGB);
    } else {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
    }

    VdoPair32u resolution = { .w = VDO_WIDTH, .h = VDO_HEIGHT };
    vdo_map_set_pair32u(settings, "resolution", resolution);

    VdoStream* stream = vdo_stream_new(settings, NULL, &error);
    g_object_unref(settings);
    if (!stream) {
        PANIC("vdo_stream_new: %s", error ? error->message : "unknown error");
    }

    VdoMap* info = vdo_stream_get_info(stream, &error);
    if (!info) {
        PANIC("vdo_stream_get_info: %s", error ? error->message : "unknown error");
    }

    *out_format = vdo_map_get_uint32(info, "format", 0);
    *out_w = vdo_map_get_uint32(info, "width", 0);
    *out_h = vdo_map_get_uint32(info, "height", 0);
    *out_pitch = vdo_map_get_uint32(info, "pitch", 0);
    *out_nbr_bufs = vdo_map_get_uint32(info, "buffer.count", VDO_NUM_BUFFERS);

    const char* buf_type = vdo_map_get_string(info, "buffer.type", NULL, "memfd");
    *out_is_dmabuf = g_strcmp0(buf_type, "vmem") != 0;

    syslog(LOG_INFO, "VDO stream: %ux%u pitch=%u buffers=%u dmabuf=%d",
           *out_w, *out_h, *out_pitch, *out_nbr_bufs, *out_is_dmabuf);

    g_object_unref(info);
    return stream;
}

/*
 * STEP 6 - Set up preprocessing when needed
 *
 * Preprocessing is represented as a larod model on the cpu-proc device. There
 * is no model file, so fd = -1. The larodMap describes what comes in from VDO
 * and what should come out for the inference model.
 *
 * This step handles format conversion such as NV12 -> RGB and resizing to the
 * model input width/height.
 */
static larodModel* setup_preprocessing(larodConnection* conn,
                                       VdoFormat vdo_format,
                                       unsigned int vdo_w,
                                       unsigned int vdo_h,
                                       unsigned int vdo_pitch,
                                       unsigned int model_pitch,
                                       larodTensor*** pp_outputs_out,
                                       size_t* pp_num_outputs) {
    larodError* error = NULL;
    const char* input_format_str = NULL;

    switch (vdo_format) {
        case VDO_FORMAT_YUV:
            input_format_str = "nv12";
            break;
        case VDO_FORMAT_RGB:
            input_format_str = "rgb-interleaved";
            break;
        case VDO_FORMAT_PLANAR_RGB:
            input_format_str = "rgb-planar";
            break;
        default:
            PANIC("Unsupported VDO format: %u", (unsigned int)vdo_format);
    }

    larodMap* map = larodCreateMap(&error);
    if (!map) {
        PANIC("larodCreateMap: %s", error ? error->msg : "unknown error");
    }

    larodMapSetStr(map, "image.input.format", input_format_str, &error);
    larodMapSetIntArr2(map, "image.input.size", vdo_w, vdo_h, &error);
    larodMapSetInt(map, "image.input.row-pitch", vdo_pitch, &error);
    larodMapSetStr(map, "image.output.format", "rgb-interleaved", &error);
    larodMapSetIntArr2(map, "image.output.size", MODEL_WIDTH, MODEL_HEIGHT, &error);
    larodMapSetInt(map, "image.output.row-pitch", model_pitch, &error);

    const larodDevice* pp_device = larodGetDevice(conn, PP_DEVICE_NAME, 0, &error);
    if (!pp_device) {
        PANIC("larodGetDevice(%s): %s", PP_DEVICE_NAME, error ? error->msg : "unknown error");
    }

    larodModel* pp_model = larodLoadModel(conn,
                                          -1,
                                          pp_device,
                                          LAROD_ACCESS_PRIVATE,
                                          "",
                                          map,
                                          &error);
    if (!pp_model) {
        PANIC("larodLoadModel(preprocessing): %s", error ? error->msg : "unknown error");
    }
    larodDestroyMap(&map);

    *pp_outputs_out = larodAllocModelOutputs(conn,
                                             pp_model,
                                             LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                             pp_num_outputs,
                                             NULL,
                                             &error);
    if (!*pp_outputs_out) {
        PANIC("larodAllocModelOutputs(pp): %s", error ? error->msg : "unknown error");
    }

    syslog(LOG_INFO, "Preprocessing model loaded on %s", PP_DEVICE_NAME);
    return pp_model;
}

/*
 * STEP 7 - Create input tensor descriptors for VDO buffers
 *
 * These tensors do not allocate image memory. They describe memory that VDO
 * already owns: data type, layout, dimensions, pitch, and fd properties. Later,
 * when a concrete VDO buffer arrives, track_vdo_buffer attaches its fd.
 */
static void create_input_tensors(tracked_input_t* tracked,
                                 unsigned int nbr_bufs,
                                 unsigned int vdo_w,
                                 unsigned int vdo_h,
                                 unsigned int vdo_pitch,
                                 VdoFormat vdo_format) {
    larodError* error = NULL;
    larodTensorLayout layout;

    if (nbr_bufs > MAX_TRACKED_BUFFERS) {
        PANIC("VDO requested %u buffers, max tracked buffers is %u", nbr_bufs, MAX_TRACKED_BUFFERS);
    }

    switch (vdo_format) {
        case VDO_FORMAT_YUV:
            layout = LAROD_TENSOR_LAYOUT_420SP;
            break;
        case VDO_FORMAT_RGB:
            layout = LAROD_TENSOR_LAYOUT_NHWC;
            break;
        case VDO_FORMAT_PLANAR_RGB:
            layout = LAROD_TENSOR_LAYOUT_NCHW;
            break;
        default:
            PANIC("Unsupported VDO format: %u", (unsigned int)vdo_format);
    }

    for (unsigned int i = 0; i < nbr_bufs; i++) {
        tracked[i].vdo_fd = -1;
        tracked[i].duped_fd = -1;
        tracked[i].tensors = larodCreateTensors(1, &error);
        if (!tracked[i].tensors) {
            PANIC("larodCreateTensors[%u]: %s", i, error ? error->msg : "unknown error");
        }

        larodTensor* tensor = tracked[i].tensors[0];
        larodSetTensorDataType(tensor, LAROD_TENSOR_DATA_TYPE_UINT8, &error);
        larodSetTensorLayout(tensor, layout, &error);
        larodBuildTensorDims(tensor, layout, vdo_w, vdo_h, 3, &error);
        larodBuildTensorPitches(tensor, layout, vdo_pitch, vdo_h, 3, &error);
        larodSetTensorFdProps(tensor, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
    }
}

/*
 * STEP 8 - Track one VDO buffer fd
 *
 * VDO reuses a small pool of buffers. The first time we see a buffer fd, we
 * attach that fd to the matching larod tensor and call larodTrackTensor. On
 * later frames with the same fd, we only return the existing slot.
 *
 * If VDO gives vmem instead of a DMA buffer, larodConvertVmemFdToDmabuf converts
 * it before tracking.
 */
static int track_vdo_buffer(larodConnection* conn,
                            tracked_input_t* tracked,
                            unsigned int nbr_bufs,
                            VdoBuffer* vdo_buf,
                            bool is_dmabuf) {
    larodError* error = NULL;
    int vdo_fd = vdo_buffer_get_fd(vdo_buf);
    int64_t vdo_offset = vdo_buffer_get_offset(vdo_buf);
    size_t vdo_capacity = vdo_buffer_get_capacity(vdo_buf);

    for (unsigned int i = 0; i < nbr_bufs; i++) {
        if (tracked[i].vdo_fd == vdo_fd) {
            return (int)i;
        }
    }

    int slot = -1;
    for (unsigned int i = 0; i < nbr_bufs; i++) {
        if (tracked[i].vdo_fd == -1) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        PANIC("No free tracking slots");
    }

    int buf_fd = vdo_fd;
    if (!is_dmabuf) {
        buf_fd = larodConvertVmemFdToDmabuf(vdo_fd, vdo_offset, &error);
        if (buf_fd == LAROD_INVALID_FD) {
            PANIC("larodConvertVmemFdToDmabuf: %s", error ? error->msg : "unknown error");
        }
        vdo_offset = 0;
    }

    int duped = dup(buf_fd);
    if (duped < 0) {
        PANIC("dup: %s", strerror(errno));
    }

    larodTensor* tensor = tracked[slot].tensors[0];
    larodSetTensorFd(tensor, duped, &error);
    larodSetTensorFdOffset(tensor, vdo_offset, &error);
    larodSetTensorFdSize(tensor, vdo_capacity, &error);
    if (!larodTrackTensor(conn, tensor, &error)) {
        PANIC("larodTrackTensor: %s", error ? error->msg : "unknown error");
    }

    tracked[slot].vdo_fd = vdo_fd;
    tracked[slot].duped_fd = duped;
    return slot;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const int threshold = 50;
    bbox_t* bbox = NULL;
    larodConnection* conn = NULL;
    larodModel* inf_model = NULL;
    larodModel* pp_model = NULL;
    larodTensor** inf_outputs = NULL;
    larodTensor** pp_outputs = NULL;
    larodJobRequest* pp_job_request = NULL;
    larodJobRequest* inf_job_request = NULL;
    size_t num_inf_outputs = 0;
    size_t pp_num_outputs = 0;
    int model_fd = -1;
    unsigned int model_pitch = 0;
    output_buf_t out_bufs[MAX_OUTPUT_TENSORS] = {
        {.fd = -1, .data = MAP_FAILED},
        {.fd = -1, .data = MAP_FAILED},
        {.fd = -1, .data = MAP_FAILED},
        {.fd = -1, .data = MAP_FAILED},
    };
    tracked_input_t tracked[MAX_TRACKED_BUFFERS] = {0};
    VdoStream* vdo_stream = NULL;
    unsigned int vdo_w = 0;
    unsigned int vdo_h = 0;
    unsigned int vdo_pitch = 0;
    unsigned int vdo_nbr_bufs = 0;
    VdoFormat vdo_format = 0;
    bool vdo_is_dmabuf = false;
    GError* vdo_error = NULL;

    openlog("object_detection_min", LOG_PERROR | LOG_PID, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    /*
     * STEP 1-4 - Prepare larod and the model.
     *
     * At the end of this block we know the model input size and have CPU-visible
     * output buffers ready for postprocessing.
     */
    conn = larod_connect();
    inf_model = load_inference_model(conn, &model_fd);
    read_model_input_size(conn, inf_model, &model_pitch);
    inf_outputs = create_output_tensors(conn, inf_model, out_bufs, MAX_OUTPUT_TENSORS, &num_inf_outputs);

    /*
     * STEP 5 - Open the camera stream.
     *
     * The requested format depends on backend capability, but the values used
     * below are the actual stream values returned by VDO.
     */
    bool rgb_backend = backend_supports_rgb(DEVICE_NAME);
    vdo_stream = create_vdo_stream(rgb_backend,
                                   &vdo_w,
                                   &vdo_h,
                                   &vdo_pitch,
                                   &vdo_nbr_bufs,
                                   &vdo_is_dmabuf,
                                   &vdo_format);

    /*
     * STEP 6 - Decide whether preprocessing is needed.
     *
     * We need preprocessing if the backend cannot consume RGB directly, or if
     * the VDO stream dimensions do not match the model input dimensions.
     */
    bool need_pp = !rgb_backend || vdo_w != MODEL_WIDTH || vdo_h != MODEL_HEIGHT;
    if (need_pp) {
        pp_model = setup_preprocessing(conn,
                                       vdo_format,
                                       vdo_w,
                                       vdo_h,
                                       vdo_pitch,
                                       model_pitch,
                                       &pp_outputs,
                                       &pp_num_outputs);
    }

    /*
     * STEP 7 - Create larod input tensor descriptors for each possible VDO
     * buffer. The real fd is attached lazily when a frame arrives.
     */
    create_input_tensors(tracked, vdo_nbr_bufs, vdo_w, vdo_h, vdo_pitch, vdo_format);

    /*
     * BBox setup
     *
     * bbox draws rectangles in the camera view. The postprocess function uses
     * normalized coordinates from the SSD model, so the boxes scale with the
     * displayed frame.
     */
    bbox = setup_bbox(VDO_CHANNEL);
    if (!bbox) {
        PANIC("setup_bbox failed");
    }

    /*
     * STEP 9 - Start the stream and poll for frames.
     *
     * The stream is non-blocking, so poll waits until VDO has a frame ready.
     */
    if (!vdo_stream_start(vdo_stream, &vdo_error)) {
        PANIC("vdo_stream_start: %s", vdo_error ? vdo_error->message : "unknown error");
    }

    int poll_fd = vdo_stream_get_fd(vdo_stream, &vdo_error);
    if (poll_fd < 0) {
        PANIC("vdo_stream_get_fd: %s", vdo_error ? vdo_error->message : "unknown error");
    }

    struct pollfd pfd = {.fd = poll_fd, .events = POLLIN};
    syslog(LOG_INFO, "Entering inference loop");

    while (running) {
        larodError* error = NULL;
        int ret;

        /* STEP 9a - Wait until the VDO stream has a frame available. */
        do {
            ret = poll(&pfd, 1, -1);
        } while (ret == -1 && errno == EINTR);
        if (ret < 0) {
            PANIC("poll: %s", strerror(errno));
        }

        /* STEP 9b - Fetch one VDO buffer from the stream. */
        VdoBuffer* vdo_buf = vdo_stream_get_buffer(vdo_stream, &vdo_error);
        if (!vdo_buf) {
            if (g_error_matches(vdo_error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
                g_clear_error(&vdo_error);
                continue;
            }
            PANIC("vdo_stream_get_buffer: %s", vdo_error ? vdo_error->message : "unknown error");
        }

        /*
         * STEP 8 - Track the frame buffer if this fd is new.
         *
         * input now points to the larod tensor array describing this frame.
         */
        int slot = track_vdo_buffer(conn, tracked, vdo_nbr_bufs, vdo_buf, vdo_is_dmabuf);
        larodTensor** input = tracked[slot].tensors;

        /*
         * STEP 10 - Run preprocessing, only when needed.
         *
         * The preprocessing job consumes the VDO tensor and writes RGB/resized
         * output tensors. The job object is created once, then its input tensor
         * is updated for each new VDO buffer.
         */
        if (need_pp) {
            if (!pp_job_request) {
                pp_job_request = larodCreateJobRequest(pp_model,
                                                       input,
                                                       1,
                                                       pp_outputs,
                                                       pp_num_outputs,
                                                       NULL,
                                                       &error);
                if (!pp_job_request) {
                    PANIC("larodCreateJobRequest(pp): %s", error ? error->msg : "unknown error");
                }
            } else {
                larodSetJobRequestInputs(pp_job_request, input, 1, &error);
            }

            if (!larodRunJob(conn, pp_job_request, &error)) {
                PANIC("larodRunJob(pp): %s", error ? error->msg : "unknown error");
            }
        }

        /*
         * STEP 11 - Run inference.
         *
         * If preprocessing ran, inference consumes pp_outputs. Otherwise it
         * consumes the raw VDO tensor directly.
         */
        larodTensor** inf_input = need_pp ? pp_outputs : input;
        size_t inf_input_n = need_pp ? pp_num_outputs : 1;

        if (!inf_job_request) {
            inf_job_request = larodCreateJobRequest(inf_model,
                                                    inf_input,
                                                    inf_input_n,
                                                    inf_outputs,
                                                    num_inf_outputs,
                                                    NULL,
                                                    &error);
            if (!inf_job_request) {
                PANIC("larodCreateJobRequest(inf): %s", error ? error->msg : "unknown error");
            }
        } else if (!need_pp) {
            larodSetJobRequestInputs(inf_job_request, inf_input, inf_input_n, &error);
        }

        if (!larodRunJob(conn, inf_job_request, &error)) {
            PANIC("larodRunJob(inf): %s", error ? error->msg : "unknown error");
        }

        /*
         * STEP 12 - Parse detections and draw boxes.
         *
         * The four mapped output tensors are read as floats. postprocess.c
         * filters by score and commits rectangles through bbox.
         */
        if (num_inf_outputs >= MAX_OUTPUT_TENSORS) {
            float confidence_threshold = (float)threshold / 100.0f;
            if (!parse_and_postprocess_output_tensors(bbox, out_bufs, confidence_threshold)) {
                syslog(LOG_ERR, "Failed to postprocess output tensors");
            }
        }

        /*
         * STEP 13 - Return the VDO buffer.
         *
         * VDO keeps ownership of frame buffers. Once this frame has been used,
         * unref it so VDO can recycle it for future frames.
         */
        if (!vdo_stream_buffer_unref(vdo_stream, &vdo_buf, &vdo_error)) {
            if (!vdo_error_is_expected(&vdo_error)) {
                PANIC("buffer_unref: %s", vdo_error ? vdo_error->message : "unknown error");
            }
            g_clear_error(&vdo_error);
        }
    }

    /*
     * STEP 14 - Cleanup.
     *
     * Stop VDO, unmap output buffers, destroy larod jobs/tensors/models, close
     * fds, and remove the bbox handle.
     */
    syslog(LOG_INFO, "Shutting down");

    if (vdo_stream) {
        vdo_stream_stop(vdo_stream);
        g_object_unref(vdo_stream);
    }

    for (size_t i = 0; i < num_inf_outputs; i++) {
        if (out_bufs[i].data != MAP_FAILED) {
            munmap(out_bufs[i].data, out_bufs[i].size);
        }
    }

    larodDestroyJobRequest(&pp_job_request);
    larodDestroyJobRequest(&inf_job_request);

    larodError* cleanup_error = NULL;
    for (unsigned int i = 0; i < MAX_TRACKED_BUFFERS; i++) {
        if (tracked[i].tensors) {
            larodDestroyTensors(conn, &tracked[i].tensors, 1, &cleanup_error);
        }
        if (tracked[i].duped_fd >= 0) {
            close(tracked[i].duped_fd);
        }
    }

    if (pp_outputs) {
        larodDestroyTensors(conn, &pp_outputs, pp_num_outputs, &cleanup_error);
    }
    if (inf_outputs) {
        larodDestroyTensors(conn, &inf_outputs, num_inf_outputs, &cleanup_error);
    }

    larodDestroyModel(&pp_model);
    larodDestroyModel(&inf_model);
    larodDisconnect(&conn, &cleanup_error);
    larodClearError(&cleanup_error);

    if (model_fd >= 0) {
        close(model_fd);
    }
    if (bbox) {
        bbox_destroy(bbox);
    }
    g_clear_error(&vdo_error);
    closelog();

    return EXIT_SUCCESS;
}
