#include <curl/curl.h>
#include <gio/gio.h>
#include <jansson.h>
#include <syslog.h>
#include "panic.h"
#include "vapix-credentials.h"
#include "curl-request.h"


static json_t* build_addtext_request(void) {
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

    return root;
}

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

    
    json_t* request_obj = build_addtext_request();
    char* request = json_dumps(request_obj, JSON_COMPACT);

    return vapix_post_json(handle, credentials, endpoint, request);
}

static const char* response_data(const json_t* props, const char* prop_name) {

    const json_t* data = json_object_get(props, "data");

    if (!json_is_object(data)) {
        syslog(LOG_WARNING, "'data' field is missing or not an object");
        return NULL;
    }

    const json_t* value = json_object_get(data, prop_name);

    if (json_is_string(value)) {
        return json_string_value(value);
    } else if (json_is_integer(value)) {
        // Static buffer for integer to string conversion
        static char buf[32];
        snprintf(buf, sizeof(buf), "%" JSON_INTEGER_FORMAT, json_integer_value(value));
        return buf;
    } else {
        syslog(LOG_WARNING, "Property '%s' not found or not string/integer", prop_name);
        return NULL;
    }
}

int main(void) {
    openlog(NULL, LOG_PID, LOG_USER);

    syslog(LOG_INFO, "Curl version %s", curl_version_info(CURLVERSION_NOW)->version);
    syslog(LOG_INFO, "Jansson version %s", JANSSON_VERSION);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* handle = curl_easy_init();

    char* credentials = retrieve_vapix_credentials("example-vapix-user");

    json_t* response = add_text(handle, credentials);

    char* debug_str = json_dumps(response, JSON_INDENT(2));
    syslog(LOG_INFO, "Full json response:\n%s", debug_str);

    syslog(LOG_INFO, "Camera: %s", response_data(response, "camera"));
    syslog(LOG_INFO, "Identity: %s", response_data(response, "identity"));
    
    free(debug_str);
    json_decref(response);
    free(credentials);
    curl_easy_cleanup(handle);
    curl_global_cleanup();
}