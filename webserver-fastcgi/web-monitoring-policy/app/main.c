#define _POSIX_C_SOURCE 200809L
#include "policy.h"
#include <axsdk/axparameter.h>
#include <errno.h>
#include <fcgiapp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#define APP_NAME "web_monitoring_policy"
#define APP_URL_PREFIX "/local/" APP_NAME "/"

/* Signal handlers only set a flag. Cleanup happens in the request loop. */
static volatile sig_atomic_t stopping;
static void request_shutdown(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

/* Camera-specific adapters. The policy module can use a fake store in tests. */
static gboolean read_parameter(void* context, const char* name, char** value) {
    GError* error = NULL;
    gboolean ok = ax_parameter_get(context, name, value, &error);
    if (error) {
        syslog(LOG_WARNING, "Read %s: %s", name, error->message);
        g_error_free(error);
    }
    return ok;
}

static gboolean write_parameter(void* context, const char* name, const char* value) {
    GError* error = NULL;
    /* TRUE is do_sync: persist the value and trigger parameter callbacks. */
    gboolean ok = ax_parameter_set(context, name, value, TRUE, &error);
    if (error) {
        syslog(LOG_WARNING, "Write %s: %s", name, error->message);
        g_error_free(error);
    }
    return ok;
}

static json_t* make_error_response(const char* text) {
    return json_pack("{s:b,s:s}", "ok", FALSE, "error", text);
}

static const char* http_status_phrase(int status_code) {
    switch (status_code) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Content Too Large";
    case 415:
        return "Unsupported Media Type";
    case 422:
        return "Unprocessable Content";
    default:
        return "Internal Server Error";
    }
}

/* Takes ownership of object: callers must not decref it after sending. */
static void send_json_response(FCGX_Request* request, int status_code, json_t* object,
                               const char* allow) {
    char* body = json_dumps(object, JSON_COMPACT);
    FCGX_FPrintF(request->out,
                 "Status: %d %s\r\nContent-Type: application/json\r\nCache-Control: "
                 "no-store\r\nX-Content-Type-Options: nosniff\r\n",
                 status_code, http_status_phrase(status_code));
    if (allow) {
        FCGX_FPrintF(request->out, "Allow: %s\r\n", allow);
    }
    FCGX_FPrintF(request->out, "\r\n%s", body ? body : "{\"ok\":false}");
    free(body);
    json_decref(object);
}

static json_t* read_json_body(FCGX_Request* request, int* status_code, const char** error) {
    const char* content_type = FCGX_GetParam("CONTENT_TYPE", request->envp);
    if (!content_type || g_ascii_strncasecmp(content_type, "application/json", 16) ||
        (content_type[16] && content_type[16] != ';')) {
        *status_code = 415;
        *error = "Use Content-Type: application/json";
        return NULL;
    }
    const char* content_length = FCGX_GetParam("CONTENT_LENGTH", request->envp);
    guint64 body_size;
    if (!content_length ||
        !g_ascii_string_to_unsigned(content_length, 10, 1, G_MAXUINT32, &body_size, NULL)) {
        *status_code = 400;
        *error = "Invalid Content-Length";
        return NULL;
    }
    if (body_size > 4096) {
        *status_code = 413;
        *error = "Request body exceeds 4096 bytes";
        return NULL;
    }
    /* FastCGI may return fewer bytes than requested. Read the entire body. */
    char body[4096];
    guint bytes_received = 0;
    while (bytes_received < body_size && !stopping) {
        int bytes_read =
            FCGX_GetStr(body + bytes_received, (int)body_size - bytes_received, request->in);
        if (bytes_read <= 0) {
            break;
        }
        bytes_received += (guint)bytes_read;
    }
    if (bytes_received != body_size) {
        *status_code = 400;
        *error = "Incomplete request body";
        return NULL;
    }
    /* Reject duplicate keys so a request has only one meaning. */
    json_t* root = json_loadb(body, body_size, JSON_REJECT_DUPLICATES, NULL);
    if (!root) {
        *status_code = 400;
        *error = "Invalid JSON or duplicate field";
        return NULL;
    }
    return root;
}

static void handle_request(FCGX_Request* request, Store* store) {
    const char* path = FCGX_GetParam("SCRIPT_NAME", request->envp);
    const char* method = FCGX_GetParam("REQUEST_METHOD", request->envp);
    gboolean is_settings_endpoint = g_strcmp0(path, APP_URL_PREFIX "settings.cgi") == 0;
    gboolean is_test_endpoint = g_strcmp0(path, APP_URL_PREFIX "test.cgi") == 0;
    if (!is_settings_endpoint && !is_test_endpoint) {
        send_json_response(request, 404, make_error_response("Unknown endpoint"), NULL);
        return;
    }
    if (is_settings_endpoint && !g_strcmp0(method, "GET")) {
        Policy policy = {0};
        gboolean ok = policy_read(store, &policy);
        json_t* result =
            ok ? json_pack("{s:b}", "ok", TRUE) : make_error_response("Cannot read saved settings");
        if (ok) {
            json_object_set_new(result, "settings", policy_json(&policy));
        }
        send_json_response(request, ok ? 200 : 500, result, NULL);
        return;
    }
    if (g_strcmp0(method, "POST")) {
        send_json_response(request, 405, make_error_response("Unsupported method"),
                           is_settings_endpoint ? "GET, POST" : "POST");
        return;
    }
    /* No CORS headers; this header prevents cross-origin HTML form submissions. */
    if (g_strcmp0(FCGX_GetParam("HTTP_X_POLICY_REQUEST", request->envp), "1")) {
        send_json_response(request, 403, make_error_response("Missing X-Policy-Request header"),
                           NULL);
        return;
    }
    int status_code = 200;
    const char* error = NULL;
    json_t* input = read_json_body(request, &status_code, &error);
    if (!input) {
        send_json_response(request, status_code, make_error_response(error), NULL);
        return;
    }
    json_t* result = is_settings_endpoint ? policy_save(store, input, &status_code)
                              : policy_test(store, input, &status_code);
    json_decref(input);
    send_json_response(request, status_code, result, NULL);
}

int main(void) {
    openlog(APP_NAME, LOG_PID, LOG_USER);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action = {0};
    action.sa_handler = request_shutdown;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    GError* error = NULL;
    AXParameter* parameters = ax_parameter_new(APP_NAME, &error);
    int result = EXIT_FAILURE;
    int listener = -1;
    if (!parameters) {
        syslog(LOG_ERR, "Parameter initialization: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        return result;
    }
    const char* socket_path = g_getenv("FCGI_SOCKET_NAME");
    if (!socket_path || FCGX_Init()) {
        goto cleanup;
    }
    listener = FCGX_OpenSocket(socket_path, 5);
    if (listener < 0) {
        goto cleanup;
    }
    struct timeval timeout = {.tv_sec = 2};
    /* The camera web server needs socket access. manifest.json restricts HTTP to admin. */
    if (chmod(socket_path, 0666) ||
        setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(listener, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
        goto cleanup;
    }
    FCGX_Request request;
    if (FCGX_InitRequest(&request, listener, 0)) {
        goto cleanup;
    }
    Store store = {.context = parameters, .read = read_parameter, .write = write_parameter};
    /* One request at a time: read parameters, handle the request, finish it. */
    while (!stopping) {
        if (FCGX_Accept_r(&request)) {
            if (!stopping) {
                g_usleep(100000);
            }
            continue;
        }
        handle_request(&request, &store);
        FCGX_Finish_r(&request);
    }
    FCGX_Free(&request, 1);
    result = EXIT_SUCCESS;
cleanup:
    if (result) {
        syslog(LOG_ERR, "FastCGI initialization failed");
    }
    if (listener >= 0) {
        close(listener);
    }
    ax_parameter_free(parameters);
    closelog();
    return result;
}
