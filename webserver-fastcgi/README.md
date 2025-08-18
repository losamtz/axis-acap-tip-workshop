# Web Parameter Samples — FastCGI + AXParameter

This folder contains two ACAP samples that demonstrate how to build web-enabled configuration UIs using **FastCGI** and **AXParameter** on Axis devices.
Both samples expose **HTTP endpoints** that interact with parameters defined in `manifest.json`, and return **JSON** responses using **jansson**.
A companion `index.html` provides a simple UI for testing.

## 1. web_parameter

- **Objective**:
    Introduces a single-process FastCGI server that responds to HTTP requests with JSON.
    Uses AXParameter for reading and writing camera parameters, without threading.

- **Endpoints**:

    - `info-acap.cgi` → GET → returns current parameter values.
    - `param-acap.cgi` → POST → updates parameters.

- **Parameters**:

    - MulticastAddress (default: 224.0.0.1)
    - MulticastPort (default: 1024)

- **Highlights**:

    - Shows how to declare FastCGI endpoints in manifest.json.
    - Reads JSON request body, validates, and updates parameters.
    - Simplest possible architecture — no threading, single loop handles all requests.

## 2. web_parameter_thread

- **Objective**:
    Extends the previous sample to support thread-safe parameter access with pthread_mutex.
    Prepared for multi-threaded FastCGI handling.

- **Endpoints**:

    - `information-acap.cgi` → GET → returns current values.
    - `parameter-acap.cgi` → POST → updates values.

- **Parameters**:

    - IpAddress (default: 192.168.0.90)
    - Port (default: 8080)

- **Highlights**:

    - Demonstrates mutex-protected AXParameter usage (pthread_mutex around ax_parameter_get/set).
    - Uses ax_parameter_add() at startup to ensure parameters exist (idempotent).
    - Shows a scalable structure: one binary can serve multiple .cgi endpoints.
    - Adds CORS headers (Access-Control-Allow-Origin: *) for client-side JS testing.

## Learning Goals

By working with these two samples, you will learn:

- How to expose camera parameters via web APIs (GET/POST JSON).
- How to use FastCGI in ACAP (FCGX_Accept_r loop).
- When to use AXParameter get/set directly vs. add mutex protection for multi-threading.
- How to connect parameters to a simple HTML UI with JavaScript fetch() calls.

## Suggested Workshop Labs

1. Modify Parameters
    - Change defaults in manifest.json and verify updates in the web UI.

2. Add Validation
    - Reject invalid IP/port values in both samples.

3. Add a New Parameter
    - Add TTL or Enabled flag, wire it through GET/POST, and update the HTML page.

4. Compare Single vs. Threaded
    - Deploy both apps, send multiple simultaneous requests (e.g. with ab or curl &), observe differences.

5. Enable Multi-threading (advanced)
    - In `web_parameter_thread`, spawn worker threads to process FastCGI requests in parallel.

6. Integrate with Another Module
    - Use the saved parameters in another ACAP (e.g., multicast streaming config).

## Summary

- `web_parameter` → the simplest FastCGI + AXParameter example, single-threaded, great for learning the basics.

- `web_parameter_thread` → builds on it, adds thread-safety and scalability for real-world apps.

Together, they form a progressive workshop path: start small, then add concurrency and robustness.