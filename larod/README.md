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
    │              │   │                                  │
    │ Path A (A9): │   │                                  │
    │ RGB → RGB    │   │                                  │
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

# Larod functions Flow
```less
══════════════════════════════════════════════════════════════════════════
                    LAROD FUNCTIONS — FD & DATA FLOW
══════════════════════════════════════════════════════════════════════════


╔══════════════════════════════════════════════════════════════════════╗
║  1. CONNECTION                                                       ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodConnect(&conn)                                                 ║
║       │                                                              ║
║       └──▶ conn (larodConnection*)                                   ║
║            Used by ALL subsequent larod calls                        ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  2. LOAD MODELS                                                      ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  model.tflite ──▶ open() ──▶ model_fd (int)                          ║
║                                  │                                   ║
║                                  ▼                                   ║
║  larodGetDevice(conn, "a9-dlpu-tflite") ──▶ device (larodDevice*)    ║
║                                                │                     ║
║                                                ▼                     ║
║  larodLoadModel(conn, model_fd, device) ──▶ model (larodModel*)      ║
║                                                                      ║
║  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─         ║
║                                                                      ║
║  larodGetDevice(conn, "cpu-proc") ──▶ pp_device (larodDevice*)       ║
║                                            │                         ║
║                 pp_map ───────────────────▶│                         ║
║                 (format, size, pitch)      │                         ║
║                                            ▼                         ║
║  larodLoadModel(conn, -1, pp_device) ──▶ pp_model (larodModel*)      ║
║                        ^^                                            ║
║                   fd=-1 means                                        ║
║                   no model file                                      ║
║                   (built-in pipeline)                                ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  3. PREPROCESSING MAP (input to larodLoadModel for PP)               ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodCreateMap() ──▶ pp_map (larodMap*)                             ║
║       │                                                              ║
║       ├── larodMapSetStr("image.input.format", "nv12" | "rgb-...")   ║
║       ├── larodMapSetIntArr2("image.input.size", vdo_w, vdo_h)       ║
║       ├── larodMapSetInt("image.input.row-pitch", vdo_pitch)         ║
║       ├── larodMapSetStr("image.output.format", "rgb-interleaved")   ║
║       ├── larodMapSetIntArr2("image.output.size", model_w, model_h)  ║
║       └── larodMapSetInt("image.output.row-pitch", model_pitch)      ║
║                                                                      ║
║  Describes: VDO frame ──▶ model input conversion                     ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  4. READ MODEL METADATA (temporary tensors)                          ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodAllocModelInputs(conn, model) ──▶ tmp_in (larodTensor**)       ║
║       │                                                              ║
║       ├── larodGetTensorDims(tmp_in[0])                              ║
║       │       └──▶ dims = [1, 256, 256, 3]                           ║
║       │                    B   H    W   C                            ║
║       │                         │    │                               ║
║       │                    model_h  model_w                          ║
║       │                                                              ║
║       ├── larodGetTensorPitches(tmp_in[0])                           ║
║       │       └──▶ pitches[2] = model_pitch (e.g. 768)               ║
║       │                                                              ║
║       └── larodDestroyTensors(tmp_in)   ← destroyed after reading    ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  5. OUTPUT TENSORS (larod allocates memory, you mmap to read)        ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodAllocModelOutputs(conn, model) ──▶ out_tensors (larodTensor**) ║
║       │                                                              ║
║       │   For each output tensor [0] and [1]:                        ║
║       │                                                              ║
║       ├── larodGetTensorFd(out_tensors[i])                           ║
║       │       └──▶ out_fd (int)                                      ║
║       │                                                              ║
║       ├── larodGetTensorFdSize(out_tensors[i])                       ║
║       │       └──▶ out_size (size_t)                                 ║
║       │                                                              ║
║       └── mmap(out_fd, out_size) ──▶ out_data[i] (void*)             ║
║                                                                      ║
║       ┌──────────────────────────────────────────┐                   ║
║       │  out_data[0] ──▶ person confidence byte  │                   ║
║       │  out_data[1] ──▶ car confidence byte     │                   ║
║       └──────────────────────────────────────────┘                   ║
║       These pointers stay valid for the lifetime                     ║
║       of the tensors. Just read after each larodRunJob.              ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  6. PP OUTPUT TENSORS (= inference input when PP is used)            ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodAllocModelOutputs(conn, pp_model) ──▶ pp_out (larodTensor**)   ║
║                                                                      ║
║  These tensors hold the preprocessed RGB image.                      ║
║  They are passed directly as inference input — zero copy.            ║
║                                                                      ║
║  ┌─────────────────────────────────────────────────────┐             ║
║  │  pp_out ──▶ RGB 256×256 ──▶ inference input         │             ║
║  └─────────────────────────────────────────────────────┘             ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  7. VDO INPUT TENSORS (always manual, one per VDO buffer)            ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  For each VDO buffer (i = 0, 1):                                     ║
║                                                                      ║
║  larodCreateTensors(1) ──▶ vdo_tensors[i] (larodTensor**)            ║
║       │                                                              ║
║       ├── larodSetTensorDataType(UINT8)                              ║
║       ├── larodSetTensorLayout(420SP | NHWC | NCHW)                  ║
║       ├── larodBuildTensorDims(layout, vdo_w, vdo_h, 3)              ║
║       ├── larodBuildTensorPitches(layout, vdo_pitch, vdo_h, 3)       ║
║       └── larodSetTensorFdProps(DMABUF | MAP)                        ║
║                                                                      ║
║  These tensors have NO fd yet — that happens during tracking.        ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  8. TENSOR TRACKING (binds VDO buffer fd to tensor, once per buf)    ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  VdoBuffer                                                           ║
║       │                                                              ║
║       ├── vdo_buffer_get_fd()      ──▶ vdo_fd (int)                  ║
║       ├── vdo_buffer_get_offset()  ──▶ offset (int64_t)              ║
║       └── vdo_buffer_get_capacity()──▶ capacity (size_t)             ║
║                                                                      ║
║                  vdo_fd                                              ║
║                    │                                                 ║
║                    ▼                                                 ║
║               dup(vdo_fd) ──▶ duped_fd (int)                         ║
║                    │                                                 ║
║                    ▼                                                 ║
║  larodSetTensorFd(tensor, duped_fd)                                  ║
║  larodSetTensorFdOffset(tensor, offset)                              ║
║  larodSetTensorFdSize(tensor, capacity)                              ║
║  larodTrackTensor(conn, tensor)                                      ║
║                                                                      ║
║  ┌─────────────────────────────────────────────────────────┐         ║
║  │  After tracking, larod knows:                           │         ║
║  │    • WHERE the image data is    (duped_fd)              │         ║
║  │    • WHERE it starts            (offset)                │         ║
║  │    • HOW BIG the buffer is      (capacity)              │         ║
║  │    • WHAT FORMAT it is          (from step 7)           │         ║
║  │                                                         │         ║
║  │  This only happens ONCE per VDO buffer.                 │         ║
║  │  VDO reuses the same 2 buffers — so 2 track calls max.  │         ║
║  └─────────────────────────────────────────────────────────┘         ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  9. JOB REQUESTS + EXECUTION (per frame)                             ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  WITH PREPROCESSING:                                                 ║
║  ──────────────────                                                  ║
║                                                                      ║
║  vdo_tensors[slot]          pp_out                                   ║
║  (VDO frame fd)        (PP output fd)                                ║
║       │                      │                                       ║
║       ▼                      ▼                                       ║
║  ┌─────────────────────────────────────────────────┐                 ║
║  │ pp_job = larodCreateJobRequest(                 │                 ║
║  │              pp_model,                          │                 ║
║  │              vdo_tensors[slot], 1,   ← input    │                 ║
║  │              pp_out, pp_num_out,     ← output   │                 ║
║  │              NULL)                              │                 ║
║  │                                                 │                 ║
║  │ larodRunJob(conn, pp_job)                       │                 ║
║  └──────────────────────┬──────────────────────────┘                 ║
║                         │                                            ║
║                    pp_out now contains                               ║
║                    RGB 256×256 data                                  ║
║                         │                                            ║
║       pp_out            │           out_tensors                      ║
║  (PP output = inf input)│      (inference output)                    ║
║       │                 │              │                             ║
║       ▼                 ▼              ▼                             ║
║  ┌─────────────────────────────────────────────────┐                 ║
║  │ inf_job = larodCreateJobRequest(                │                 ║
║  │               model,                            │                 ║
║  │               pp_out, pp_num_out,    ← input    │                 ║
║  │               out_tensors, num_out,  ← output   │                 ║
║  │               NULL)                             │                 ║
║  │                                                 │                 ║
║  │ larodRunJob(conn, inf_job)                      │                 ║
║  └──────────────────────┬──────────────────────────┘                 ║
║                         │                                            ║
║                         ▼                                            ║
║                    out_data[0] ──▶ person byte                       ║
║                    out_data[1] ──▶ car byte                          ║
║                                                                      ║
║  ═══════════════════════════════════════════════════                 ║
║                                                                      ║
║  WITHOUT PREPROCESSING:                                              ║
║  ──────────────────────                                              ║
║                                                                      ║
║  vdo_tensors[slot]                    out_tensors                    ║
║  (VDO frame fd)                  (inference output)                  ║
║       │                                │                             ║
║       ▼                                ▼                             ║
║  ┌─────────────────────────────────────────────────┐                 ║
║  │ inf_job = larodCreateJobRequest(                │                 ║
║  │               model,                            │                 ║
║  │               vdo_tensors[slot], 1,  ← input    │                 ║
║  │               out_tensors, num_out,  ← output   │                 ║
║  │               NULL)                             │                 ║
║  │                                                 │                 ║
║  │ larodRunJob(conn, inf_job)                      │                 ║
║  └──────────────────────┬──────────────────────────┘                 ║
║                         │                                            ║
║                         ▼                                            ║
║                    out_data[0] ──▶ person byte                       ║
║                    out_data[1] ──▶ car byte                          ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  10. JOB REUSE (subsequent frames)                                  ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  Jobs are created ONCE (lazily on first frame).                      ║
║  On subsequent frames, only the input is updated:                    ║
║                                                                      ║
║  larodSetJobRequestInputs(pp_job, vdo_tensors[slot], 1)              ║
║                                                                      ║
║  The output tensors never change — same mmap'd memory.               ║
║  larodRunJob writes new results into the same out_data pointers.     ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  11. READING RESULTS (after each larodRunJob)                        ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  ┌────────────────────────────────────────────────────────┐          ║
║  │                                                        │          ║
║  │   out_fd[0] ◄──mmap──▶ out_data[0] ──▶ uint8_t byte    │          ║
║  │                                         │              │          ║
║  │                                    person / 2.55       │          ║
║  │                                         │              │          ║
║  │                                    "Person: 82.4%"     │          ║
║  │                                                        │          ║
║  │   out_fd[1] ◄──mmap──▶ out_data[1] ──▶ uint8_t byte    │          ║
║  │                                         │              │          ║
║  │                                    car / 2.55          │          ║
║  │                                         │              │          ║
║  │                                    "Car: 3.1%"         │          ║
║  │                                                        │          ║
║  └────────────────────────────────────────────────────────┘          ║
║                                                                      ║
║  No copy needed — mmap'd memory is updated in-place by larod.        ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
       │
       ▼
╔══════════════════════════════════════════════════════════════════════╗
║  12. CLEANUP (reverse order)                                         ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  larodDestroyJobRequest(&pp_job)                                     ║
║  larodDestroyJobRequest(&inf_job)                                    ║
║  larodDestroyTensors(vdo_tensors[i])     ← input tensors             ║
║  close(duped_fds[i])                     ← dup'd VDO fds             ║
║  larodDestroyTensors(pp_out)             ← PP output tensors         ║
║  larodDestroyTensors(out_tensors)        ← inference output tensors  ║
║  larodDestroyModel(&pp_model)                                        ║
║  larodDestroyModel(&model)                                           ║
║  larodDisconnect(&conn)                                              ║
║  close(model_fd)                         ← model file fd             ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝


══════════════════════════════════════════════════════════════════════════
  COMPLETE FD MAP — every file descriptor in the application
══════════════════════════════════════════════════════════════════════════

  ┌──────────────┬──────────────────────┬─────────────────────────────┐
  │ FD           │ Source               │ Used by                     │
  ├──────────────┼──────────────────────┼─────────────────────────────┤
  │ model_fd     │ open(model.tflite)   │ larodLoadModel              │
  │ vdo_fd[0]    │ VDO buffer 0         │ dup → tensor tracking       │
  │ vdo_fd[1]    │ VDO buffer 1         │ dup → tensor tracking       │
  │ duped_fd[0]  │ dup(vdo_fd[0])       │ larodSetTensorFd            │
  │ duped_fd[1]  │ dup(vdo_fd[1])       │ larodSetTensorFd            │
  │ pp_out_fd    │ larod (allocated)    │ PP output / inference input │
  │ out_fd[0]    │ larod (allocated)    │ mmap → person result        │
  │ out_fd[1]    │ larod (allocated)    │ mmap → car result           │
  └──────────────┴──────────────────────┴─────────────────────────────┘

  ```less

### memfd vs vmem

  ```less
┌─────────────────────────────────────────────────────────────────┐
│  "memfd" / DMA-buf (dmabuf = true)                              │
│                                                                 │
│  • Modern Linux shared memory                                   │
│  • File descriptor directly points to the frame data            │
│  • Larod can access it directly via the fd                      │
│  • Used on: ARTPEC-9 and newer SoCs                             │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│  "vmem" (dmabuf = false)                                        │
│                                                                 │
│  • Axis proprietary video memory allocator                      │
│  • The fd points to a vmem region — NOT directly usable         │
│    by larod as-is                                               │
│  • Must be converted to DMA-buf first                           │
│  • Used on: older SoCs (ARTPEC-7, some ARTPEC-8 configs)        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
  ```