




## Build

```bash
docker build --tag vdo-producer --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker create vdo-producer):/opt/app ./build