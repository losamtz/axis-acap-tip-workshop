# Draw views normalized coordinates


## Description

Draw different shapes for differend existing views when accessing from the web inteface.

Use of palette color space for large overlays like plain boxes, to lower the memory usage.

## Build

```
docker build --tag draw-view --build-arg ARCH=aarch64 .
```
```
docker cp $(docker create draw-view):/opt/app ./build
```