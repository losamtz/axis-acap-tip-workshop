/**
 * ACAP: MDB subscriber -> CivetWeb WebSocket broadcaster (+ serves index.html)
 *
 * Subscribes to:
 *   topic:  "com.axis.analytics_scene_description.v0.beta"
 *   source: "1"   (video channel 1; adjust if needed)
 *
 * Every received ADF payload is wrapped into a small JSON envelope and sent
 * to all connected WS clients at "/ws".
 *
 * Build PKGS: glib-2.0 mdb jansson
 * Link CivetWeb: -lcivetweb -lpthread (and -lssl -lcrypto if your build is HTTPS)
 */

#include "civetweb.h"
#include <mdb/connection.h>
#include <mdb/error.h>
#include <mdb/subscriber.h>

#include "mdb-callbacks.h"
#include "civet-ws.h"

#include <jansson.h>
#include <glib.h>
#include <glib-unix.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ---- Config ---- */
#define APP_NAME "websocket_mdb"
#define PORT     "2001"                                     /* CivetWeb listen port */
#define WS_PATH  "/local/websocket_mdb/ws"                  /* WebSocket endpoint */
#define PAGE_PATH "/"                                       /* Serves html/index.html */

/* CivetWeb opcode constant (name differs across versions) */
#ifndef MG_WEBSOCKET_OPCODE_TEXT
#define MG_WEBSOCKET_OPCODE_TEXT 0x1
#endif

/* ---- Globals ---- */
static volatile sig_atomic_t g_running = 1;



/* ---- Util ---- */
__attribute__((noreturn)) __attribute__((format(printf,1,2)))
static void panic(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsyslog(LOG_ERR, fmt, ap); va_end(ap);
    closelog();
    exit(EXIT_FAILURE);
}

static void on_signal(int signo) {
    (void)signo;
    g_running = 0;
}



/* ---- main ---- */
int main(void) {
    openlog(APP_NAME, LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "Starting %s …", APP_NAME);

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    // 1) #### Start CivetWeb (single process, internal thread pool) #############################
    const char *opts[] = {
        "listening_ports", PORT,
        "request_timeout_ms", "10000",
        "error_log_file", "error.log",
        0
    };

    mg_init_library(0);

    struct mg_callbacks cb; 
    memset(&cb, 0, sizeof(cb));
    struct mg_context *ctx = mg_start(&cb, NULL, opts);

    if (!ctx) 
        panic("Failed to start CivetWeb on %s", PORT);

    mg_set_request_handler(ctx, PAGE_PATH, RootHandler, NULL);
    mg_set_websocket_handler(ctx, WS_PATH,
                             ws_connect_handler,
                             ws_ready_handler,
                             ws_data_handler,
                             ws_close_handler,
                             NULL);

    syslog(LOG_INFO, "HTTP listening on :%s  WS=%s", PORT, WS_PATH);

    // 2) ############### MDB Connection ######################################################

    mdb_error_t *err = NULL;
    mdb_connection_t *connection = mdb_connection_create(on_connection_error, NULL, &err);
    if (err) panic("mdb_connection_create: %s", err->message);

    // 3) ############### MDB subscribe - 2 topics ############################################

    // subscriber for analytics_scene_description.v0.beta
    channel_identifier_t sceneChannel = {
        .topic  = "com.axis.analytics_scene_description.v0.beta",
        .source = "1"  /* video channel */
    };

    mdb_subscriber_config_t *sceneCfg =
        mdb_subscriber_config_create(sceneChannel.topic, sceneChannel.source,
                                     on_message, &sceneChannel, &err);

    if (err) panic("subscriber_config_create: %s", err->message);

    mdb_subscriber_t *sceneSubscriber =
        mdb_subscriber_create_async(connection, sceneCfg,
                                    on_done_subscriber_create, &sceneChannel, &err);
    if (err) panic("subscriber_create_async for scene sub: %s", err->message);
    
    //  subscriber for consolidated_track.v1.beta
    channel_identifier_t consolidatedChannel = {
        .topic  = "com.axis.consolidated_track.v1.beta",
        .source = "1"
    };

    mdb_subscriber_config_t *consolidatedCfg =
        mdb_subscriber_config_create(consolidatedChannel.topic, consolidatedChannel.source,
                                    on_message, &consolidatedChannel, &err);

    mdb_subscriber_t *consolidatedSubscriber =
        mdb_subscriber_create_async(connection, consolidatedCfg,
                                    on_done_subscriber_create,
                                    &consolidatedChannel, &err);

    // 3) #############         Idle loop (systemd controls lifecycle)    #######################

    while (g_running) sleep(1);

    // 4) #############        Cleanup             ##############################################

    mdb_subscriber_destroy(&sceneSubscriber);
    mdb_subscriber_config_destroy(&sceneCfg);
    mdb_subscriber_destroy(&consolidatedSubscriber);
    mdb_subscriber_config_destroy(&consolidatedCfg);
    mdb_connection_destroy(&connection);

    mg_stop(ctx);
    syslog(LOG_INFO, "Stopped %s", APP_NAME);
    closelog();
    return 0;
}