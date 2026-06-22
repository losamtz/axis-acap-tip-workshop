# Web Parameter Thread

This example expands the FastCGI parameter API with thread-safe access to shared AXParameter state. It is useful after `web-parameter` because it introduces concurrency concerns without changing the basic HTTP behavior.

## Main Difference

```mermaid
flowchart LR
    Request[FastCGI request] --> Lock[pthread_mutex_lock]
    Lock --> Param[AXParameter get/set]
    Param --> Unlock[pthread_mutex_unlock]
    Unlock --> Response[JSON response]
```

The parameter handle is global:

```c
static AXParameter* handle = NULL;
static pthread_mutex_t handle_mtx = PTHREAD_MUTEX_INITIALIZER;
```

Every read or write is protected by the mutex.

## Runtime Parameters

This example creates parameters at startup if they do not already exist:

```c
add_if_missing("IpAddress", "192.168.0.90", "string");
add_if_missing("Port", "8080", "string");
```

The helper treats "already exists" as success, which makes restart behavior predictable.

## Endpoints

| Method | Path | Meaning |
| --- | --- | --- |
| `GET` | `/local/web_parameter_thread/information-acap.cgi` | Read `IpAddress` and `Port` |
| `POST` | `/local/web_parameter_thread/parameter-acap.cgi` | Update `IpAddress` and `Port` |

## JSON Response Helper

The example centralizes response writing:

```c
static void send_json(FCGX_Request* req, int status_code, json_t* obj) {
    char* body = json_dumps(obj, JSON_COMPACT);
    FCGX_FPrintF(req->out,
                 "Status: %d\r\n"
                 "Content-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n"
                 "\r\n"
                 "%s",
                 status_code, body ? body : "{}");
    free(body);
}
```

This keeps endpoint handlers focused on application logic.

## Build

```sh
docker build --tag web-parameter-thread --build-arg ARCH=aarch64 .
docker cp $(docker create web-parameter-thread):/opt/app ./build
```

## Classroom Exercises

1. Remove the mutex and discuss what can go wrong with shared state.
2. Add stricter validation for `Port`.
3. Convert repeated endpoint strings into constants.
