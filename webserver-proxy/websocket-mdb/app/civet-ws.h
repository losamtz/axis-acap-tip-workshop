#include <stdio.h>
#include "civetweb.h"

int RootHandler(struct mg_connection *conn, void *ud);
int ws_connect_handler(const struct mg_connection *conn, void *ud);
void ws_ready_handler(struct mg_connection *conn, void *ud);
int ws_data_handler(struct mg_connection *conn, int flags, char *data, size_t len, void *ud);
void ws_close_handler(const struct mg_connection *conn, void *ud);
void broadcast_text(const char *msg, size_t len);