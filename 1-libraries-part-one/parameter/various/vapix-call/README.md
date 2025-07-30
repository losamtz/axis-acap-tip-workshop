Build the application: 

docker build --tag <APP_IMAGE> .

<APP_IMAGE> is the name to tag the image with, e.g., print-changes:1.0

Default architecture is aarch64. To build for armv7hf it's possible to update the ARCH variable in the Dockerfile or to set it in the docker build command via build argument:

docker build --build-arg ARCH=armv7hf --tag <APP_IMAGE> .

Copy the result from the container image to a local directory build:

docker cp $(docker create <APP_IMAGE>):/opt/app ./build

The expected output

Application log can be found directly at:

http://<AXIS_DEVICE_IP>/axis-cgi/admin/systemlog.cgi?appname=vapixcall