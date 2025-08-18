# Webserver Civetweb - Web Proxy

This sample demonstrates how to build a reverse-proxied web server inside an ACAP application using CivetWeb + AXParameter + Jansson.

- CivetWeb runs a single-threaded HTTP server (num_threads=1).
- AXParameter is used to persist configuration parameters.
- JSON endpoints allow get/set of parameters.
- Reverse proxy forwards requests from /local/api_multicast/ → http://localhost:2001.

This is the simplest possible architecture—no mutexes or threading required.

## Endpoints

Base path: `/local/api_multicast/`

- **GET** `/info-acap.cgi`
    Returns current parameter values:

    ```json
    {
    "MulticastAddress": "224.0.0.1",
    "MulticastPort": "1024",
    "ok": true
    }
    ```
- **POST** `/param-acap.cgi`
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
- ACAP reverseProxy exposes `/local/api_multicast/`.
- `index.html` uses fetch() calls to the endpoints or/and curl
- All AXParameter access happens in a single thread, so no locking is required.