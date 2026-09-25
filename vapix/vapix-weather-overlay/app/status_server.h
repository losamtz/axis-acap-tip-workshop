#ifndef STATUS_SERVER_H
#define STATUS_SERVER_H

#include <glib.h>

/* The Weather worker publishes immutable JSON snapshots. The HTTP worker
 * never touches the monitor or the Parameter API. */
gboolean status_server_start(void);
void status_server_publish(const gchar* json);
void status_server_stop(void);

#endif
