# Fastcgi API - Web Parameter Thread

single FastCGI app using fcgiapp.h + Jansson + AXParameter that exposes two endpoints:

- **`/param-acap.cgi`** - **POST JSON** to set parameters (MulticastAddress, MulticastPort)

- **`/info-acap.cgi`** - **GET** (or any) to return current params as JSON

It runs one FastCGI loop in the main thread (no GLib main loop needed since we’re not using callbacks). If later you want live parameter callbacks, you can move the FCGI loop to a worker thread and run a GMainLoop on the main thread.

## Notes & rationale

- Threading: This sample doesn’t use callbacks, so no GMainLoop is required. If you later register callbacks to react to parameter changes, run GMainLoop in the main thread and the FastCGI loop in a worker thread (guarding AXParameter with a mutex, which we already do).

- Mutex: We protect AXParameter operations with g_param_mtx in case the server is extended to handle concurrent requests.

- JSON: Uses Jansson for parsing and emitting clean JSON responses.

- Extensibility: To add more fields, extend the JSON parsing in handle_param() and return them in handle_info().

