#include <mdb/connection.h>
#include <mdb/error.h>
#include <mdb/subscriber.h>

/* Keep topic/source to show in envelope */
typedef struct channel_identifier {
    const char *topic;
    const char *source;
} channel_identifier_t;

void on_connection_error(const mdb_error_t *error, void *user_data);
void on_message(const mdb_message_t *message, void *user_data);
void on_done_subscriber_create(const mdb_error_t *error, void *user_data);
void syslog_payload_json(const mdb_message_payload_t* p);