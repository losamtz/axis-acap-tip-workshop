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

/* We'll read these from the model at runtime */
static unsigned int MODEL_WIDTH  = 0;
static unsigned int MODEL_HEIGHT = 0;


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