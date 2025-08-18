# Reverse-Proxy Web Server (CivetWeb) – Comparison Guide

This README summarizes two companion samples that expose a small JSON API over the camera’s built-in reverse proxy using **CivetWeb**, **AXParameter**, and **Jansson**:

- `web_proxy` – single-threaded CivetWeb (simplest; no mutexes)
- `web_proxy_thread` – multi-threaded CivetWeb (thread-safe AXParameter via pthread_mutex)

Both samples serve a simple HTML UI and offer JSON endpoints to get/set persistent parameters via AXParameter.

## What these samples demonstrate

- How to run a CivetWeb server inside an ACAP app.
- How to publish it externally through the ACAP reverseProxy (manifest).
- How to persist configuration with AXParameter.
- How to build JSON endpoints using Jansson.
- What changes when moving from single-threaded to multi-threaded HTTP handling.

## High-level Architecture
```
Browser  ──►  http://<camera>/local/api_multicast/…  ──►  Reverse Proxy  ──►  CivetWeb (port 2001)
                                                                       │
                                                                       ├── GET /info… (returns JSON)
                                                                       └── POST /param… (accepts JSON)
                                                                AXParameter (persisted device params)
```

### Notes

CivetWeb is multi-threaded by default. No need to specify amound of threads.
AXParameter access is protected by a pthread_mutex_t to avoid concurrent access issues, in case of use of register callbacks (which are not used in web-proxy-thread sample).

You can check default config in following sample ![web-server from acap-native-sdk-sample](https://github.com/AxisCommunications/acap-native-sdk-examples/blob/3973f5da55e7e52f188bddeff4eaa1698beb651c/web-server/app/web_server_rev_proxy.c#L52)

## Comparison

| Scenario                                              | Pick                                                       |
| ----------------------------------------------------- | ---------------------------------------------------------- |
| Workshop, minimal code, easy to reason about          | **`web_proxy`** (single-threaded)               |
| Higher throughput, concurrent requests, future growth | **`web_thread`** (multi-threaded + mutex) |


## API description

![Civetweb & Jansson](./doc_jansson_civetweb_api.md)