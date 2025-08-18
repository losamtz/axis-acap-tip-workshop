## Webserver Civetweb - Web Proxy

This sample demonstrates how to build a **reverse-proxied web server** inside an ACAP application using **CivetWeb + AXParameter + Jansson**, with **pthread-based mutex protection**.

- CivetWeb runs in its default multi-threaded mode.
- A pthread_mutex_t protects AXParameter access.
- JSON endpoints allow get/set of parameters.
- Reverse proxy forwards requests from /local/api_multicast_thread/ → http://localhost:2002.

This pattern is needed when enabling **multiple request-handling threads**.

## Endpoints

Base path: `/local/api_multicast_thread/`

- **GET** `/information-acap.cgi`
    Returns current parameter values:

    ```json
    {
    "MulticastAddress": "224.0.0.1",
    "MulticastPort": "1024",
    "ok": true
    }
    ```
- **POST** `/parameter-acap.cgi`
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

- CivetWeb listens on port `2002` inside the app.
- ACAP reverseProxy exposes `/local/api_multicast_thread/`.
- `index.html` uses fetch() calls to the endpoints or/and curl
- Each HTTP request may be handled by a different worker thread.
- AXParameter access is guarded by a global pthread_mutex_t.