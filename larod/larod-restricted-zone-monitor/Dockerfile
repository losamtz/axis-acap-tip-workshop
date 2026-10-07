ARG ARCH=aarch64
ARG VERSION=12.11.0
FROM axisecp/acap-native-sdk:${VERSION}-${ARCH}-ubuntu24.04
ARG CHIP=artpec8
WORKDIR /opt/app
COPY app/ ./
RUN case "$CHIP" in artpec8|artpec9|cpu) MODEL=ssd_mobilenet_v2_coco_quant_postprocess.tflite ;; \
    edgetpu) MODEL=ssd_mobilenet_v2_coco_quant_postprocess_edgetpu.tflite ;; \
    *) echo "Choose CHIP=artpec8, artpec9, cpu or edgetpu"; exit 1 ;; esac && \
    mkdir -p model label && \
    curl -fL --retry 3 "https://raw.githubusercontent.com/google-coral/test_data/master/$MODEL" -o model/converted_model.tflite && \
    curl -fL --retry 3 https://raw.githubusercontent.com/google-coral/test_data/master/coco_labels.txt -o label/labels.txt && \
    cp "manifest.json.$CHIP" manifest.json && \
    . /opt/axis/acapsdk/environment-setup* && \
    acap-build . -a model/converted_model.tflite -a label/labels.txt
