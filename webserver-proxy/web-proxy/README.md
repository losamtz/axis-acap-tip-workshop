# Webserver Civetweb - Web Proxy

This sample demonstrates how to build a reverse-proxied web server inside an ACAP application using CivetWeb + AXParameter + Jansson.

- CivetWeb runs a single-threaded HTTP server (num_threads=1).
- AXParameter is used to persist configuration parameters.
- JSON endpoints allow get/set of parameters.
- Reverse proxy forwards requests from /local/api_multicast/ → http://localhost:2001.

This is the simplest possible architecture—no mutexes or threading required.

## Endpoints

Base path: `/local/web_proxy/api/`

- **GET** `/info`
    Returns current parameter values:

    ```json
    {
    "MulticastAddress": "224.0.0.1",
    "MulticastPort": "1024",
    "ok": true
    }
    ```
- **POST** `/param`
    Accepts JSON body:

    ```json
    { "MulticastAddress": "239.1.2.3", "MulticastPort": "9000" }
    ```

    returns:
    ```json
    { "ok": true, "changed": true }
    ```

- **GET** `/` → Serves `html/index.html` (frontend).

---

## Parameters

Configured via `manifest.json`:

- MulticastAddress (default: 224.0.0.1)
- MulticastPort (default: 1024)

Stored persistently using AXParameter.

## How it works

- CivetWeb listens on port `2001` inside the app.
- ACAP reverseProxy exposes `/local/web_proxy/api/`.
- `index.html` uses fetch() calls to the endpoints or/and curl
- All AXParameter access happens in a single thread, so no locking is required.

## Lab:

1. start & open under apps applicatiion web proxy
2. Test changing values
3. reload page

4. Test the api with curl:

3. Test with curl:

```bash
curl --anyauth -u root:pass http://192.168.0.90/local/web_proxy/api/info
```

```bash
curl --anyauth -u root:pass -H "Content-Type: application/json" \
  -d '{"MulticastAddress":"224.0.0.2","MulticastPort":"9000"}' \
  http://192.168.0.90/local/web_proxy/api/param

```

## Build

```bash
docker build --tag web-proxy --build-arg ARCH=aarch64 .

```
```bash
docker cp $(docker create web-proxy):/opt/app ./build

```