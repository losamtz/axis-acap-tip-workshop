/**
 * Single-threaded CivetWeb reverse-proxy backend with AXParameter + Jansson
 */
#include "civetweb.h"
#include <axsdk/axparameter.h>
#include <jansson.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define APP_NAME "web_proxy"
#define PORT     "2001"

static volatile sig_atomic_t running = 1;
static AXParameter* handle = NULL;

/* ── helpers ─────────────────────────────────────────────────────────────── */

__attribute__((noreturn)) __attribute__((format(printf,1,2)))
static void panic(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsyslog(LOG_ERR, fmt, ap); va_end(ap);
    closelog();
    exit(EXIT_FAILURE);
}

static void on_signal(int signo) { 

    (void)signo; 
    running = 0; 
}

static void send_json(struct mg_connection* conn, int status, json_t* obj) {

    char* body = json_dumps(obj, JSON_COMPACT);

    mg_printf(conn,
              "HTTP/1.1 %d OK\r\n"
              "Content-Type: application/json\r\n"
              "Cache-Control: no-store\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Headers: content-type\r\n"
              "Connection: close\r\n\r\n"
              "%s",
              status, body ? body : "{}");

    free(body);
}

static char* read_body(struct mg_connection* conn) { 

    const char* len_str = mg_get_header(conn, "Content-Length");
    long len = len_str ? strtol(len_str, NULL, 10) : 0;

    if (len <= 0 || len > (1<<20)) 
        return NULL;

    char* buf = (char*)malloc((size_t)len + 1);

    if (!buf) 
        return NULL;

    long got = mg_read(conn, buf, (size_t)len);

    if (got < 0) 
        got = 0;

    buf[got] = '\0';
    return buf;
}



static char* get_param(const char* name) {

    GError* error = NULL;
    char* value = NULL;

    if (!ax_parameter_get(handle, name, &value, &error)) {
        if (error) { 
            syslog(LOG_WARNING, "get(%s) failed: %s", name, error->message); 
            g_error_free(error); 
        }
        return NULL;
    }
    return value; // must g_free
}

static gboolean set_param(const char* name, const char* value) {

    GError* error = NULL;

    gboolean ok = ax_parameter_set(handle, name, value, FALSE, &error);

    if (!ok && error) {
        syslog(LOG_ERR, "set(%s=%s) failed: %s", name, value, error->message);
        g_error_free(error);
    }

    return ok;
}

/* ── Handlers ────────────────────────────────────────────────────────────── */

// GET /info
static int InfoHandler(struct mg_connection* conn, void* ud __attribute__((unused))) {

    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) 
        return 0;

    json_t* out = json_object();

    char* addr = get_param("MulticastAddress");
    char* port = get_param("MulticastPort");

    json_object_set_new(out, "MulticastAddress", addr ? json_string(addr) : json_null());
    json_object_set_new(out, "MulticastPort",   port ? json_string(port) : json_null());
    json_object_set_new(out, "ok", json_true());

    if (addr) g_free(addr);
    if (port) g_free(port);

    send_json(conn, 200, out);
    json_decref(out);
    return 1;
}

// POST /param
static int ParamHandler(struct mg_connection* conn, void* ud __attribute__((unused))) {

    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) 
        return 0;

    char* body = read_body(conn);
    if (!body) {

        json_t* error = json_pack("{s:s}", "error", "Missing or empty body");

        send_json(conn, 400, error);
        json_decref(error);
        return 1;
    }

    json_error_t jerror;
    json_t* root = json_loads(body, 0, &jerror);
    free(body);

    if (!root || !json_is_object(root)) {
        if (root) 
            json_decref(root);

        json_t* error = json_pack("{s:s}", "error", "Invalid JSON");
        send_json(conn, 400, error);

        json_decref(error);

        return 1;
    }

    const json_t* jAddr = json_object_get(root, "MulticastAddress");
    const json_t* jPort = json_object_get(root, "MulticastPort");

    bool changed = false;
    if (jAddr && json_is_string(jAddr)) changed |= set_param("MulticastAddress", json_string_value(jAddr));
    if (jPort && json_is_string(jPort)) changed |= set_param("MulticastPort",   json_string_value(jPort));

    json_t* res = json_object();
    json_object_set_new(res, "ok", json_true());
    json_object_set_new(res, "changed", changed ? json_true() : json_false());
    send_json(conn, 200, res);

    json_decref(res);
    json_decref(root);
    return 1;
}

// GET /
static int RootHandler(struct mg_connection* conn, void* ud __attribute__((unused))) {

    char buf[4096]; 
    size_t n;

    FILE* file = fopen("html/index.html", "r");
    if (!file) { 
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\n"); 
        return 1; 
    }

    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");

    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) 
        mg_write(conn, buf, n);

    fclose(file);
    return 1;
}
// Function to troubleshoot URI
static int cb_begin_request(struct mg_connection *conn) {

    const struct mg_request_info *req_info = mg_get_request_info(conn);

    syslog(LOG_INFO, "URI: %s  method: %s", 
        req_info->request_uri ? req_info->request_uri : "", 
        req_info->request_method ? req_info->request_method : "");

    return 0; // 0 = continue with normal processing
}
/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {

    openlog(APP_NAME, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting %s (CivetWeb single-threaded)", APP_NAME);

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    // Init AXParameter
    GError* error = NULL;
    handle = ax_parameter_new(APP_NAME, &error);

    if (!handle) 
        panic("ax_parameter_new failed: %s", error ? error->message : "unknown");


    // Start CivetWeb in single-threaded mode
    const char* opts[] = {"listening_ports", PORT, "request_timeout_ms", "10000", "error_log_file", "error.log", 0};

    mg_init_library(0);

    struct mg_callbacks cb; 
    memset(&cb, 0, sizeof(cb));
    cb.begin_request = cb_begin_request; // Log requests

    struct mg_context* ctx = mg_start(&cb, NULL, opts);

    if (!ctx) 
        panic("Failed to start CivetWeb on %s", PORT);

    syslog(LOG_INFO, "CivetWeb listening on %s", PORT);

    // Register handlers 
    mg_set_request_handler(ctx, "/", RootHandler,  NULL);
    mg_set_request_handler(ctx, "/local/web_proxy/api/info",  InfoHandler,  NULL);
    mg_set_request_handler(ctx, "/local/web_proxy/api/param",  ParamHandler,  NULL);

    while (running) 
        sleep(1);

    mg_stop(ctx);
    ax_parameter_free(handle);
    closelog();
    return EXIT_SUCCESS;
}
