#include "http.h"
#include "overlay_protocol.h"
#include <curl/curl.h>
#include <string.h>
#define BODY_LIMIT (256 * 1024)
#define CA_BUNDLE "/usr/local/packages/vapix_weather_overlay/ca-certificates.crt"

char* camera_credentials(GError** error) {
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
    if (!connection) return NULL;
    GVariant* result = g_dbus_connection_call_sync(connection, "com.axis.HTTPConf1",
        "/com/axis/HTTPConf1/VAPIXServiceAccounts1", "com.axis.HTTPConf1.VAPIXServiceAccounts1",
        "GetCredentials", g_variant_new("(s)", "weather-overlay"), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, error);
    g_object_unref(connection);
    if (!result) return NULL;
    const char* value;
    g_variant_get(result, "(&s)", &value);
    char* credentials = strchr(value, ':') ? g_strdup(value) : NULL;
    g_variant_unref(result);
    if (!credentials) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid service credentials");
    return credentials;
}

static size_t collect(char* data, size_t size, size_t count, void* user_data) {
    GString* body = user_data;
    if (size && count > BODY_LIMIT / size) return 0;
    size_t bytes = size * count;
    if (bytes > BODY_LIMIT - body->len) return 0;
    g_string_append_len(body, data, bytes);
    return bytes;
}

json_t* http_json(const char* url, const char* credentials, json_t* request, GError** error) {
    CURL* curl = curl_easy_init();
    if (!curl) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot initialize HTTP client"); return NULL; }
    GString* body = g_string_new(NULL);
    char* payload = request ? json_dumps(request, JSON_COMPACT) : NULL;
    struct curl_slist* headers = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, credentials ? 5L : 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ACAP-Weather-Workshop/1.0");
    if (credentials) {
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_USERPWD, credentials);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    } else {
        curl_easy_setopt(curl, CURLOPT_CAINFO, CA_BUNDLE);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    if (payload) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    }
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    json_t* parsed = NULL;
    if (result == CURLE_COULDNT_RESOLVE_HOST && !credentials)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
            "Cannot resolve api.open-meteo.com from the camera; check camera DNS, gateway, and network DNS access");
    else if (result != CURLE_OK) g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "HTTP transport: %s", curl_easy_strerror(result));
    else if (status != 200) g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "HTTP status %ld", status);
    else {
        parsed = json_loads(body->str, JSON_REJECT_DUPLICATES, NULL);
        if (!json_is_object(parsed)) {
            json_decref(parsed); parsed = NULL;
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Expected a JSON object");
        }
    }
    curl_slist_free_all(headers); free(payload); g_string_free(body, TRUE); curl_easy_cleanup(curl);
    return parsed;
}

json_t* overlay_call(const char* credentials, const char* method, json_t* params, GError** error) {
    json_t* request = overlay_request(method, params);
    json_t* response = http_json("http://127.0.0.12/axis-cgi/dynamicoverlay/dynamicoverlay.cgi", credentials, request, error);
    json_decref(request);
    if (response && (json_object_get(response, "error") || !json_is_object(json_object_get(response, "data")))) {
        /* Do not log arbitrary response bodies or credentials. */
        json_int_t code = json_integer_value(json_object_get(json_object_get(response, "error"), "code"));
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "VAPIX %s failed (code %lld): %s", method, (long long)code,
                    code == 103 ? "invalid request parameter" :
                    code == 100 ? "unsupported API version" : "camera rejected operation");
        json_decref(response); response = NULL;
    }
    return response;
}
