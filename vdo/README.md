
## Build

```bash
docker build --tag vdo-utilities --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-utilities):/opt/app ./build
```