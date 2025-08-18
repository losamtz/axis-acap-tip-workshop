# Fastcgi API - Web Parameter Thread

This ACAP sample exposes two **FastCGI endpoints** that read/write camera parameters via **AXParameter** and returns JSON using jansson.
Compared to the single-process sample, this one is prepared for **multi-request handling** and demonstrates **thread-safe** AXParameter access using a `pthread_mutex`.

- **App name / scope**: `web_parameter_thread`
- **Parameters**: IpAddress (string), Port (string)
- **Endpoints** (declared in manifest.json):

    - `information-acap.cgi` — GET current values
    - `parameter-acap.cgi` — POST new values

- **UI**: `settingPage`: `index.html` (simple HTML can call these endpoints)

Deployment model: runMode: "never". The camera web server spawns the FastCGI app on demand. The webserver passes a Unix socket path via `FCGI_SOCKET_NAME`, which the app opens with `FCGX_OpenSocket()` and then serves requests in a loop.

## What this sample shows

- Declaring **FastCGI endpoints** in an ACAP package.
- Parsing **JSON request bodies** and producing **JSON responses**.
- Using **AXParameter** in a **thread-safe** manner:

    - Global `AXParameter* handle`
    - Protected by `pthread_mutex_t handle_mtx`
    - Helpers `get_param_dup()` and `set_param()` lock around access.

- Ensuring parameters exist at startup with `ax_parameter_add()`.

## Code Tour

`main.c`

- **Initialization**

    - handle = ax_parameter_new(APP_NAME, ...)
    - add_if_missing("IpAddress", "192.168.0.90", "string")
    - add_if_missing("Port", "8080", "string")
    - FCGX_OpenSocket(getenv("FCGI_SOCKET_NAME"), 5) → FCGX_InitRequest(...)

- **Routing**

    - ENDPOINT_GET = "/local/web_parameter_thread/information-acap.cgi"
    - ENDPOINT_SET = "/local/web_parameter_thread/parameter-acap.cgi"
    - handle_info() → reads IpAddress, Port → JSON
    - handle_param() → parses body { "IpAddress": "...", "Port": "..." } → validates → sets → JSON

- **Thread safety**

- All AXParameter calls are wrapped by pthread_mutex_lock(&handle_mtx) / unlock.
- You can extend to a multithreaded accept loop if needed; the mutex will keep parameter operations safe.

## Notes 

- Threading: This sample doesn’t use callbacks, so no GMainLoop is required. If you later register callbacks to react to parameter changes, run GMainLoop in the main thread and the FastCGI loop in a worker thread (guarding AXParameter with a mutex, which we already do).
- Mutex: We protect AXParameter operations with g_param_mtx in case the server is extended to handle concurrent requests.
- JSON: Uses Jansson for parsing and emitting clean JSON responses.
- Extensibility: To add more fields, extend the JSON parsing in handle_param() and return them in handle_info().

## API

`GET /local/web_parameter_thread/information-acap.cgi`

Response 200

```json
{
  "ok": true,
  "IpAddress": "192.168.0.90",
  "Port": "8080"
}

```

`POST /local/web_parameter_thread/parameter-acap.cgi`

Request

```json
{
  "IpAddress": "10.0.0.5",
  "Port": "9090"
}

```
Response 200

```json
{
  "ok": true
}
```
Errors

```json
{"error":"Invalid JSON body"}       // bad JSON or no body
{"error":"No fields to update"}     // neither IpAddress nor Port present
{"ok":false,"error":"Failed to set one or more parameters"} // set error
```

## Lab: Install and test

1. From a browser/UI
    - Open the app page from the camera "Apps" UI (loads `index.html`)
    - Call enpoints from your page with `fetch()`
2. From curl

```bash
# GET current
curl -anyauth -u root:pass http://192.168.0.90/local/web_parameter_thread/information-acap.cgi

# POST update
curl -u root:pass -H "Content-Type: application/json" \
  -d '{"IpAddress":"10.0.0.5","Port":"9090"}' \
  http://192.168.0.90/local/web_parameter_thread/parameter-acap.cgi

```

3. Logs
    - starting web_parameter_thread FastCGI app
    - FCGX_InitRequest succeeded
    - Any JSON parse or parameters errors