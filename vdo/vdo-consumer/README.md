


## Build

```bash
docker build --tag vdo-consumer --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-consumer):/opt/app ./build
```