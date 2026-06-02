# Minimal VDO stream and Larod based ACAP application running inference on an edge device

## Flow


CONNECT TO LAROD SERVICE
s
```c
larodConnection* conn = NULL;
larodConnect(conn, &error)
```

### GET LAROD MODEL FD
```c
// Create larod models
// model_file can be downloaded into the app through dockerfile which can live in /usr/local/packages/vdo_larod/model.tflite
// curl -o model.tflite https://acap-ml-models.s3.amazonaws.com/tensorflow_to_larod_resnet/custom_resnet_artpec8_car_human_256.tflite ;
// acap-build . -a 'model.tflite';
int larod_model_fd = open(model_file, O_RDONLY);
```

### GET LAROD DEVICE BACKEND
```c
// device_name can be retrieved with larodGetDevices or specifying as argumentin manifest or hardcoded: a9-dlpu-tflite | axis-a8-dlpu-tflite
const larodDevice* device = larodGetDevice(conn, device_name, 0, &error);
```

### LOAD THE MODEL
```c
larodModel* model = larodLoadModel(conn,
                                    larod_model_fd,
                                    device,
                                    LAROD_ACCESS_PRIVATE,
                                    "Vdo larod model",
                                    NULL,
                                    &error);
```

### SETUP TEMPORARY INPUT TENSORS - Get Model information
```c
size_t num_inputs           = 0;
larodTensor** input_tensors = larodAllocModelInputs(conn, model, 0, num_inputs, NULL, &error);
```

### SETUP OUTPUT TENSORS - Will be used for the inference job request
```c
size_t num_outputs           = 0;
larodTensor** output_tensors = larodAllocModelOutputs(conn,
                                                        model,
                                                        LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                                        num_outputs,
                                                        NULL,
                                                        &error);
```

### GET INPUT MODEL RESOLUTION 
```c
const larodTensorDims* input_dims = larodGetTensorDims(input_tensors[0], &error);
VdoFormat model_img_format = VDO_FORMAT_RGB;; // for artpec 8 & 9
unsigned int model_input_img_width = input_dims->dims[2]
unsigned int model_input_img_heoght = input_dims->dims[1]
```

### GET INPUT MODEL PITCHES
```c
const larodTensorPitches* input_pitches = larodGetTensorPitches(input_tensors[0], &error);
unsigned int pitch = input_pitches->pitches[2];
```

### GET DATA FROM OUTPUT TENSORS
```c
// To be able to get the data from the output tensors get the fd and mmap the memory

typedef struct model_tensor_output {
    int fd;
    void* data;
    size_t size;
    larodTensorDataType datatype;
    uint64_t timestamp;
} model_tensor_output_t;

model_tensor_output_t model_tensor_output = calloc(num_outputs, sizeof(model_tensor_output_t));

for (size_t i = 0; i < num_outputs; i++) {

    int fd = larodGetTensorFd(output_tensors[i], &error);

    size_t output_size           = 0;
    void* data                   = NULL;
    larodTensorDataType datatype = LAROD_TENSOR_DATA_TYPE_INVALID;

    larodGetTensorFdSize(output_tensors[i], &output_size, &error);
    data = mmap(NULL, output_size, PROT_READ, MAP_SHARED, fd, 0);
    datatype = larodGetTensorDataType(output_tensors[i], &error);
    
    model_output_tensors[i].fd = fd;

    larodGetTensorFdSize(output_tensors[i], &output_size, &error);
    model_output_tensors[i].size = output_size;
    data = mmap(NULL, output_size, PROT_READ, MAP_SHARED, fd, 0);

    model_output_tensors[i].data = data;

    datatype = larodGetTensorDataType(output_tensors[i], &error);
    model_output_tensors[i].datatype = datatype;

}
larodDestroyTensors(conn, &input_tensors, num_inputs, &error);
```
