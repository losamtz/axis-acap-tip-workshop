#include "event.h"
#include <syslog.h>

static bool payload(AXEventKeyValueSet* values, bool active, bool available, GError** error) {
    gboolean alarm_value = active;
    gboolean available_value = available;
    return ax_event_key_value_set_add_key_value(values, "active", NULL, &alarm_value,
                                                AX_VALUE_TYPE_BOOL, error) &&
           ax_event_key_value_set_add_key_value(values, "available", NULL, &available_value,
                                                AX_VALUE_TYPE_BOOL, error);
}

static void declared(guint declaration, gpointer user_data) {
    AlarmEvent* event = user_data;
    event->declaration = declaration;
    event->ready = true;
}

bool alarm_event_init(AlarmEvent* event) {
    event->handler = ax_event_handler_new();
    if (!event->handler) {
        return false;
    }
    GError* error = NULL;
    AXEventKeyValueSet* values = ax_event_key_value_set_new();
    bool ok =
        ax_event_key_value_set_add_key_value(values, "topic0", "tnsaxis",
                                             "CameraApplicationPlatform", AX_VALUE_TYPE_STRING,
                                             &error) &&
        ax_event_key_value_set_add_key_value(values, "topic1", "tnsaxis", "LarodRestrictedZone",
                                             AX_VALUE_TYPE_STRING, &error) &&
        ax_event_key_value_set_add_key_value(values, "topic2", "tnsaxis", "PersonPresence",
                                             AX_VALUE_TYPE_STRING, &error) &&
        payload(values, false, false, &error) &&
        ax_event_key_value_set_mark_as_data(values, "active", NULL, &error) &&
        ax_event_key_value_set_mark_as_data(values, "available", NULL, &error) &&
        ax_event_key_value_set_add_nice_names(values, "topic1", "tnsaxis", NULL,
                                              "Larod restricted zone", &error) &&
        ax_event_key_value_set_add_nice_names(values, "topic2", "tnsaxis", NULL,
                                              "Person presence alarm", &error) &&
        ax_event_handler_declare2(event->handler, values, FALSE, "active", &event->declaration,
                                  declared, event, &error);
    ax_event_key_value_set_free(values);
    if (error) {
        syslog(LOG_ERR, "Event declaration: %s", error->message);
        g_clear_error(&error);
    }
    return ok;
}

bool alarm_event_update(AlarmEvent* event, bool active, bool available) {
    if (!event->ready) {
        return false;
    }
    if (event->sent && active == event->active && available == event->available) {
        return true;
    }
    GError* error = NULL;
    AXEventKeyValueSet* values = ax_event_key_value_set_new();
    bool ok = payload(values, active, available, &error);
    AXEvent* message = ok ? ax_event_new2(values, NULL) : NULL;
    ax_event_key_value_set_free(values);
    ok =
        message && ax_event_handler_send_event(event->handler, event->declaration, message, &error);
    if (message) {
        ax_event_free(message);
    }
    if (ok) {
        event->sent = true;
        event->active = active;
        event->available = available;
        syslog(LOG_INFO, "PersonPresence active=%d available=%d", active, available);
    } else {
        syslog(LOG_WARNING, "Event send: %s", error ? error->message : "failed");
    }
    g_clear_error(&error);
    return ok;
}

void alarm_event_destroy(AlarmEvent* event) {
    if (event->handler) {
        alarm_event_update(event, false, false);
        ax_event_handler_free(event->handler);
        event->handler = NULL;
    }
}
