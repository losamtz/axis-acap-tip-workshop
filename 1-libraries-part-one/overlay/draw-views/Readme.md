# Draw rectangle normalized coordinates


## Description

Use of palette color space for large overlays like plain boxes, to lower the memory usage.

## Build

docker build --tag draw-rectangle-normal --build-arg ARCH=aarch64 .

docker cp $(docker create draw-rectangle-normal):/opt/app ./build