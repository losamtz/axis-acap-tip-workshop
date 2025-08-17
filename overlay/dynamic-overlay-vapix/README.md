# Dynamic Overlay VAPIX API - Draw text with VAPIX Overlay API

This sample shows how to create a dynamic text overlay on an Axis device using the VAPIX Dynamic Overlay API over HTTP. This library makes it able to add text overlay, images and boxes which can also be configured through the web interface of the camera. The sample demonstrates:

- Getting a service account credential from the device over D-Bus (com.axis.HTTPConf1.VAPIXServiceAccounts1.GetCredentials)
- Building a JSON RPC request with jansson
- Sending the request to /axis-cgi/dynamicoverlay/dynamicoverlay.cgi with libcurl
- Parsing the JSON response and logging useful fields

The code adds a text overlay (top-left), white text on black background, font size 60, with a formatted timestamp string ("AXIS TIP Paris workshop - Date: %c").

## Project layout

- `main.c`
Builds the addText JSON payload, posts it, and logs the response.

- `curl-request.c/.h`
Thin helper around libcurl (POST JSON, collect response into a GString).

- `vapix-credentials.c/.h`
Fetches the basic auth username:password for a dynamic user using GDBus.

- `panic.h`
Small helper for fatal logging + exit (used throughout).

## Dependencies

- GLib / GIO (for GString, GDBus, misc)
- libcurl
- jansson (JSON)
- syslog

On your build host (SDK container), make sure the dev packages are available (the ACAP SDK v3 container already ships what you need).

## Lab

1. Start the app from the Apps page (or run the binary if you’re developing).
2. Open the Live View → you should see white text on black background at top-left.
3. Check syslog for the JSON response and parsed fields:

Full json response: ...

Camera: <id>
Identity: <overlay-identity> => 

**Note**: this identifier is important if we want to update the overlay. Warning: update function  is not implemeted!

Check:

- App logs show Curl version and Jansson version.
- D-Bus credential fetch succeeded (no “D-Bus” error in logs).
- HTTP POST returned 200 (no panic about response code).
- Overlay text appears at top-left on the stream.

### Extra lab:

Inside build_addtext_request():

1. camera: integer camera index (usually 1 for single-sensor devices)
2. position: "topLeft" | "topRight" | "bottomLeft" | "bottomRight" | "center" | ..."
3. text: any string; note %c will be interpreted by the camera as a time format placeholder (depends on API behavior/firmware; adjust if you want raw text)

4. textColor: "white", "black", "red", "green", etc.
5. textBGColor: background color name or "transparent" if supported
6. fontSize: integer pixels

---

## Application flow:

### Retrive credentials with dbus

```c
char* retrieve_vapix_credentials(const char* username) {
    GError* error               = NULL;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection)
        panic("Error connecting to D-Bus: %s", error->message);

    const char* bus_name       = "com.axis.HTTPConf1";
    const char* object_path    = "/com/axis/HTTPConf1/VAPIXServiceAccounts1";
    const char* interface_name = "com.axis.HTTPConf1.VAPIXServiceAccounts1";
    const char* method_name    = "GetCredentials";

    GVariant* result = g_dbus_connection_call_sync(connection,
                                                   bus_name,
                                                   object_path,
                                                   interface_name,
                                                   method_name,
                                                   g_variant_new("(s)", username),
                                                   NULL,
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1,
                                                   NULL,
                                                   &error);
    if (!result)
        panic("Error invoking D-Bus method: %s", error->message);

    char* credentials = parse_credentials(result);

    g_variant_unref(result);
    g_object_unref(connection);
    return credentials;
}
```

### cURL request

```c
static char*
vapix_post(CURL* handle, const char* credentials, const char* endpoint, const char* request) {
    GString* response = g_string_new(NULL);
    char* url         = g_strdup_printf("http://127.0.0.12/%s", endpoint);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_USERPWD, credentials);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_to_gstring_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);

    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK)
        panic("curl_easy_perform error %d: '%s'", res, curl_easy_strerror(res));

    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code != 200)
        panic("Got response code %ld from request to %s with response '%s'",
              response_code,
              request,
              response->str);

    free(url);
    return g_string_free(response, FALSE);
}

```

### Endpoint to dynamic overlay and payload

```c
static json_t*
vapix_post_json(CURL* handle, const char* credentials, const char* endpoint, const char* request) {
    char* text_response = vapix_post(handle, credentials, endpoint, request);
    json_error_t parse_error;
    json_t* json_response = json_loads(text_response, 0, &parse_error);
    if (!json_response)
        panic("Invalid JSON response: %s", parse_error.text);

    const json_t* request_error = json_object_get(json_response, "error");
    if (request_error)
        panic("Failed to perform request: %s",
              json_string_value(json_object_get(request_error, "message")));

    free(text_response);
    return json_response;
}
```
```c
static json_t* add_text(CURL* handle, const char* credentials) {
    const char* endpoint = "/axis-cgi/dynamicoverlay/dynamicoverlay.cgi";

    const char* request =
        "{"
        "  \"apiVersion\": \"1.0\","
        "  \"context\": \"abc\","
        "  \"method\": \"addText\","
        "  \"params\": {"
        "    \"camera\": 1,"
        "    \"text\": \"AXIS TIP Paris workshop - Date: %c\","
        "    \"position\": \"topLeft\","
        "    \"textColor\": \"white\","
        "    \"fontSize\": 32,"
        "    \"textBGColor\": \"black\""  
        "   }"
        "}";

    return vapix_post_json(handle, credentials, endpoint, request);
}
```
or
```c
    json_t* root = json_object();
    json_t* params = json_object();

    // Fill the "params" object
    json_object_set_new(params, "camera", json_integer(1));
    json_object_set_new(params, "text", json_string("AXIS TIP Paris workshop - Date: %c"));
    json_object_set_new(params, "position", json_string("topLeft"));
    json_object_set_new(params, "textColor", json_string("white"));
    json_object_set_new(params, "fontSize", json_integer(60));
    json_object_set_new(params, "textBGColor", json_string("black"));

    // Fill the root object
    json_object_set_new(root, "apiVersion", json_string("1.0"));
    json_object_set_new(root, "context", json_string("abc"));
    json_object_set_new(root, "method", json_string("addText"));
    json_object_set_new(root, "params", params);
```
---
## Build

```bash
docker build --tag dynamic-overlay --build-arg ARCH=aarch64 .

```
```bash
docker cp $(docker create dynamic-overlay):/opt/app ./build

```