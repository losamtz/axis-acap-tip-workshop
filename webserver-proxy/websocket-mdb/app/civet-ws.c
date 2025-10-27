#include "civetweb.h"
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <glib.h>
#include <glib-unix.h>
#include "civet-ws.h"


/* Track connected WS clients (thread-safe) */
static GMutex g_clients_mtx;
static GList *g_clients = NULL;  /* items are struct mg_connection* */

/* ---- CivetWeb: HTTP GET / -> serve html/index.html ---- */
int RootHandler(struct mg_connection *conn, void *ud) {
    (void)ud;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0) return 0;

    FILE *fp = fopen("html/index.html", "rb");
    if (!fp) {
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n\r\n"
                        "index.html missing\n");
        return 1;
    }

    mg_printf(conn, "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Cache-Control: no-store\r\n"
                    "Connection: close\r\n\r\n");

    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) mg_write(conn, buf, n);
    fclose(fp);
    return 1;
}

/* ---- CivetWeb: WebSocket handlers ---- */
int ws_connect_handler(const struct mg_connection *conn, void *ud) {
    (void)ud;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    syslog(LOG_INFO, "WS connect from %s %s", ri->remote_addr, ri->local_uri);
    return 0; /* 0 = accept */
}

void ws_ready_handler(struct mg_connection *conn, void *ud) {
    (void)ud;
    g_mutex_lock(&g_clients_mtx);
    g_clients = g_list_prepend(g_clients, conn);
    g_mutex_unlock(&g_clients_mtx);
    syslog(LOG_INFO, "WS ready; clients=%d", g_list_length(g_clients));
}

int ws_data_handler(struct mg_connection *conn, int flags, char *data, size_t len, void *ud) {
    (void)conn; (void)flags; (void)data; (void)len; (void)ud;
    /* We don't expect inbound messages; ignore. Return 1 to keep connection. */
    return 1;
}

void ws_close_handler(const struct mg_connection *conn, void *ud) {
    (void)ud;
    g_mutex_lock(&g_clients_mtx);
    for (GList *it = g_clients; it; it = it->next) {
        if (it->data == conn) { g_clients = g_list_delete_link(g_clients, it); break; }
    }
    int left = g_list_length(g_clients);
    g_mutex_unlock(&g_clients_mtx);
    syslog(LOG_INFO, "WS closed; clients=%d", left);
}

/* Send to all connected clients (best-effort) */
void broadcast_text(const char *msg, size_t len) {
    g_mutex_lock(&g_clients_mtx);
    for (GList *it = g_clients; it; it = it->next) {
        struct mg_connection *c = (struct mg_connection *)it->data;
        mg_websocket_write(c, MG_WEBSOCKET_OPCODE_TEXT, msg, len);
    }
    g_mutex_unlock(&g_clients_mtx);
}