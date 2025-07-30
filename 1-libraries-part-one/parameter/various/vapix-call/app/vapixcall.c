

// 1 - Parse credentials
// 2 - Retrieve vapix credentials
// 3 - Request parameter from param.cgi

#include <curl/curl.h>
#include <gio/gio.h>
#include <jansson.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>

#define APP_NAME "vapixcall"

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list arg;
    va_start(arg, format);
    vsyslog(LOG_ERR, format, arg);
    va_end(arg);
    exit(1);
}

static char* parse_credentials(GVariant* result) {
    char* credentials_string = NULL;
    char* id                 = NULL;
    char* password           = NULL;

    g_variant_get(result, "(&s)", &credentials_string);
    if (sscanf(credentials_string, "%m[^:]:%ms", &id, &password) != 2)
        panic("Error parsing credential string '%s'", credentials_string);

    char* credentials = g_strdup_printf("%s:%s", id, password);

    free(id);
    free(password);
    return credentials;
}

static char* retrieve_vapix_credentials(char* username) {
    GError* error = NULL;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if(!connection)
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

static size_t append_to_gstring_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t processed_bytes = size * nmemb;
    g_string_append_len((GString*)userdata, ptr, processed_bytes);
    return processed_bytes;
}

static char* get_text_vapix_parameter(CURL* handle, const char* credentials, const char* vapix_parameter) {
    GString* response = g_string_new(NULL);
    char* url         = g_strdup_printf("http://127.0.0.12/axis-cgi/param.cgi?action=list&group=%s", vapix_parameter);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_USERPWD, credentials);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_to_gstring_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);

    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK)
        panic("curl_easy_perform error %d: '%s'", res, curl_easy_strerror(res));

    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code != 200)
        panic("Got response code %ld from request with response '%s'",
              response_code,
              response->str);

    free(url);
    return g_string_free(response, FALSE);
}

static char* find_value(char* parameter_str){
    if (!parameter_str) return NULL;

    char* equal_sign = strchr(parameter_str, '=');
    if (!equal_sign) {
        // no '=' found
        return NULL;
    }

    // Move pointer to first char after '='
    char* value_start = equal_sign + 1;

    // Skip leading spaces
    while (*value_start && isspace((unsigned char)*value_start)) {
        value_start++;
    }

    // Trim trailing spaces in-place
    char* end = value_start + strlen(value_start) - 1;
    while (end > value_start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return value_start;
}

int main(void) {
    GError* error = NULL;

    openlog(APP_NAME, LOG_PID, LOG_USER);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* handle = curl_easy_init();

    if(!handle) {
        panic("%s", error->message);
    }

    char* parameter = "root.ImageSource.I0.Rotation";
    char* credentials = retrieve_vapix_credentials("vapix-user-call");
    char* response_text = get_text_vapix_parameter(handle, credentials, parameter);
    char* value = find_value(response_text);

    if(value)
        syslog(LOG_INFO, "Parameter root.ImageSource.I0.Rotation = '%s'", value);
    else 
        syslog(LOG_WARNING, "No value found in response");
    

    free(response_text);
    free(credentials);

    curl_easy_cleanup(handle);
    curl_global_cleanup();

    closelog();
    return 0;
}