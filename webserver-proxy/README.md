## Reverse-Proxy Web Server (CivetWeb) – Comparison Guide

This README summarizes two companion samples that expose a small JSON API over the camera’s built-in reverse proxy using **CivetWeb**, **AXParameter**, and **Jansson**:

- `web_server_rev_proxy` – single-threaded CivetWeb (simplest; no mutexes)
- `web_server_rev_proxy_thread` – multi-threaded CivetWeb (thread-safe AXParameter via pthread_mutex)

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

CivetWeb is multi-threaded (default).
AXParameter access is protected by a pthread_mutex_t to avoid concurrent access issues.

## Comparison

| Scenario                                              | Pick                                                       |
| ----------------------------------------------------- | ---------------------------------------------------------- |
| Workshop, minimal code, easy to reason about          | **`web_server_rev_proxy`** (single-threaded)               |
| Higher throughput, concurrent requests, future growth | **`web_server_rev_proxy_thread`** (multi-threaded + mutex) |
