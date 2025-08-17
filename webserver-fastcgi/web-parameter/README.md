## Fastcgi API - Web Parameter 

Exposes two endpoints and uses AXParameter without callbacks:

- **`info-acap.cgi`** – **GET** - returns current MulticastAddress and MulticastPort as JSON

- **`param-acap.cgi`** – **POST (JSON)** - sets **MulticastAddress** and/or **MulticastPort**, returns JSON status

It uses fcgiapp.h (request-based API) and not fcgi_stdio.h, jansson for JSON, and axparameter.h for parameter get/set.

The app is single-threaded, so no mutex is needed.

## Build

```bash
docker build --tag web-parameter --build-arg ARCH=aarch64 .

```
```bash
docker cp $(docker create web-parameter):/opt/app ./build

```