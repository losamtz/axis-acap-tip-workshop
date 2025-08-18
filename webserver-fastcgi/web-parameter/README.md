## Fastcgi API - Web Parameter 

A minimal ACAP that exposes two FastCGI endpoints and a small HTML UI to read and update parameters (MulticastAddress, MulticastPort) using the AXParameter API. Responses are JSON via jansson.

- Endpoints (declared in manifest.json):

    - **`info-acap.cgi`** – **GET** - returns current MulticastAddress and MulticastPort as JSON
    - **`param-acap.cgi`** – **POST (JSON)** - sets **MulticastAddress** and/or **MulticastPort**, returns JSON status

- **UI**: `index.html` (declared as settingPage) calls the two endpoints with fetch().
- It uses fcgiapp.h (request-based API) and not fcgi_stdio.h, jansson for JSON, and axparameter.h for parameter get/set.

- The app is single-threaded, so no mutex is needed.

The camera web server starts the FastCGI program per request and passes a socket path via the `FCGI_SOCKET_NAME` environment variable, which the app uses with `FCGX_OpenSocket()`.

## What you’ll learn

- How to declare FastCGI endpoints in an ACAP package.
- How to parse request bodies and emit JSON with jansson.
- How to get/set AXParameter safely (no callbacks required).
- How to serve a simple HTML page that talks to your FastCGI endpoints.

## Folder highlights

- `web_parameter.c`
    Single-process FastCGI program that:

    - Opens the FastCGI socket from FCGI_SOCKET_NAME
    - Routes by SCRIPT_NAME (/local/web_parameter/info-acap.cgi, /local/web_parameter/param-acap.cgi)
    - Uses AXParameter to read/write parameters
    - Responds JSON (application/json) via jansson

- `manifest.json`
    Declares:

    - `httpConfig` FastCGI entries (`param-acap.cgi`, `info-acap.cgi`)
    - `settingPage: index.html`
    - `paramConfig` defaults (`MulticastAddress`, `MulticastPort`)
    - runMode: "never" (no daemon)

- `index.html`
    Simple form that GETs /local/web_parameter/info-acap.cgi and POSTs to /local/web_parameter/param-acap.cgi.

## API

`GET /local/web_parameter/info-acap.cgi`

Response 200
```json
{
  "ok": true,
  "MulticastAddress": "224.0.0.1",
  "MulticastPort": "1024"
}

```
`POST /local/web_parameter/param-acap.cgi`

Request
```json
{
  "MulticastAddress": "224.0.0.2",
  "MulticastPort": "9000"
}

```

Response 200
```json
{
  "ok": true,
  "MulticastAddress": "224.0.0.2",
  "MulticastPort": "9000",
  "changed": true
}

```

On validation/parse errors you’ll get:

```json
{"ok": false, "error": "Invalid JSON"}
```

## How it works (under the hood)

1. **Spawn-on-demand**

    With `runMode`: "never", the Axis web server launches the FastCGI app **only when a request hits info-acap.cgi or param-acap.cgi**.

2. **Socket handoff**

    The web server sets `FCGI_SOCKET_NAME` to a Unix socket path. The app:

    ```c
    const char* socket_path = getenv("FCGI_SOCKET_NAME");
    int sock = FCGX_OpenSocket(socket_path, 5);
    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO);

    int status = FCGX_InitRequest(&req, sock, 0);

    while (FCGX_Accept_r(&req) == 0) { ... }

    ```

    Then it routes on `SCRIPT_NAME` and `REQUEST_METHOD`.

3. AXParameter

    - `ax_parameter_new(APP_NAME, &error)` once per process
    - `ax_parameter_get and ax_parameter_set(..., /*run_callback=*/FALSE, ...)` per request
    - No GLib main loop or callbacks needed for simple get/set.

4. JSON (Jansson)

    - Request body parsing with FCGX_GetStr → json_loads
    - Responses with json_object_* + json_dumps, Content-Type: application/json.

## Lab: Install & Test

1. Install the package (via camera web UI or acapctrl tool).

2. Open the app page
    Navigate to the app in the camera’s Apps page; it should open index.html.

3. Test with curl:

```bash
curl --anyauth -u root:pass http://192.168.0.90/local/web_parameter/info-acap.cgi
```
```bash
curl --anyauth -u root:pass -H "Content-Type: application/json" \
  -d '{"MulticastAddress":"224.0.0.2","MulticastPort":"9000"}' \
  http://192.168.0.90/local/web_parameter/param-acap.cgi

```

4. Logs

    Check System Log for lines like:

    - Starting web_parameter (single-process FastCGI)
    - AXParameter handler has been instanciated
    - `FCGI GET /local/web_parameter/info-acap.cgi`


## Build

```bash
docker build --tag web-parameter --build-arg ARCH=aarch64 .

```
```bash
docker cp $(docker create web-parameter):/opt/app ./build

```