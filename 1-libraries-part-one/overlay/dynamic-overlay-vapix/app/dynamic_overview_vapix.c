#include <curl/curl.h>
#include <gio/gio.h>
#include <jansson.h>
#include <syslog.h>
#include "panic.h"
#include "vapix-credentials.h"


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

static json_t* add_text(CURL* handle, const char* credentials) {
    const char* endpoint = "/axis-cgi/dynamicoverlay/dynamicoverlay.cgi";

    const char* request =
        "{"
        "  \"apiVersion\": \"1.0\","
        "  \"context\": \"abc\","
        "  \"method\": \"addText\","
        "  \"params\": {"
        "    \"camera\": 1,"
        "    \"text\": \"AXIS TIP Paris workshop %c\","
        "    \"position\": \"topLeft\","
        "    \"textColor\": \"white\""        
        "   }"
        "}";

    return vapix_post_json(handle, credentials, endpoint, request);
}

static const char* response_data(const json_t* props, const char* prop_name) {
    const json_t* data       = json_object_get(props, "data");
    const json_t* prop_value = json_object_get(data, prop_name);
    return json_string_value(prop_value);
}

int main(void) {
    openlog(NULL, LOG_PID, LOG_USER);

    syslog(LOG_INFO, "Curl version %s", curl_version_info(CURLVERSION_NOW)->version);
    syslog(LOG_INFO, "Jansson version %s", JANSSON_VERSION);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* handle = curl_easy_init();

    char* credentials = retrieve_vapix_credentials("example-vapix-user");

    json_t* props = add_text(handle, credentials);

    syslog(LOG_INFO, "Camera: %s", response_data(props, "camera"));
    syslog(LOG_INFO, "Identity: %s", response_data(props, "indentity"));
    

    json_decref(props);
    free(credentials);
    curl_easy_cleanup(handle);
    curl_global_cleanup();
}