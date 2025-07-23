# Draw text with VAPIX Overlay API


## Description

This application showcase the possibility to use VAPIX dynamic [Overlay API](https://developer.axis.com/vapix/network-video/overlay-api/) and use the existent functionalities that this library offers. 
This library makes it able to add text overlay, images and boxes which can also be configured through the web interface of the camera.

This sample also uses: 
- [VAPIX access for ACAP](https://developer.axis.com/acap/develop/VAPIX-access-for-ACAP-applications/), dbus access to dynamic user credentials 
- [libcurl](https://developer.axis.com/acap/api/native-sdk-api/#curl)
- [jansson](https://developer.axis.com/acap/api/native-sdk-api/#jansson)

# Application flow:

## Retrive credentials with dbus

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

## cURL request

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
        "    \"text\": \"AXIS TIP Paris workshop - Date %c\","
        "    \"position\": \"topLeft\","
        "    \"textColor\": \"white\","
        "    \"fontSize": 14","
        "    \"textBGColor\": \"black\""  
        "   }"
        "}";

    return vapix_post_json(handle, credentials, endpoint, request);
}
```

## Build

```bash
docker build --tag dynamic-overlay --build-arg ARCH=aarch64 .

```
```bash
docker cp $(docker create dynamic-overlay):/opt/app ./build

```