/**
 * Single-threaded CivetWeb reverse-proxy backend with AXParameter + Jansson
 */
#include "civetweb.h"
#include <axsdk/axparameter.h>
#include <jansson.h>
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
static AXParameter* g_param = NULL;

/* ── helpers ─────────────────────────────────────────────────────────────── */

__attribute__((noreturn)) __attribute__((format(printf,1,2)))
static void panic(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsyslog(LOG_ERR, fmt, ap); va_end(ap);
    closelog();
    exit(EXIT_FAILURE);
}

static void on_signal(int signo) { (void)signo; running = 0; }

static void send_json(struct mg_connection* c, int status, json_t* obj) {
    char* body = json_dumps(obj, JSON_COMPACT);
    mg_printf(c,
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

static char* read_body(struct mg_connection* c) {
    const char* len_str = mg_get_header(c, "Content-Length");
    long len = len_str ? strtol(len_str, NULL, 10) : 0;
    if (len <= 0 || len > (1<<20)) return NULL;
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) return NULL;
    long got = mg_read(c, buf, (size_t)len);
    if (got < 0) got = 0;
    buf[got] = '\0';
    return buf;
}

/* ── AXParameter helpers (no mutex needed: single-threaded) ──────────────── */

static gboolean add_if_missing(const char* name, const char* def, const char* meta) {
    GError* err = NULL;
    if (!ax_parameter_add(g_param, name, def, meta, &err)) {
        if (err && err->code == AX_PARAMETER_PARAM_ADDED_ERROR) {
            g_error_free(err);
            return TRUE; // already exists
        }
        if (err) { syslog(LOG_ERR, "add(%s) failed: %s", name, err->message); g_error_free(err); }
        return FALSE;
    }
    return TRUE;
}

static char* get_param_dup(const char* name) {
    GError* err = NULL;
    char* val = NULL;
    if (!ax_parameter_get(g_param, name, &val, &err)) {
        if (err) { syslog(LOG_WARNING, "get(%s) failed: %s", name, err->message); g_error_free(err); }
        return NULL;
    }
    return val; // must g_free
}

static gboolean set_param(const char* name, const char* value) {
    GError* err = NULL;
    gboolean ok = ax_parameter_set(g_param, name, value, FALSE, &err);
    if (!ok && err) {
        syslog(LOG_ERR, "set(%s=%s) failed: %s", name, value, err->message);
        g_error_free(err);
    }
    return ok;
}

/* ── Handlers ────────────────────────────────────────────────────────────── */

// GET /info-acap.cgi
static int InfoHandler(struct mg_connection* c, void* ud __attribute__((unused))) {
    if (strcmp(mg_get_request_info(c)->request_method, "GET") != 0) return 0;

    json_t* out = json_object();

    char* addr = get_param_dup("MulticastAddress");
    char* port = get_param_dup("MulticastPort");

    json_object_set_new(out, "MulticastAddress", addr ? json_string(addr) : json_null());
    json_object_set_new(out, "MulticastPort",   port ? json_string(port) : json_null());
    json_object_set_new(out, "ok", json_true());

    if (addr) g_free(addr);
    if (port) g_free(port);

    send_json(c, 200, out);
    json_decref(out);
    return 1;
}

// POST /param-acap.cgi
static int ParamHandler(struct mg_connection* c, void* ud __attribute__((unused))) {
    if (strcmp(mg_get_request_info(c)->request_method, "POST") != 0) return 0;

    char* body = read_body(c);
    if (!body) {
        json_t* err = json_pack("{s:s}", "error", "Missing or empty body");
        send_json(c, 400, err);
        json_decref(err);
        return 1;
    }

    json_error_t jerr;
    json_t* root = json_loads(body, 0, &jerr);
    free(body);

    if (!root || !json_is_object(root)) {
        if (root) json_decref(root);
        json_t* err = json_pack("{s:s}", "error", "Invalid JSON");
        send_json(c, 400, err);
        json_decref(err);
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
    send_json(c, 200, res);

    json_decref(res);
    json_decref(root);
    return 1;
}

// GET /
static int RootHandler(struct mg_connection* c, void* ud __attribute__((unused))) {
    FILE* f = fopen("html/index.html", "r");
    if (!f) { mg_printf(c, "HTTP/1.1 500 Internal Server Error\r\n\r\n"); return 1; }
    mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) mg_write(c, buf, n);
    fclose(f);
    return 1;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    openlog(APP_NAME, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting %s (CivetWeb single-threaded)", APP_NAME);

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    // Init AXParameter
    GError* gerr = NULL;
    g_param = ax_parameter_new(APP_NAME, &gerr);
    if (!g_param) panic("ax_parameter_new failed: %s", gerr ? gerr->message : "unknown");

    // Ensure parameters exist
    if (!add_if_missing("MulticastAddress", "224.0.0.1", "string")) panic("add MulticastAddress failed");
    if (!add_if_missing("MulticastPort",   "1024",      "string")) panic("add MulticastPort failed");

    // Start CivetWeb in single-threaded mode
    const char* opts[] = {
        "listening_ports", PORT,
        "request_timeout_ms", "10000",
        "num_threads", "1",                // <── single-threaded
        0
    };
    mg_init_library(0);
    struct mg_callbacks cb; memset(&cb, 0, sizeof(cb));
    struct mg_context* ctx = mg_start(&cb, NULL, opts);
    if (!ctx) panic("Failed to start CivetWeb on %s", PORT);
    syslog(LOG_INFO, "CivetWeb listening on %s", PORT);

    // Register handlers (reverse proxy strips /local/my_web_server)
    mg_set_request_handler(ctx, "/",               RootHandler,  NULL);
    mg_set_request_handler(ctx, "/info-acap.cgi",  InfoHandler,  NULL);
    mg_set_request_handler(ctx, "/param-acap.cgi", ParamHandler, NULL);

    while (running) sleep(1);

    mg_stop(ctx);
    ax_parameter_free(g_param);
    closelog();
    return EXIT_SUCCESS;
}
