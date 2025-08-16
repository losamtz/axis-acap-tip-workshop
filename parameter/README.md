# ACAP Parameter API – Workshop Overview

Welcome to **Module 1: Parameter API** of the *Axis ACAP Tip Workshop*.  
This section focuses on understanding and mastering ACAP’s **AXParameter API**—the foundation for making ACAP applications configurable and responsive to runtime changes.

---

## Lab Samples Included

This folder contains three progressive hands-on samples:

### 1. `parameter_manifest`
- **Objective**: Demonstrates how to create and register application parameters via the **application manifest**.
- **Key Features**:
  - Parameters are declared in the manifest file.
  - Application code registers callbacks to respond to parameter changes.
  - Graceful shutdown with signal handling.
- **What you'll learn**:
  - Using `ax_parameter_new()` to open a parameter handle.
  - Registering callbacks with `ax_parameter_register_callback()`.
  - Integrating with a GLib main loop to monitor parameter updates.

---

### 2. `parameter_runtime`
- **Objective**: Demonstrates how to **add, remove, set, and list parameters dynamically** at runtime.
- **Key Features**:
  - `ax_parameter_add()` to add new parameters.
  - `ax_parameter_set()` to update values (with optional callback trigger).
  - `ax_parameter_remove()` to unregister parameters.
  - `ax_parameter_list()` to inspect current parameters.
- **What you'll learn**:
  - Building applications that can evolve parameter sets during runtime.
  - Observing parameter updates via syslog.
  - Linking parameter changes to application logic.

---

### 3. `parameter_custom_interface`
- **Objective**: Demonstrates how to build a **custom web interface** to edit parameters live.
- **Key Features**:
  - A Bootstrap-based modal UI for editing parameters.
  - JavaScript (`onload.js` and `submitForm.js`) fetching parameters with `/axis-cgi/param.cgi` and updating them.
  - Tight coupling between the browser UI and AXParameter callbacks in the ACAP application.
- **What you'll learn**:
  - Bridging backend parameter logic with a frontend.
  - Securely fetching and setting parameters via CGI.
  - Delivering user-friendly configuration panels on Axis devices.

---

## How to Work Through the Labs

1. **Start with `parameter_manifest`**  
   Understand the basics of parameter creation, registration, and monitoring.

2. **Move to `parameter_runtime`**  
   Learn how to manipulate parameters dynamically during the lifetime of your application.

3. **Finish with `parameter_custom_interface`**  
   Build a real-world UI that lets end users configure your application interactively.

---

## Lab Workflow Summary

| Lab / Sample                  | Primary Focus             | Expected Learning Outcomes                  |
|-------------------------------|---------------------------|---------------------------------------------|
| `parameter_manifest`          | Static parameter setup    | Manifest-declared params, callbacks, signals |
| `parameter_runtime`           | Dynamic param management  | Adding, setting, listing, removing params    |
| `parameter_custom_interface`  | UI-driven parameter edits | Bridging web UI with AXParameter backend     |

> Recommended path: **manifest → runtime → custom interface**.

---

## Prerequisites

- ACAP SDK with Docker-based toolchain
- Basic familiarity with **C** and **GLib**
- Axis device with web access
- Browser for testing the custom UI

