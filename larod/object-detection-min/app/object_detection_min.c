/**
 * - object_detection_min-
 *
 * This application loads a larod model which takes an image as input and
 * outputs values corresponding to the class, score and location of detected
 * objects in the image.
 *
 * The application expects at least one argument on the command line in the
 * following order: MODEL.
 *
 * If THRESHOLD and LABELSFILE is supplied postprocessing will be used.
 *
 * First argument, MODEL, is a string describing path to the model.
 *
 * Second argument, THRESHOLD is an integer ranging from 0 to 100 to select good detections.
 *
 * Third argument, LABELSFILE, is a string describing path to the label txt.
 *
 * FOURTH argument, DEVICE, is a string for which larod device to use.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-types.h"
#include "larod.h"
#include "postprocess.h"
#include <bbox.h>

#include <math.h>
#include <poll.h>
#include <unistd.h>

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
#define VDO_FRAMERATE   30.0
#define IMAGE_FIT       "crop"     /* scale or crop */
#define VDO_WIDTH       640
#define VDO_HEIGHT      360

/* We'll read these from the model at runtime */
static unsigned int MODEL_WIDTH  = 0; // 300
static unsigned int MODEL_HEIGHT = 0; // 300


/* ══════════════════════════════════════════════
 *  Backend capability detection
 *
 *  a9-dlpu-tflite:        accepts RGB directly from VDO
 *  axis-a8-dlpu-tflite:   needs NV12 → RGB preprocessing
 *  
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

    if(!larodConnect(&conn, &error)) {
        PANIC("Failed to connect to larod: %s", error->message);
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

    /* Open the model file */
    int model_fd = open(MODEL_PATH, O_RDONLY);
    if(model_fd < 0) {
        PANIC("Failed to open model file '%s': %s", MODEL_PATH, strerror(errno));
    }
    *model_fd_out = model_fd;

    /* Get the device handle for our choosen backend */
    const larodDevice* device = larodGetDevice(DEVICE_NAME, &error);
    if(!device) {
        PANIC("Failed to get device '%s': %s", DEVICE_NAME, error->message);
    }

    /* Load the model - this might take a while at first load */
    larodModel* model = larodLoadModel(conn,
                                        model_fd,
                                        device,
                                        LAROD_ACCESS_PRIVATE,
                                        "object detection model",
                                        NULL,
                                        &error);
    if(!model) {
        PANIC("Failed to load model '%s' on device '%s': %s", MODEL_PATH, DEVICE_NAME, error->message);
    }
    syslog(LOG_INFO, "Model loaded successfully on device '%s'", DEVICE_NAME);
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

    larodError* error        = NULL;
    larodTensor** tmp_inputs = NULL;
    size_t num_inputs        = 0;

    tmp_inputs = larodAllocTensors(conn, model, 0, &num_inputs, NULL, &error);
    if(!tmp_inputs || num_inputs == 0) {
        PANIC("Failed to allocate temporary input tensors: %s", error->message);
    }

    /* tmp_input[0] is the input tensor, we can read it's dimentions and layout to understand what the model expects */
    const larodTensorDims = larodGetTensorDims(tmp_inputs[0], &error);
    if(!larodTensorDims || dims->len != 4) {
        PANIC("Unexpected input tensor dimensions: %s", error->message);
    }

    // NHWC layout: dim = [N, W, H, C] and we expect N=1, C=3 (RGB) [batch size is usually 1 for inference, and 3 channels for RGB, width and height are model dependent]
    MODEL_WIDTH = dims->data[2];
    MODEL_HEIGHT = dims->data[1];

    const larodTensorPitches* pitches = larodGetTensorPitches(tmp_inputs[0], &error);
    if(!pitches) {
        PANIC("Failed to get tensor pitches: %s", error->message);
    }
    *pitch_out = pitches->pitches[2]; // pitch for W dimension

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
                                            site_t* num_outputs) {
    larodError* error = NULL;

    // let alrod allocate output tensors with read/write + mmap-able memory 
    // output tensors are allocated by larod and we get their fds, sizes, and mmap them to read the results after inference
    larodTensor** tensors = larodAllocModelOutputs(conn, model, LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP, num_outputs, NULL, &error);

    if(!tensors) {
        PANIC("larodAllocModelOutputs: %s", error->msg);
    }

    // For each output tensor: get its fd, size, and mmap it so we can read the results after inference
    // Without error handling for simplicity !!!
    for(size_t i = 0; i < *num_outputs; i++) {
        out_bufs[i].fd = larodGetTensorFd(tensors[i], &error);
        larodGetTensorFdSize(tensors[i], &out_bufs[i].size, &error);
        // mmap the tensor fd so we can read the results after inference
        out_bufs[i].data = mmap(NULL, out_bufs[i].size, PROT_READ, MAP_SHARED, out_bufs[i].fd, 0);
        syslog(LOG_INFO, "Output tensor %zu: fd=%d, size=%zu bytes", i, out_bufs[i].fd, out_bufs[i].size);
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

static VdoStream* create_new_vdo_stream(bool rgb_backend,
                                    unsigned int* out_w,
                                    unsigned int* out_h,
                                    unsigned int* out_pitch,
                                    unsigned int* out_nbr_bufs,
                                    bool* out_is_dmabuf,
                                    VdoFormat* out_format) {

    g_autoptr(GError) error        = NULL;


    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "channel",         VDO_CHANNEL);
    vdo_map_set_uint32(settings, "buffer.count",    VDO_NUM_BUFFERS);
    vdo_map_set_double(settings, "framerate",       VDO_FRAMERATE);
    vdo_map_set_boolean(settings, "socket.blocking", false);
    vdo_map_set_string(settings, "image.fit", IMAGE_FIT);
    VdoPair32u resolution = { .w = VDO_WIDTH, .h = VDO_HEIGHT };
    vdo_map_set_pair32u(settings, "resolution", resolution);
    
    if(rgb_backend) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_RGB );
    } else {
        
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);  /* NV12 is the most common YUV format and widely supported by VDO */
        vdo_map_set_pair32u(settings, "resolution", resolution);

    }

    // Create a vdo stream using the vdoMap filled in above
    g_autoptr(VdoStream) vdo_stream = vdo_stream_new(vdo_settings, NULL, &error);
    if (!vdo_stream) {
        PANIC("vdo_stream_new: %s", error->message);    
    }
    /* Read back what VDO actually gave us */
    VdoMap* info = vdo_stream_get_info(stream, &error);
    if (!info) {
        PANIC("vdo_stream_get_info: %s", error->message);
    }
    
    *out_format    = vdo_map_get_uint32(info, "format", 0);
    *out_w         = vdo_map_get_uint32(info, "width", 0);
    *out_h         = vdo_map_get_uint32(info, "height", 0);
    *out_pitch     = vdo_map_get_uint32(info, "pitch", 0);
    *out_nbr_bufs  = vdo_map_get_uint32(info, "buffer.count", VDO_NUM_BUFFERS);

    const char* buf_type = vdo_map_get_string(info, "buffer.type", NULL, "memfd");
    *out_is_dmabuf = (g_strcmp0(buf_type, "vmem") != 0);

    syslog(LOG_INFO, "Dump of vdo stream settings map =====");
    vdo_map_dump(vdo_settings);

    g_object_unref(info);

    return g_steal_pointer(&vdo_stream);
}
/* ══════════════════════════════════════════════
 *
 *  STEP 6 — SET UP PREPROCESSING (if needed)
 *  
 *  Two scenarios:
 *  A) Backends supports RGB (e.g. a9-dlpu-tflite) and delivers the expected resolution → no preprocessing needed, 
 *  we can feed VDO buffers directly to the model.
 * 
 *  B) Backend delivers NV12 or delivers a different resolution → we need preprocessing to convert NV12→RGB and/or resize. 
 *  We set up a larod "model" on the cpu-proc device with parameters describing the input (VDO) and output (model) formats, 
 *  and let larod handle the conversion for us.
 *
 * ══════════════════════════════════════════════ */

int main(argc, argv) {
    (void)argc;
    /* local variables */
    larodConnection*        conn                = NULL;
    larodModel*             inf_model           = NULL;
    larodModel*             pp_model            = NULL;
    larodTensor**           inf_outputs         = NULL;
    larodTensor**           pp_outputs          = NULL;
    larodJobRequest*        pp_job_request      = NULL;
    larodJobRequest*        inf_job_request     = NULL;
    size_t                  num_inf_outputs     = 0;
    size_t                  num_pp_outputs      = 0;
    int                     model_fd            = -1;
    unsigned int            model_pitch         = 0;
    output_buf_t            out_buf[4]          = {{.fd=-1, .data=MAP_FAILED},
                                                   {.fd=-1, .data=MAP_FAILED},
                                                   {.fd=-1, .data=MAP_FAILED},
                                                   {.fd=-1, .data=MAP_FAILED}};

    tracked_input_t         tracked[5]          = {0}; /* MAX_NBR_IMG_PROVIDER_BUFFERS = 5 */
    VdoStream*              vdo_stream          = NULL;
    unsigned int            vdo_w, vdo_h, vdo_pitch, vdo_nbrbufs;
    VdoFormat               vdo_format;
    bool                    vdo_is_dmabuf;

    openlog("object_detection_min", LOG_PERROR | LOG_PID, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    syslog(LOG_INFO, "Starting up object detection application");

    /* Step 1: Connect to LAROD */
    conn = larod_connect();

    /* Step 2: Load inference model */
    inf_model = load_inference_model(conn, &model_fd);

    /* Step 3: Read what the model expects as input */
    read_model_input_size(conn, inf_model, &model_pitch);

    /* Step 4: Create + mmap output tensors */
    inf_outputs = create_output_tensors(conn, inf_model, out_bufs, &num_inf_outputs);
    syslog(LOG_INFO, "Model has %zu output tensors", num_inf_outputs);

    /* Step 5: Determine backend capabilities */
    bool rgb_backend = backend_supports_rgb(DEVICE_NAME);

    /* Step 6: Create VDO stream */
    vdo_stream = create_vdo_stream(rgb_backend, &vdo_w, &vdo_h, &vdo_pitch, &vdo_nbr_bufs, &vdo_is_dmabuf, &vdo_format);

}