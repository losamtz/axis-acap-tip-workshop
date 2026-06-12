ARG ARCH=aarch64
ARG VERSION=12.10.0
ARG UBUNTU_VERSION=24.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk

FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

RUN apt-get update && apt-get install -y --no-install-recommends \
    unzip \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/app/model
ARG CHIP

# Download the pretrained model
ARG MODEL_BUCKET=https://acap-ml-models.s3.amazonaws.com/tensorflow_to_larod_resnet
RUN curl -o model.tflite $MODEL_BUCKET/custom_resnet_artpec8_car_human_256.tflite 

# Copy the library to application folder
WORKDIR /opt/app
COPY ./app .

# Build the ACAP application
RUN . /opt/axis/acapsdk/environment-setup* && acap-build . -a 'model/model.tflite'