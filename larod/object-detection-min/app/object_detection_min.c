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
 *  STEP 5: Check backend capability detection
 *
 *  a9-dlpu-tflite:        accepts RGB directly from VDO
 *  axis-a8-dlpu-tflite:   needs NV12 → RGB preprocessing
 *  
 * ══════════════════════════════════════════════ */

 static bool backend_supports_rgb(const char* device_name) {
    return (strcmp(device_name, "a9-dlpu-tflite") == 0);
 }


/* ══════════════════════════════════════════════
 *
 *  STEP 6 — CREATE VDO STREAM
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
 *  STEP 7 — SET UP PREPROCESSING (if needed)
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

 larodModel* setup_preprocessing(larodConnection* conn, 
                                    VdoFormat vdo_format, 
                                    unsigned int vdo_w, 
                                    unsigned int vdo_h, 
                                    unsigned int vdo_pitch,
                                    unsigned int model_pitch,
                                    larodTensor** pp_outputs_out,
                                    size_t pp_num_outputs) {
    larorError* error = NULL;

    /*
    *  step 1: Determine input format string based on what VDO delivers
    */
    const char* input_format_str;
    switch(vdo_format){
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
            PANIC("Unsupported VDO format: %s", error->msg);
    }

    /* Step 2: Output always what the model needs: RGB interleaved */
    const char* output_format_str = "rgb-interleaved";
    syslog(LOG_INFO, "Setting up preprocessing: input_format=%s %ux%u -> output_format=%s %ux%u", input_format_str, vdo_w, vdo_h, output_format_str, MODEL_WIDTH, MODEL_HEIGHT);

    /* Step 3: Build the parameter map describing input and output formats */
    larodMap* map = larodCreateMap(&error);
    if(!map) PANIC("larodCreateMap: %s", error->msg);

    // Input: what VDO gives us (NV12 / YUV or RGB)
    larodMapSetStr(map, "image.input.format", input_format_str, &error);
    larodMapSetIntArr2(map, "image.input.size", vdo_w, vdo_h, &error);
    larodMapSetInt(map, "image.input.row-pitch", vdo_pitch, &error);

    // Output: what the model needs (RGB interleaved)
    larodMapSetStr(map, "image.outpu.format", output_format_str);
    larodMapSetIntArr2(map, "image.output.size", MODEL_WIDTH, MODEL_WIDTH, &error);
    larodMapSetInt(map, "image.output.row-pitch", model_pitch, &error);

    /* Step 4: Load preprocessing "model" on cpu-proc (no model file → fd = -1) */
    const larodDevice* pp_device = larodGetDevice(conn, PP_DEVICE_NAME, 0, &error);
    larodModel* pp_model = larodLoadModel(conn, -1, pp_device, LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP, pp_num_outputs, NULL, &error);

    if (!pp_model) {
        PANIC("larodLoadModel(preprocessing): %s", error->msg);
    }
    larodDestroyMap(&map);

    /* Step 5: Allocate output tensors for preprocessing */
    *pp_outputs_out = laarodAllocModelOutputs(conn, pp_model, LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP, pp_num_outputs, NULL, &error);
    if(!*pp_outputs_out) {
        PANIC("larodAllocModelOutputs(pp): %s", error->msg);
    }
    syslog(LOG_INFO, "Preprocessing model loaded on %s", PP_DEVICE_NAME);

    return pp_model;

}
/* ══════════════════════════════════════════════
 *
 *  STEP 8 — CREATE INPUT TENSORS FOR VDO BUFFERS
 *
 *  We create one input tensor per VDO buffer.
 *  Each tensor describes the image layout
 *  (NV12, width, height, pitch) and is flagged
 *  for DMA-buf access.
 *
 * ══════════════════════════════════════════════ */

static void create_input_tensors(tracked_input_t tracked
                                    unsigned int vdo_nbr_bufs,
                                    unsigned int vdo_w,
                                    unsigned int vdo_h,
                                    unsigned int vdo_pitch,
                                    VdoFormat vdo_format) {
    larodError* error = NULL;

    // Pick tensor layout to match VDO format (e.g. NV12 → 420SP, RGB → RGB interleaved)
    larodTensorLayout layout;
    switch(vdo_format) {
        case VDO_FORMAT_YUV:        layout = LAROD_TENSOR_LAYOUT_420SP; break;
        case VDO_FORMAT_YUV:        layout = LAROD_TENSOR_LAYOUT_NHWC; break;
        case VDO_FORMAT_YUV:        layout = LAROD_TENSOR_LAYOUT_NCHW; break;
        default: PANIC("Unsupported VDO format: %s", error->msg);
    }

    const char* layout_str;
    switch(layout) {
        case LAROD_TENSOR_LAYOUT_420SP:     layout_str = "420SP (NV12)"; break;
        case LAROD_TENSOR_LAYOUT_NHWC:     layout_str = "NHWC (RGB)"; break;
        case LAROD_TENSOR_LAYOUT_NCHW:     layout_str = "NCHW (planar)"; break;
        default: PANIC("Unsupported tensor layout: %s", error->msg);
    }

    for(unsigned int i = 0; i < vdo_nbr_bufs; i++) {
        tracked[i].vdo_fd    = -1;
        tracked[i].duped_fd  = -1;

        // Create one tensor
        tracked[i].tensors = larodCreateTensors(1, &error);
        if (!tracked[i].tensors) {
            PANIC("larodCreateTensors[%u]: %s", i, error->msg);
        }

        larodTensor* t = tracked[i].tensors[0];

        // Tell larod about the data format
        larodSetTensorDataType(t, LAROD_TENSOR_DATA_TYPE_UINT8, &error);
        larodSetTensorLayout(t, layout, &error);
        larodBuildTensorDims(t, layout, vdo_w, vdo_h, 3, &error);
        larodSetTensorPitches(t, layout, vdo_pitch, vdo_h, 3, &error);
        larodSetTensorFdProps(t, LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
    }
    syslog(LOG_INFO, "Created %u input tensors (NV12 %ux%u)", nbr_bufs, vdo_w, vdo_h);
}
/* ══════════════════════════════════════════════
 *
 *  STEP 10 — TRACK A VDO BUFFER
 *
 *  When we see a VDO buffer fd for the first
 *  time, we dup it, convert vmem→dmabuf if
 *  needed, and register ("track") the tensor
 *  with larod so it knows the memory region.
 *
 * ══════════════════════════════════════════════ */

 static int track_vdo_buffer(larodConnection* conn, 
                                                tracked_input_t tracked, 
                                                unsigned int nbr_bufs, 
                                                VdoBuffer vdo_buf, 
                                                bool is_dmabuf) {
    larodError* error = NULL;
    
    int vdo_fd          = vdo_buffer_get_fd(vdo_buf);
    int64_t vdo_offset  = vdo_buffer_offset(vdo_buf); // tells larod where inside that buffer the real image data starts
    size_t vdo_capacity = vdo_buffer_capacity(vdo_buf);

    // Check if we've already tracked this fd
    for(unsigned int i = 0; i < nbr_bufs; i++) {
        if(tracked[i].vdo_fd == vdo_fd) {
            return(int)i; // already known, reuse this slot, return index/slot to main
        }

        // find the next free slot if vdo_fd is not tracked
        int slot = -1;
        for(unsigned int i = 0; i < nbr_bufs; i++) {
            if(tracked[i].vdo_fd == -1) {
                slot = (int)i;
                break;
            }
        }
        if(slot < 0) {
            PANIC("No free tracking slots (max=%u)", nbr_bufs);
        }

        // Convert vmem → dmabuf if the device doesn't use dmabuf natively
        int buf_fd = vdo_fd;
        if(!is_dmabuf) {
            buf_fd = larodConvertVmemFdToDmabuf(vdo_fd, vdo_offset, &error);
            if(buf_fd == LAROD_INVALID_FD)  PANIC("larodConvertVmemFdToDmabuf: %s", error->msg);

            vdo_offset = 0;
        }

        // Dup the fd so larod can own its own copy
        int duped = dup(buf_fd);
        if(duped < 0) PANIC("dup: %s", strerror(errno));

        // Bind the fd to the tensor and register it with larod
        larodTensor* t = tracked[slot].tensor[0];
        larodTensorFd(t, duped, &error);
        larodSetTensorFdOffset(t, vdo_offset, &error);
        larodTensorFdSize(t, vdo_capacity, &error);
        larodTrackTensor(conn, t, &error);

        tracked[slot].vdo_fd   = vdo_fd;
        tracked[slot].dup_fd   = duped;
        syslog(LOG_INFO, "Tracked VDO buffer slot %d (vdo_fd=%d, duped=%d)", slot, vdo_fd, duped);
        
        return slot;
    }
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
    unsigned int            vdo_w, vdo_h, vdo_pitch, vdo_nbr_bufs;
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

    /* Step 7: Do the preprocessing if needed */
    bool need_pp;

    if(rgb_backend) {
        /* If backend supports RGB, we only need PP if the resolution doesn't match - Most RGB compatible can retrieve the 1:1 but fotr this exaample we will preprocess */
        need_pp = (vdo_w != MODEL_WIDTH || vdo_h != MODEL_HEIGHT);
        if(!need_pp)
            syslog(LOG_INFO, "Preprocessing NO: VDO delivers RGB at the expected resolution %ux%u → no preprocessing needed", vdo_w, vdo_h);
    } else {
        /* If the backend doesn't support RGB and gives us NV12, we always need PP to convert formats */
        need_pp = true;
        syslog(LOG_INFO, "Preprocessing YES: VDO delivers NV12 but model needs RGB → preprocessing needed to convert formats");
    }

    // Load preprocessing model pp_model and allocate ouput tensors (pp_outputs) - get num outputs
    if(need_pp) {
        pp_model = setup_preprocessing(conn, vdo_format, vdo_w, vdo_h, vdo_pitch, model_pitch, &pp_outputs, &pp_num_outputs);
    }

    /* Step 8: Create input tensors (one per VDO buffer) */
    create_input_tensors(tracked, vdo_nbr_bufs, vdo_w, vdo_h, vdo_pitch, vdo_format);

    /* Step 9: Start VDO stream and enter the main loop */
    GError* vdo_error = NULL;
    if(!vdo_start_stream(vdo_stream, &vdo_error)) {
        PANIC("vdo_stream_start: %s", vdo_error->message);
    }

    int poll_fd = vdo_stream_get_fd(vdo_stream, &vdo_error);

    if(poll_fd < 0) {
        PANIC("vdo_stream_get_fd: %s", vdo_error->message);
    }

    struct pollfd pfd = {.fd = poll_fd, .events = POLLIN}
    
    syslog(LOG_INFO, "Entering main inference loop");

    while(running) {
        larodError* error = NULL;

        // Step 9.a: wait for a frame
        int ret;
        do {
            ret = poll(&pfd, 1, -1);
        } while (ret == -1 && erno == EINTR);

        if(ret < 0) PANIC("poll: %s", strerror(errno));

        // Setp 9b: Get VDO buffer
        VdoBuffer* vdo_buf = vdo_stream_get_buffer(vdo_stream, &error);
        if(!vdo_buffer) {
            if(g_error_matche(vdo_error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
                g_clear_error(&vdo_error);
                continue;
            }
            PANIC("vdo_stream_get_buffer: %s", vdo_error->message);
        }

        /* Step 10: Track the buffer (first time setup per VDO fd) */
        int slot = track_vdo_buffer(conn, tracked, vdo_nbr_bufs, vdo_buf, vdo_is_dmabuf);
        larodTensor** input = tracked[slot].tensors;

        /* Step 11: Run preprocessing (if needed) */
        if(need_pp) {
            // Lazy-create preprocessing job request
            if(!pp_job) {
                pp_job = larodCreateJobRequest(pp_model,
                                                input, 1,
                                                pp_outputs,
                                                pp_num_outputs,
                                                NULL,
                                                &error);
                if(!pp_job) PANIC("larodCreateJobRequest(pp): %s", error->msg);
            } else {
                larodSetJobRequestInputs(pp_job, input, 1, &error);
            }

            if(!larodRunJob(conn, pp_job, &error)) PANIC("larodRunJob(pp): %s", error->msg);
        }

        /* Step 12: Run the inference 
        *  Input is either the raw VDO tensor or the PP out
        *  Even though we are requesting the vdo resolution 640x360 as best for the model 300x300
        *  In RGB we could try to get 1:1 300x300 but this is the typical workflow due to 
        *  the varaity of  models and chips
        */
       larodTensor** inf_input      = need_pp ? pp_outputs : input;
       size_t        inf_input_n    = need_pp ? pp_num_outputs : 1

       if(!inf_job) {
            inf_job = larodCreateJobRequest(inf_model,
                                            inf_input, inf_num_n,
                                            inf_outputs, num_inf_outputs,
                                            NULL, &error);
            if(!inf_job) PANIC("larodCreateJobRequest(inf): %s", error->msg);
       }
       // No need to update inputs - they don't change after PP is stable

       if(!larodRunJob(conn, inf_job, &error)) {
            PANIC("larodRunJob(inf): %s", error->msg);
       }

       /* Step 12: Read results */
       if(num_inf_outputs >= 2) {
            uint_t* person = (uint_t*) out_bufs[0].data;
            uint_t* car = (uint_t*) out_bufs[1].data;
            syslog(LOF_INFO,
                    "Person: %.1f%% - Car: %.1f%%",
                    (float)*person / 2.55f,
                    (float)*car / 2.55f);
        }

        // Return VDO buffer
        if(!vdo_stream_buffer_unref(vdo_stream, &vdo_buf, &vdo_error)) {
            if (!vdo_error_is_expected(&vdo_error)) {
                PANIC("buffer_unref: %s", vdo_error->message);
            }
            g_clear_error(&vdo_error);
        }

    }

    /* Step 13: CLEAN UP */
    syslog(LOG_INFO, "Shutting down...");

    // Stop VDO
    if(vdo_stream) {
        vdo_stream_stop(vdo_stream);
        g_object_unref(vdo_stream);
    }

    // Unmap output buffers 
    for(int i = 0; i < 2; i++) {
        if(out_bufs[i].data != MAP_FAILED) munmap(out_bufs[i].data, out_bufs[i].size);
    }

    // Destroy job requests
    larodDestroyJobRequest(&pp_job);
    larodDestroyJobRequest(&inf_job);

    // Destroy tracked input tensors
    larod* cerror = NULL;
    for(unsigned int i = 0; i < VDO_NUM_BUFFERS; i++) {
        if(tracked[i].tensors) {
            larodDestroyTensors(conn, &tracked[i].tensors, 1, &cerror);
        }
        if(tracked[i].duped_fd >=0) close(tracked[i].duped_fd);
    }

    // Destroy output tensors
    if(pp_outputs) larodDestroyTensors(con, &pp_outputs, pp_num_outputs, &cerror);
    if(pp_outputs) larodDestroyTensors(con, &inf_outputs, num_inf_outputs, &cerror);

    // Destroy models
    larodDestroyModel(&pp_model);
    larodDestroyModel(&inf_model);

    // Disconnect
    larodDisconnect(&conn, &cerror);
    larodClearError(&cerror);

    // Close model file
    if(model_fd >= 0) close(model_fd);

    syslog(LOG_INFO, "========== vdo_larod_min exited ==========");
    closelog();
    return EXIT_SUCCESS;


}