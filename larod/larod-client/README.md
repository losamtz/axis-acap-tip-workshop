# Larod client tool

larod-client is a small program that uses the larod API in the same way as a typical application would do. Command line options and an interactive mode makes it easy to interact with the larod service. Some use case examples are listed in this file; please refer to the tool help (--help option) and ultimately the source code for nitty-gritty-details.

## Description

Before starting this tutorial, we would need to configure camera developer mode ["Developer mode instructions"](https://developer.axis.com/acap/get-started/set-up-developer-environment/set-up-device-advanced/) 

In this example we will work with the larod client tool accessing it through ssh. 

We will:

1. Build and install a dummy app to be able to enable later ssh acap user
2. Configure camera developer mode 
3. Create an environment for python locally, so we avoid installing the needed libraries globally
4. Download a model from Coral 
5. Download a picture from unsplash with resolution 300x300
6. Run a python script to convert the jpg into RGB or binary
7. Export binary and model to the /tmp camera folder through ssh
8. Run inference with larod tool selecting backend, the model and input file (binary dog picture) for the model

## Steps

### 1 - Build this acap and install in the camera

```bash
docker build --build-arg ARCH=aarch64 --tag larod-client .
```

### 2 - Configure developer mode and assign a password to your ssh user

Instructions: ["Developer mode instructions"](https://developer.axis.com/acap/get-started/set-up-developer-environment/set-up-device-advanced/)


### 3 - Create an environment for python applications

Do this once per project to avoid externally-managed-environment error:

```bash

# 1. Make a project folder (if you haven't already)
mkdir -p ~/python-playground
cd ~/python-playground

# 2. Create a virtual environment called .venv
python3 -m venv .venv

# 3. Activate it (VERY important)
source .venv/bin/activate

# 4. Install safely the packages
pip install click Pillow numpy

```

When done:

```bash
deactivate
```

### 4 - Download model

Model: ["Coral - SSD MobileNet V1"](https://raw.githubusercontent.com/google-coral/test_data/master/ssd_mobilenet_v1_coco_quant_postprocess.tflite)

### 5 - Download image

Image jpg (Save it as dog.jpg): ["dog"](https://unsplash.com/photos/FFwNGYZK-2o/download?ixid=M3wxMjA3fDB8MXxhbGx8fHx8fHx8fHwxNzYzNDU3OTM1fA&force=true&w=300)

or use curl:

```bash
curl -o dog.jpg "https://unsplash.com/photos/FFwNGYZK-2o/download?ixid=M3wxMjA3fDB8MXxhbGx8fHx8fHx8fHwxNzYzNDU3OTM1fA&force=true&w=300"
```

### 6 - Run python script 

If you are in under `~/python-playground` copy the script `img-converter.py` in this folder

```bash

# 5. Run the script
python img-converter.py -i dog.jpg -w 300 -h 300

```

You should get a `test.bin` file.


### 7 - Export model and binary recently created to /tmp in the camera

```bash
# 6. Send files to /tmp
scp ssd_mobilenet_v1_coco_quant_postprocess.tflite test.bin acap-larod_client@cam_ip:/tmp/

```

### 8 - Run inference with larod tool selecting backend, the model and input file (binary dog picture) for the model

* Create output files before running inference of your model due to a permission issues since FW 12. It will be fix soon. For now this step is needed.

```bash
touch test_out0.bin
touch test_out1.bin
touch test_out2.bin
touch test_out3.bin
```

```bash
# 7. Run inference
larod-client -d -c axis-a8-dlpu-tflite -g ssd_mobilenet_v1_coco_quant_postprocess.tflite -R 1 -i test.bin -o test_out0.bin -o test_out1.bin -o test_out2.bin -o test_out3.bin

```

## Extra steps

We will check the files output data

### 9 - Copy outputs + model back to your PC 

```bash
# 8. Get back to you folder
cd ~/python-playground

# 9. Copy outputs and model to your

```