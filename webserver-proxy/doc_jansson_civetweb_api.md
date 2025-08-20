# web_proxy — CivetWeb + Jansson (with AXParameter) walkthrough

This README explains **what each function, type, and call does** in the provided sample, focusing on **CivetWeb** (HTTP server) and **Jansson** (JSON), and walking through the code **in order**. It also clarifies how requests flow through the program and what the endpoints return.

> Sample tagline: *Single-threaded CivetWeb reverse-proxy backend with AXParameter + Jansson.*

---

## High-level overview

- The program starts a **CivetWeb** HTTP server listening on port `2001` (single worker thread).  
- It exposes:
  - `GET /local/web_proxy/api/info` → returns JSON with `MulticastAddress`, `MulticastPort`, and `ok`.
  - `POST /local/web_proxy/api/param` → accepts JSON with `MulticastAddress` and/or `MulticastPort`, updates AXParameter, and returns `{ ok, changed }`.
  - `GET /` → serves `html/index.html`.
- The Axis **reverseProxy** can forward browser requests under `/local/web_proxy/api/*` to `http://localhost:2001/*`, removing the need for CORS.
- **AXParameter** holds persistent camera parameters (`MulticastAddress`, `MulticastPort`).

---

## Code walkthrough (in order of appearance)

### Macros & globals
- `#define APP_NAME "web_proxy"`
- `#define PORT "2001"` — CivetWeb listens here.
- `static volatile sig_atomic_t running = 1;` — gate for the main loop; set to `0` on SIGINT/SIGTERM.
- `static AXParameter* handle = NULL;` — global AXParameter handle.

### 1 `panic(fmt, ...)`
**Purpose:** log an error and exit immediately.  
**Key points:**
- Uses `vsyslog()` to log; calls `exit(EXIT_FAILURE)`.
- GCC attributes: `noreturn` and `format(printf,1,2)` enable better compile-time checks.

### 2 `on_signal(int signo)`
**Purpose:** signal handler for graceful shutdown.
- Sets `running = 0` so the main loop can terminate.

### 3 `send_json(struct mg_connection* conn, int status, json_t* obj)`
**Purpose:** serialize a Jansson `json_t*` object and send an HTTP response.  
**CivetWeb used:** `mg_printf()` to write headers/body.  
**Jansson used:** `json_dumps(obj, JSON_COMPACT)` → `char*` that must be `free()`d.  
**Headers:** JSON content-type, `no-store`, some permissive CORS headers (not strictly needed behind reverseProxy).

>  **Note:** The status line uses `"HTTP/1.1 %d OK"` for all codes. If you send `400`, the text will still say "OK". Consider mapping codes to reason phrases or (better) omit the reason phrase in HTTP/1.1.

**Improvement (optional):** add `Content-Length` and compute a proper reason phrase.

### 4 `read_body(struct mg_connection* conn)`
**Purpose:** read request body based on `Content-Length`.
- CivetWeb: `mg_get_header(conn, "Content-Length")`, `mg_read()`.
- Allocates a buffer, reads up to `len`, NUL-terminates, returns `char*` the caller must `free()`.

>  For robustness, loop until all bytes are read; the current code does one `mg_read()` call. See **Appendix A** for a looped version.

### 5 AXParameter helpers
- `add_if_missing(name, initial_value, meta)`  
  Calls `ax_parameter_add()`. If the parameter already exists (error code `AX_PARAMETER_PARAM_ADDED_ERROR`), returns `TRUE`. Logs other errors.  
  **Memory:** `GError*` cleared with `g_error_free()` when used.

- `get_param_dup(name)`  
  Calls `ax_parameter_get()`. On success, returns a `char*` that **must be freed with `g_free()`** by the caller.

- `set_param(name, value)`  
  Calls `ax_parameter_set()` (no callback/commit). Logs if it fails.

### 6 HTTP handlers

- **`InfoHandler(conn, ud)`** — `GET /local/web_proxy/api/info`
  - Rejects non-GET by returning `0` (let other handlers try).
  - Builds `json_object()`; inserts:
    - `MulticastAddress`: `json_string(addr)` or `json_null()`
    - `MulticastPort`: `json_string(port)` or `json_null()`
    - `ok`: `json_true()`
  - Sends with `send_json(..., 200, out)` and decrefs `out`.

- **`ParamHandler(conn, ud)`** — `POST /local/web_proxy/api/param`
  - Rejects non-POST by returning `0`.
  - Reads body; if missing → `400` with `{ "error": "Missing or empty body" }`.
  - Parses JSON: `json_loads()` with `json_error_t jerror` for diagnostics.
  - Validates object: `json_is_object(root)`.
  - Extracts optional keys:
    - `jAddr = json_object_get(root, "MulticastAddress")`
    - `jPort = json_object_get(root, "MulticastPort")`
  - If present and strings (`json_is_string()`), calls `set_param(...)` and ORs into `changed`.
  - Responds `{ ok: true, changed: <bool> }`.

- **`RootHandler(conn, ud)`** — `GET /`
  - Streams `html/index.html` with `mg_write()` in 4 KiB chunks.
  - On fopen failure → `500`.

### 7 `cb_begin_request(struct mg_connection *conn)`
**Purpose:** global CivetWeb callback to **log all requests**.
- CivetWeb provides `mg_request_info` (method, URI, headers, etc.).
- Return `0` → continue normal request processing; return `1` would short-circuit handling.

### 8 `main()`
1. Open syslog; log startup message.
2. Install signal handlers.
3. `ax_parameter_new(APP_NAME, &error)` → create handle (fatal on failure).
4. Ensure parameters exist with `add_if_missing(...)`.
5. CivetWeb options:
   - `listening_ports = "2001"`
   - `request_timeout_ms = "10000"`
   - `num_threads = "1"` (single-threaded)
6. `mg_init_library(0)`; set callbacks; `mg_start(&cb, NULL, opts)`.
7. Register request handlers:
   - `/` → `RootHandler`
   - `/local/web_proxy/api/info` → `InfoHandler`
   - `/local/web_proxy/api/param` → `ParamHandler`
8. Loop while `running`, sleeping 1s per iteration.
9. On exit: `mg_stop(ctx)`, `ax_parameter_free(handle)`, `closelog()`.

---

## CivetWeb reference (used in this sample)

### Core types
- `struct mg_context` — server instance state returned by `mg_start()`; used to register handlers and stop the server.
- `struct mg_callbacks` — table of optional global callbacks (e.g., `begin_request`). Set members you use; zero-init the rest.
- `struct mg_connection` — per-request connection state passed into handlers and callbacks.
- `struct mg_request_info` — metadata for a request: `request_method`, `request_uri`, `local_uri`, headers, etc. Retrieve via `mg_get_request_info(conn)`.

### Key functions
- `mg_init_library(flags)` — initialize CivetWeb library (optional but good practice).
- `mg_start(callbacks, cbdata, options)` — start server. `callbacks` points to your `mg_callbacks` table; `cbdata` is a user pointer delivered to callbacks; `options` is a `NULL`-terminated array of key/value strings (e.g., `{"listening_ports", "2001", 0}`).
- `mg_stop(ctx)` — stop server, free resources.
- `mg_set_request_handler(ctx, uri, handler, cbdata)` — register a handler for a **path prefix**. Longest-prefix wins. If your handler returns **1**, the request is considered handled; **0** lets CivetWeb continue searching or serve a file.
- `mg_get_request_info(conn)` — get request metadata.
- `mg_printf(conn, fmt, ...)` — write to the response stream (headers/body).
- `mg_write(conn, buf, len)` — write binary data to the response.
- `mg_read(conn, buf, len)` — read request body (for POST, etc.). Returns bytes read or <0 on error.

### Threading note
- Even with `num_threads = 1`, CivetWeb still uses internal threads for housekeeping; request handling runs on **one** worker, so your handlers run serially. If you remove the option, CivetWeb creates a pool, and your handlers may run concurrently → protect shared state.

---

## Jansson reference (used in this sample)

### Core types
- `json_t` — opaque JSON value (object/array/string/number/true/false/null).
- `json_error_t` — parsing diagnostics struct filled by `json_loads`, `json_loadf`, etc.

### Creation & mutation
- `json_object()` — create `{}`.
- `json_string(const char*)` — create a JSON string value.
- `json_true()`, `json_false()`, `json_null()` — singletons for those literals.
- `json_object_set_new(obj, key, value)` — **inserts and steals ownership** of `value`. Do **not** `json_decref()` the value after calling this; the object now owns it.
- `json_pack(fmt, ...)` — printf-like JSON builder (e.g., `json_pack("{s:s}", "error", "Missing body")`).

### Parsing & querying
- `json_loads(const char* text, size_t flags, json_error_t* err)` — parse text to `json_t*` (or `NULL` on error).
- `json_is_object(v)` — type check.
- `json_object_get(obj, key)` — fetch child value (borrowed ref).
- `json_string_value(v)` — for string values, returns `const char*` (do **not** free).

### Serialization & memory
- `json_dumps(v, flags)` — allocate and return a `char*` representation; **free()** it when done.
- `json_decref(v)` — decrease refcount; free when it hits 0. Every `json_t*` you create/obtain **and own** must be decref’d (except where ownership is transferred with `*_set_new`).

**Ownership rules in this sample:**
- Objects created with `json_object()` or `json_pack()` → `json_decref()` exactly once when done.
- Values passed via `json_object_set_new()` → ownership transferred; **don’t** decref them separately.
- Buffer from `json_dumps()` → `free()`.

---

## Endpoint contract

### `GET /local/web_proxy/api/info`
Response JSON:
```json
{
  "MulticastAddress": "224.0.0.1" | null,
  "MulticastPort": "1024" | null,
  "ok": true
}
```

### `POST /local/web_proxy/api/param`
Request JSON (any subset of keys):
```json
{ "MulticastAddress": "224.0.0.2", "MulticastPort": "1025" }
```
Response JSON:
```json
{ "ok": true, "changed": true | false }
```

---

## Axis AXParameter quick reference (used here)
- `ax_parameter_new(app_name, &GError*)` — open a handle.  
- `ax_parameter_add(handle, name, initial_value, meta, &GError*)` — create parameter if missing; returns `FALSE` on error or already exists (check error code `AX_PARAMETER_PARAM_ADDED_ERROR`).
- `ax_parameter_get(handle, name, &out_value, &GError*)` — fetch value; returns an allocated `char*` (free with `g_free()`).
- `ax_parameter_set(handle, name, value, commit, &GError*)` — set value.
- `ax_parameter_free(handle)` — free handle.

**Threading:** this sample is single-threaded (one CivetWeb worker), so no mutex is needed. If you increase threads, protect AXParameter usage.

---

## Request flow
1. Browser calls camera: `/local/web_proxy/api/info` or `/local/web_proxy/api/param`.
2. Camera reverse-proxy forwards to `http://localhost:2001/local/web_proxy/api/...`.
3. CivetWeb matches registered handlers and calls the corresponding function.
4. Handler builds JSON via Jansson, sends using `mg_printf`/`mg_write`.
5. Client receives JSON. No CORS needed (same-origin via reverseProxy).

---

## Improvements & hardening (optional)
- **Status line:** Replace `"%d OK"` with proper reason phrase mapping or omit it.
- **Content-Length:** compute and send it for better client behavior.
- **Body reading:** loop until full `Content-Length` is read (see Appendix A below).
- **TLS:** if exposing beyond localhost, enable HTTPS or keep `127.0.0.1:2001` and rely on the camera’s TLS terminator.
- **Input validation:** validate `MulticastPort` is numeric/in-range; validate `MulticastAddress`.

---

## Appendix A — robust body reader
```c
static char* read_body_full(struct mg_connection* c) {
    const char* len_str = mg_get_header(c, "Content-Length");
    long len = len_str ? strtol(len_str, NULL, 10) : 0;
    if (len <= 0 || len > (1<<20)) return NULL;

    char* buf = malloc((size_t)len + 1);
    if (!buf) return NULL;

    size_t got = 0;
    while (got < (size_t)len) {
        int n = mg_read(c, buf + got, (int)((size_t)len - got));
        if (n <= 0) break;  // client closed or error
        got += (size_t)n;
    }
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[got] = '\0';
    return buf;
}
```

---

## Appendix B — CivetWeb options used
```c
const char* opts[] = {
  "listening_ports", PORT,            // e.g., "2001" or "127.0.0.1:2001"
  "request_timeout_ms", "10000",
  "num_threads", "1",               // single-threaded request handling
  0
};
```

---

## Appendix C — Build notes
- Link against: `glib-2.0`, `gio-2.0` (for GError), `jansson`, `axparameter`, and `civetweb`.
- If CivetWeb has no `pkg-config` file, add `-I<path>/civetweb/include` and `-L<path>/civetweb/lib -lcivetweb` manually.
- Example (excerpt):
```make
PKGS = glib-2.0 gio-2.0 jansson
CFLAGS += $(shell pkg-config --cflags $(PKGS)) -I/opt/build/civetweb/include
LDLIBS += $(shell pkg-config --libs $(PKGS)) -laxparameter -lcivetweb -lpthread
```

---

## Quick cheat-sheet

### CivetWeb
- **Types:** `mg_context`, `mg_callbacks`, `mg_connection`, `mg_request_info`
- **Start/stop:** `mg_init_library`, `mg_start`, `mg_stop`
- **Routing:** `mg_set_request_handler(ctx, prefix, handler, cbdata)` (longest-prefix wins)
- **I/O:** `mg_printf`, `mg_write`, `mg_read`
- **Meta:** `mg_get_request_info`

### Jansson
- **Create:** `json_object`, `json_string`, `json_true/false/null`, `json_pack`
- **Mutate:** `json_object_set_new`
- **Parse:** `json_loads`, `json_is_object`, `json_object_get`, `json_string_value`
- **Serialize:** `json_dumps`
- **Memory:** `json_decref` (owning refs), `free()` for `json_dumps` buffer
