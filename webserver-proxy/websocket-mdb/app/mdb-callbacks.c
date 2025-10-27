#include <mdb/connection.h>
#include <mdb/error.h>
#include <mdb/subscriber.h>
#include "mdb_callbacks.h"
#include "civet-ws.h"
#include <syslog.h>
#include <jansson.h>
#include <string.h>

/* ---- MDB subscriber callbacks ---- */
void on_connection_error(const mdb_error_t *error, void *user_data) {
    (void)user_data;
    syslog(LOG_ERR, "MDB connection error: %s", error ? error->message : "unknown");
    /* Abort: camera-side infra broken; let systemd restart the app */
    abort();
}

void on_message(const mdb_message_t *message, void *user_data) {

    const struct timespec *ts = mdb_message_get_timestamp(message);
    const mdb_message_payload_t *payload = mdb_message_get_payload(message);

    syslog(LOG_INFO, "Message at %lld.%09ld, size=%zu",
           (long long)ts->tv_sec, ts->tv_nsec, payload ? payload->size : 0);
    
    //syslog_payload_json(payload);   // pretty-print if JSON

    const channel_identifier_t *chid = (const channel_identifier_t *)user_data;

    /* Wrap the ADF payload as text (it's already JSON); also provide topic/source + ts */
    json_t *root = json_object();
    json_object_set_new(root, "topic",  json_string(chid->topic));
    json_object_set_new(root, "source", json_string(chid->source));


    /* Try parse ADF as JSON; if it fails, send it as raw string */
    json_error_t jerr;
    json_t *adf = json_loadb((const char *)payload->data, payload->size, 0, &jerr);
    if (adf && json_is_object(adf)) {
        /* Copy all top–level fields from the ADF object into root.
           For scene metadata, this will add the "frame" object
           directly alongside "topic" and "source". */
        void *iter = json_object_iter(adf);
        while (iter) {
            const char *key = json_object_iter_key(iter);
            json_t *value   = json_object_iter_value(iter);
            /* Use json_object_set (not *_set_new) so we don’t steal
               ownership of 'value'. */
            json_object_set(root, key, value);
            iter = json_object_iter_next(adf, iter);
        }
        json_decref(adf);
    } else {
        /* Parsing failed – just include the raw string */
        json_object_set_new(root,
                            "data",
                            json_stringn((const char *)payload->data,
                            payload->size));
    }

    char *body = json_dumps(root, JSON_COMPACT);
    if (body) {
        broadcast_text(body, strlen(body));
        free(body);
    }
    json_decref(root);
}

void on_done_subscriber_create(const mdb_error_t *error, void *user_data) {
    const channel_identifier_t *chid = (const channel_identifier_t *)user_data;
    if (error) {
        syslog(LOG_ERR, "MDB subscribe error: %s", error->message);
        abort();
    }
    syslog(LOG_INFO, "MDB subscribed to topic=%s source=%s", chid->topic, chid->source);
}

void syslog_payload_json(const mdb_message_payload_t* p) {
    if (!p || !p->data) { syslog(LOG_INFO, "Payload: (null)"); return; }

    // Copy + NUL-terminate for jansson
    char *copy = (char*)malloc(p->size + 1);
    if (!copy) { syslog(LOG_ERR, "OOM logging JSON"); return; }
    memcpy(copy, p->data, p->size);
    copy[p->size] = '\0';

    json_error_t jerr;
    json_t *root = json_loads(copy, 0, &jerr);
    if (!root) {
        syslog(LOG_WARNING, "Payload is not JSON (line %d col %d): %s",
               jerr.line, jerr.column, jerr.text);
        free(copy);
        return;
    }

    char *pretty = json_dumps(root, JSON_INDENT(2) | JSON_ENSURE_ASCII);
    if (pretty) {
        syslog(LOG_INFO, "Payload JSON:\n%s", pretty);
        free(pretty);
    }
    json_decref(root);
    free(copy);
}