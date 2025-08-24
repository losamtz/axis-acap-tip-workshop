# CivetWeb reference (used in this sample)

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


## robust body reader
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

## CivetWeb option
```c
const char* opts[] = {
  "listening_ports", PORT,            // e.g., "2001" or "127.0.0.1:2001"
  "request_timeout_ms", "10000",
  "num_threads", "1",               // single-threaded request handling
  0
};
```
---


## Quick cheat-sheet

### CivetWeb
- **Types:** `mg_context`, `mg_callbacks`, `mg_connection`, `mg_request_info`
- **Start/stop:** `mg_init_library`, `mg_start`, `mg_stop`
- **Routing:** `mg_set_request_handler(ctx, prefix, handler, cbdata)` 
- **I/O:** `mg_printf`, `mg_write`, `mg_read`
- **Meta:** `mg_get_request_info`

### Jansson
- **Create:** `json_object`, `json_string`, `json_true/false/null`, `json_pack`
- **Mutate:** `json_object_set_new`
- **Parse:** `json_loads`, `json_is_object`, `json_object_get`, `json_string_value`
- **Serialize:** `json_dumps`
- **Memory:** `json_decref` (owning refs), `free()` for `json_dumps` buffer
