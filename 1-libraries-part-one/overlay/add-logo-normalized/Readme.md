# Draw rectangle normalized coordinates


## Description

Upload a logo and set in corner positions with normalized coordinates

## Build

docker build --tag add-logo-normal --build-arg ARCH=aarch64 .

docker cp $(docker create add-logo-normal):/opt/app ./build