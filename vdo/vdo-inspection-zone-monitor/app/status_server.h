#ifndef STATUS_SERVER_H
#define STATUS_SERVER_H

#include <glib.h>

/* The GLib main thread publishes immutable JSON snapshots. The HTTP worker
 * never touches the monitor or the Parameter API. */
gboolean status_server_start(void);
gboolean status_server_take_calibration(void);
void status_server_publish(const gchar* json);
void status_server_stop(void);

#endif
