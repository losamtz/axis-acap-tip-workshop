# Simple VDO-Larod minimized Example

A minimal but production-realistic example of running ML inference on an
Axis camera using the **VDO** (Video Data Output) and **larod** (ML runtime) APIs.

## What Does It Do?

Captures live video frames from the camera sensor, preprocesses them
(resize + color convert), runs a person/car classification model, and
prints the confidence percentages to syslog.

Camera Sensor → VDO Frame → Preprocessing → Inference → "Person: 82% — Car: 3%"

## Who Is This For?

Developers new to ACAP Native SDK who want to understand the **VDO → larod
pipeline** without getting lost in production error handling, dynamic
configuration, and multi-platform support code.

---

## Architecture



## Flow
```
1.  larodConnect()                                 Connect to larod daemon
2.  larodGetDevice() + larodLoadModel()            Load .tflite on the DLPU
3.  larodAllocModelInputs()                        Read model's expected input size
4.  larodAllocModelOutputs() + mmap                Create output buffers we can read
5.  vdo_stream_new()                               Open camera stream (NV12)
6.  If VDO size ≠ model size:
       larodCreateMap() → larodLoadModel(fd=-1)    Create resize pipeline
       larodAllocModelOutputs()                    PP output = inference input
7.  larodCreateTensors()                           One input tensor per VDO buffer + set data type, layout, dims, pitches, fd props
8.  Loop:
       poll()                                      Wait for frame
       vdo_stream_get_buffer()                     Get frame
       dup(fd) + larodTrackTensor()                First-time: register buffer with larod
       larodRunJob(pp)                             Preprocess (if needed)
       larodRunJob(inf)                            Inference
       Read mmap'd output                          Person: X% — Car: Y%
       vdo_stream_buffer_unref()                   Return buffer to VDO
 9.  Cleanup: destroy jobs, tensors, models, disconnect
```
## Flow


### Step 1 - CONNECT TO LAROD SERVICE
Opens a session with the larod daemon. All subsequent larod calls use this connection handle. One connection per application.

```c
larodConnection* conn = NULL;
larodConnect(&conn, &error)
```

### Step 2 - GET LAROD DEVICE BACKEND & LOAD THE MODEL

```c
// Create larod models
// model_file can be downloaded into the app through dockerfile which can live in /usr/local/packages/vdo_larod/model.tflite
// curl -o model.tflite https://acap-ml-models.s3.amazonaws.com/tensorflow_to_larod_resnet/custom_resnet_artpec8_car_human_256.tflite ;
// acap-build . -a 'model.tflite';
int larod_model_fd = open(model_file, O_RDONLY);
```

```c
// device_name can be retrieved with larodGetDevices or specifying as argumentin manifest or hardcoded: a9-dlpu-tflite | axis-a8-dlpu-tflite
const larodDevice* device = larodGetDevice(conn, "a9-dlpu-tflite", 0, &error);
```


```c
larodModel* model = larodLoadModel(conn,
                                    larod_model_fd,
                                    device,
                                    LAROD_ACCESS_PRIVATE, // only this application can use the loaded model
                                    "Vdo larod model",
                                    NULL,
                                    &error);
```

Summary:
- `larodGetDevice` — selects which hardware backend to use
- `larodLoadModel` — loads the .tflite file onto that backend
- `LAROD_ACCESS_PRIVATE` — only this application can use the loaded model

### Step 3: SETUP TEMPORARY INPUT TENSORS - Get Model information and input dimentions

We allocate temporary input tensors just to query the model's expected input size and pitch, then destroy them. This lets the code adapt to any model without hardcoding dimensions.

Pitches are the byte offsets to move to the next element in each dimension. For NHWC, we expect pitch for W to be 3 (for RGB), and pitch for H to be width * 3, and pitch for N to be height * width * 3. We can read the pitch for the W dimension to understand how the model expects the data to be laid out in memory.

```c
size_t num_inputs           = 0;
larodTensor** tmp_input_tensors = larodAllocModelInputs(conn, model, 0, &num_inputs, NULL, &error);
```
```c
const larodTensorDim* dims = larodGetTensorDims(tmp_input_tensors[0], &error);
// dims = [1, 256, 256, 3] -> batch=1, H=256, W=256, channels=3
```
```c
const larodTensorPitches* pitches = larodGetTensorPitches(tmp_inputs[0], &error);
*pitch_out = pitches->pitches[2];  // pitch for W dimension
```

### Step 4: CREATE OUTPUT TENSORS and mmap - Will be used for the inference job request
Larod allocates the output memory. We mmap each tensor's file descriptor so we can read the inference results directly from memory after each larodRunJob call.

```c
size_t num_outputs           = 0;
larodTensor** output_tensors = larodAllocModelOutputs(conn,
                                                        model,
                                                        LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                                        num_outputs,
                                                        NULL,
                                                        &error);
```
mmap

```c
// For each output tensor: get its fd, size, and mmap it so we can read the results after inference
int fd      = larodGetTensorFd(output_tensors[i], &error);
size_t size = ...;  // from larodGetTensorFdSize
void* data  = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
```

### Step 5 — Determine if preprocessing is needed
```yml
┌─────────────────────────────────────────────────────┐
│  Does the backend accept RGB directly?              │
│                                                     │
│  YES (a9-dlpu-tflite):                              │
│    → Request RGB from VDO at model resolution       │
│    → If VDO delivers exact size: skip preprocessing │
│    → If VDO gives different size: resize RGB→RGB    │
│                                                     │
│  NO (axis-a8-dlpu-tflite, cpu-tflite, etc.):        │
│    → Request NV12 from VDO                          │
│    → ALWAYS preprocess: NV12→RGB + resize           │
└─────────────────────────────────────────────────────┘
```

### Step 6 — Set up preprocessing (if needed)

Preprocessing is itself a larod model — loaded with fd = -1 (no model file):

```c
// Describe input (what VDO gives us)
larodMapSetStr(map, "image.input.format", "nv12", &error);
larodMapSetIntArr2(map, "image.input.size", vdo_w, vdo_h, &error);
larodMapSetInt(map, "image.input.row-pitch", vdo_pitch, &error);


// Describe output (what the model needs)
larodMapSetStr(map, "image.output.format", "rgb-interleaved", &error);
larodMapSetIntArr2(map, "image.output.size", MODEL_WIDTH, MODEL_HEIGHT, &error);
larodMapSetInt(map, "image.output.row-pitch", model_pitch, &error);

// Load on cpu-proc (libyuv-based image processing)
larodModel* pp = larodLoadModel(conn, -1, pp_device, LAROD_ACCESS_PRIVATE, "", map, &error);
```

The preprocessing output tensors become the inference input tensors — zero-copy.

### Step 7 — Create input tensors for VDO buffers

Input tensors describe the VDO frame layout so larod knows how to read the image data. They are **always created manually** with
`larodCreateTensors`, regardless of format or whether preprocessing is used. These same tensors serve as input to either the preprocessing model or the inference model directly.

The tensor layout must match what VDO delivers:

**NV12 (YUV) input — e.g. `axis-a8-dlpu-tflite`:**

```c
larodTensor** t = larodCreateTensors(1, &error);
larodSetTensorDataType(t[0], LAROD_TENSOR_DATA_TYPE_UINT8, &error);
larodSetTensorLayout(t[0], LAROD_TENSOR_LAYOUT_420SP, &error);     // NV12
larodBuildTensorDims(t[0], LAROD_TENSOR_LAYOUT_420SP, w, h, 3, &error);
larodBuildTensorPitches(t[0], LAROD_TENSOR_LAYOUT_420SP, pitch, h, 3, &error);
larodSetTensorFdProps(t[0], LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
```

**RGB interleaved input — e.g. a9-dlpu-tflite:**


```c
larodTensor** t = larodCreateTensors(1, &error);
larodSetTensorDataType(t[0], LAROD_TENSOR_DATA_TYPE_UINT8, &error);
larodSetTensorLayout(t[0], LAROD_TENSOR_LAYOUT_NHWC, &error);     // RGB interleaved
larodBuildTensorDims(t[0], LAROD_TENSOR_LAYOUT_NHWC, w, h, 3, &error);
larodBuildTensorPitches(t[0], LAROD_TENSOR_LAYOUT_NHWC, pitch, h, 3, &error);
larodSetTensorFdProps(t[0], LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
```
**Planar RGB input — e.g. ambarella-cvflow:**

```c
larodTensor** t = larodCreateTensors(1, &error);
larodSetTensorDataType(t[0], LAROD_TENSOR_DATA_TYPE_UINT8, &error);
larodSetTensorLayout(t[0], LAROD_TENSOR_LAYOUT_NCHW, &error);
larodBuildTensorDims(t[0], LAROD_TENSOR_LAYOUT_NCHW, w, h, 3, &error);
larodBuildTensorPitches(t[0], LAROD_TENSOR_LAYOUT_NCHW, pitch, h, 3, &error);
larodSetTensorFdProps(t[0], LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);
```
The layout must match what VDO delivers, otherwise larod will misinterpret the pixel data. The code handles this automatically
via a switch on vdo_format:

```c
switch (vdo_format) {
    case VDO_FORMAT_YUV:        layout = LAROD_TENSOR_LAYOUT_420SP; break;
    case VDO_FORMAT_RGB:        layout = LAROD_TENSOR_LAYOUT_NHWC;  break;
    case VDO_FORMAT_PLANAR_RGB: layout = LAROD_TENSOR_LAYOUT_NCHW;  break;
}
```

Why always manual? These tensors describe VDO frame memory, not model expectations. The same tensor is used as input to either the
preprocessing model or the inference model. When preprocessing is skipped, it works because VDO already delivers frames matching the
model's expected format, size, and pitch exactly.

### Step 8 — Track VDO buffers (DMA-buf zero-copy)

This is the most hardware-specific part. VDO gives you frames as file descriptors. For larod to access this memory efficiently:

```c
// 1. Get fd from VDO buffer
int vdo_fd = vdo_buffer_get_fd(vdo_buf);

// 2. Convert vmem → dmabuf if needed (some SoCs use vmem)
int buf_fd = larodConvertVmemFdToDmabuf(vdo_fd, offset, &error);

// 3. Dup the fd (larod needs its own copy)
int duped = dup(buf_fd);

// 4. Bind to tensor and register
larodSetTensorFd(tensor, duped, &error);
larodSetTensorFdOffset(tensor, offset, &error);
larodSetTensorFdSize(tensor, capacity, &error);
larodTrackTensor(conn, tensor, &error);
```
This happens once per VDO buffer (typically 2 buffers). After tracking, the tensor is reused automatically for every subsequent frame from that buffer.

### Step 9 — Main loop: poll → preprocess → infer → read

```c
while (running) {
    poll(&pfd, 1, -1);                              // Wait for frame
    VdoBuffer* buf = vdo_stream_get_buffer(...);     // Get frame

    track_vdo_buffer(conn, tracked, ...);            // First-time: register

    if (need_pp) {
        larodRunJob(conn, pp_job, &error);           // Preprocess
    }

    larodRunJob(conn, inf_job, &error);              // Inference

    uint8_t* person = (uint8_t*)out_bufs[0].data;   // Read mmap'd output
    uint8_t* car    = (uint8_t*)out_bufs[1].data;
    syslog(LOG_INFO, "Person: %.1f%% — Car: %.1f%%",
           *person / 2.55f, *car / 2.55f);

    vdo_stream_buffer_unref(stream, &buf, ...);      // Return buffer
}
```

Job requests are created once (lazily on first frame) and reused for all subsequent frames. Only the input tensors underlying data changes (VDO fills the same buffer with new frame data).

### Step 10 — Cleanup

Resources are freed in reverse order of creation:

```lua
VDO stream → output mmaps → job requests → tracked tensors →
PP tensors → inference tensors → models → connection → file descriptors
```

### Diagram

```less
    ┌──────────────────────────────────────────────────────────┐
    │                    CAMERA SENSOR                         │
    └──────────────┬───────────────────────────────────────────┘
                   │
                   ▼
    ┌──────────────────────────────────────────────────────────┐
    │  VDO Stream (poll + vdo_stream_get_buffer)               │
    │                                                          │
    │  Path A (A9): RGB at model resolution (e.g. 256×256)     │
    │  Path B (A8): NV12 (YUV) at model resolution             │
    │                                                          │
    │  Buffers: 2 (rotating)                                   │
    │  image.fit: "scale"                                      │
    └──────────────┬───────────────────────────────────────────┘
                   │
                   │  vdo_buffer_get_fd() → file descriptor
                   │
                   ▼
    ┌──────────────────────────────────────────────────────────┐
    │  TENSOR TRACKING (first time only per buffer)            │
    │                                                          │
    │  dup(fd) → larodSetTensorFd → larodTrackTensor           │
    │                                                          │
    │  After this, larod knows where the image data lives.     │
    └──────────────┬───────────────────────────────────────────┘
                   │
              ┌────┴────┐
              │ need_pp?│
              └────┬────┘
         ┌──YES───┘└───NO──┐
         │                 │
         ▼                 ▼
    ┌──────────────┐   ┌──────────────────────────────────┐
    │ PREPROCESSING│   │  DIRECT TO INFERENCE             │
    │ (cpu-proc)   │   │                                  │
    │              │   │  Path A (A9): VDO delivers RGB   │
    │ Path B (A8): │   │  at model resolution — no        │
    │ NV12 → RGB   │   │  conversion needed.              │
    │ + resize     │   │                                  │
    │              │   │  Input tensors created via       │
    │ Path A (A9): │   │  larodAllocModelInputs to match  │
    │ RGB → RGB    │   │  model dims exactly [1,256,256,3]│
    │ resize only  │   │                                  │
    └──────┬───────┘   └───────────────┬──────────────────┘
           │                           │
           └───────────┬───────────────┘
                       │
                       ▼
    ┌──────────────────────────────────────────────────────────┐
    │  INFERENCE (larodRunJob)                                 │
    │                                                          │
    │  Backend: a9-dlpu-tflite (A9) or axis-a8-dlpu-tflite (A8)│
    │  Input:  RGB 256×256 NHWC [1,256,256,3]                  │
    │  Output: 2 × uint8                                       │
    │          [0] = person confidence (0–255)                 │
    │          [1] = car confidence    (0–255)                 │
    └──────────────┬───────────────────────────────────────────┘
                   │
                   │  mmap'd memory — just read it
                   │
                   ▼
    ┌──────────────────────────────────────────────────────────┐
    │  RESULT                                                  │
    │                                                          │
    │  person_pct = output[0] / 2.55                           │
    │  car_pct    = output[1] / 2.55                           │
    │                                                          │
    │  syslog: "Person: 82.4% — Car: 3.1%"                     │
    └──────────────────────────────────────────────────────────┘
```

## Larod API Reference (functions used in this example)

### Connection

| Function | Purpose |
|:---------|:--------|
| `larodConnect` | Open session with larod daemon |
| `larodDisconnect` | Close session |

### Device & Model

| Function | Purpose |
|:---------|:--------|
| `larodGetDevice` | Get handle for a named backend (e.g. `"a9-dlpu-tflite"`) |
| `larodLoadModel` | Load a model file onto a device (`fd=-1` for preprocessing) |
| `larodDestroyModel` | Unload a model |

### Tensors — Allocation

| Function | Purpose |
|:---------|:--------|
| `larodAllocModelInputs` | Allocate input tensors matching model's expected dimensions |
| `larodAllocModelOutputs` | Allocate output tensors (larod allocates the memory) |
| `larodCreateTensors` | Create empty tensors (you configure dims manually) |
| `larodDestroyTensors` | Free tensor handles |

### Tensors — Configuration

| Function | Purpose |
|:---------|:--------|
| `larodSetTensorDataType` | Set data type (e.g. `UINT8`) |
| `larodSetTensorLayout` | Set layout (`NHWC`, `NCHW`, `420SP`) |
| `larodBuildTensorDims` | Build width/height/channels for a layout |
| `larodBuildTensorPitches` | Build row stride for a layout |
| `larodSetTensorFdProps` | Set memory properties (`DMABUF`, `MAP`) |

### Tensors — Buffer Binding

| Function | Purpose |
|:---------|:--------|
| `larodSetTensorFd` | Bind a file descriptor to a tensor |
| `larodSetTensorFdOffset` | Set byte offset within the fd |
| `larodSetTensorFdSize` | Set usable byte size |
| `larodTrackTensor` | Register tensor memory with larod |
| `larodGetTensorFd` | Read back a tensor's fd |
| `larodGetTensorFdSize` | Read back a tensor's byte size |

### Tensors — Introspection

| Function | Purpose |
|:---------|:--------|
| `larodGetTensorDims` | Read tensor dimensions |
| `larodGetTensorPitches` | Read tensor pitches (row strides) |

### Preprocessing Map

| Function | Purpose |
|:---------|:--------|
| `larodCreateMap` | Create key-value config map |
| `larodMapSetStr` | Set string (e.g. `"image.input.format"`, `"nv12"`) |
| `larodMapSetInt` | Set integer (e.g. `"image.input.row-pitch"`) |
| `larodMapSetIntArr2` | Set width+height pair (e.g. `"image.input.size"`) |
| `larodDestroyMap` | Free map |

### Job Execution

| Function | Purpose |
|:---------|:--------|
| `larodCreateJobRequest` | Package model + input/output tensors into a runnable job |
| `larodSetJobRequestInputs` | Update inputs on an existing job (reuse) |
| `larodRunJob` | Execute inference (blocks until complete) |
| `larodDestroyJobRequest` | Free job request |

### Utility

| Function | Purpose |
|:---------|:--------|
| `larodConvertVmemFdToDmabuf` | Convert vmem fd to DMA-buf fd |
| `larodClearError` | Free error struct |