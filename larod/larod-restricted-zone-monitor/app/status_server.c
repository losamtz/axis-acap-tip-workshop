#include "status_server.h"

#include <fcgiapp.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

static GMutex snapshot_lock;
static gchar* snapshot;
static GThread* worker;
static int listener = -1;
static gint stopping;

static gpointer serve(gpointer unused) {
    (void)unused;
    FCGX_Request request;
    if (FCGX_InitRequest(&request, listener, 0) != 0) {
        syslog(LOG_ERR, "Cannot initialize FastCGI request");
        return NULL;
    }
    while (!g_atomic_int_get(&stopping)) {
        if (FCGX_Accept_r(&request) != 0) {
            /* Idle accept/read timeouts must not stop the status service. */
            if (!g_atomic_int_get(&stopping)) {
                g_usleep(100000);
            }
            continue;
        }
        const gchar* path = FCGX_GetParam("SCRIPT_NAME", request.envp);
        const gchar* method = FCGX_GetParam("REQUEST_METHOD", request.envp);
        const gchar* status = "200 OK";
        const gchar* extra = "";
        gchar* body = NULL;
        if (g_strcmp0(path, "/local/larod_restricted_zone/status.cgi") != 0) {
            status = "404 Not Found";
            body = g_strdup("{\"error\":\"Not found\"}");
        } else if (g_strcmp0(method, "GET") != 0) {
            status = "405 Method Not Allowed";
            extra = "Allow: GET\r\n";
            body = g_strdup("{\"error\":\"Use GET\"}");
        } else {
            g_mutex_lock(&snapshot_lock);
            body = g_strdup(snapshot);
            g_mutex_unlock(&snapshot_lock);
            if (!body) {
                status = "503 Service Unavailable";
                body = g_strdup("{\"error\":\"Starting\"}");
            }
        }
        FCGX_FPrintF(request.out,
                     "Status: %s\r\n%sContent-Type: application/json\r\n"
                     "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n%s",
                     status, extra, body);
        g_free(body);
        FCGX_Finish_r(&request);
    }
    FCGX_Free(&request, 1);
    return NULL;
}

gboolean status_server_start(void) {
    const gchar* path = g_getenv("FCGI_SOCKET_NAME");
    if (!path || !*path) {
        syslog(LOG_ERR, "FastCGI startup: FCGI_SOCKET_NAME is missing");
        return FALSE;
    }
    if (FCGX_Init() != 0) {
        syslog(LOG_ERR, "FastCGI startup: FCGX_Init failed");
        return FALSE;
    }
    listener = FCGX_OpenSocket(path, 5);
    if (listener < 0) {
        syslog(LOG_ERR, "FastCGI startup: cannot open socket %s: %s", path, strerror(errno));
        return FALSE;
    }
    /* The device web server runs as a different user. HTTP access is restricted
     * to admin in manifest.json, as in the workshop's FastCGI examples. */
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    if (chmod(path, 0666) != 0 ||
        setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(listener, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        syslog(LOG_ERR, "FastCGI startup: socket permissions or timeout setup failed: %s",
               strerror(errno));
        close(listener);
        listener = -1;
        return FALSE;
    }
    g_atomic_int_set(&stopping, 0);
    worker = g_thread_new("zone-status", serve, NULL);
    syslog(LOG_INFO, "FastCGI status service listening");
    return TRUE;
}

void status_server_publish(const gchar* json) {
    g_mutex_lock(&snapshot_lock);
    g_free(snapshot);
    snapshot = g_strdup(json);
    g_mutex_unlock(&snapshot_lock);
}

void status_server_stop(void) {
    if (worker) {
        g_atomic_int_set(&stopping, 1);
        FCGX_ShutdownPending();
        shutdown(listener, SHUT_RDWR);
        g_thread_join(worker);
        worker = NULL;
    }
    if (listener >= 0) {
        close(listener);
        listener = -1;
    }
    g_clear_pointer(&snapshot, g_free);
}
