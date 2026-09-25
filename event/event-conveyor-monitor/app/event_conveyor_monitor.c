#include <axsdk/axevent.h>
#include <axsdk/axparameter.h>
#include <string.h>
#include <glib-unix.h>
#include <jansson.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include "conveyor.h"
#include "status_server.h"

#define APP_NAME "event_conveyor_monitor"
#define HISTORY_SIZE 30

typedef struct App App;
typedef struct {
    App* app;
    const gchar* topic;
    gboolean stateful;
    gboolean ready;
    guint declaration;
    guint subscription;
} Channel;
struct App {
    AXParameter* parameters;
    guint parameter_callbacks;
    AXEventHandler* publisher;
    AXEventHandler* subscriber;
    GMainLoop* loop;
    Channel channels[2];
    Conveyor conveyor;
    gint64 started;
    guint timer;
    gboolean state_sent;
    gboolean published_jam;
    gboolean received_known;
    gboolean received_jam;
    guint published;
    guint received;
    guint errors;
    json_t* history;
};

static void report_error(App* app, const gchar* operation, GError* error) {
    ++app->errors;
    syslog(LOG_ERR, "%s: %s", operation, error ? error->message : "Event API failed");
    g_clear_error(&error);
}

static void record(App* app, const gchar* direction, const Channel* channel,
                   gboolean active, gint duration, gint count) {
    json_t* entry = json_pack("{s:I,s:s,s:s,s:b,s:i,s:i}",
        "timestampMs", (json_int_t)(g_get_real_time() / 1000),
        "direction", direction, "topic", channel->topic, "active", active,
        "blockedSeconds", duration, "packageCount", count);
    json_array_insert_new(app->history, 0, entry);
    if (json_array_size(app->history) > HISTORY_SIZE)
        json_array_remove(app->history, HISTORY_SIZE);
    syslog(LOG_INFO, "%s %s ConveyorId=1 active=%s BlockedSeconds=%d PackageCount=%d",
           direction, channel->topic, active ? "true" : "false", duration, count);
}

static gboolean add_topics(AXEventKeyValueSet* kv, const Channel* channel, GError** error) {
    return ax_event_key_value_set_add_key_value(kv, "topic0", "tnsaxis",
               "CameraApplicationPlatform", AX_VALUE_TYPE_STRING, error) &&
           ax_event_key_value_set_add_key_value(kv, "topic1", "tnsaxis",
               "ConveyorMonitor", AX_VALUE_TYPE_STRING, error) &&
           ax_event_key_value_set_add_key_value(kv, "topic2", "tnsaxis",
               channel->topic, AX_VALUE_TYPE_STRING, error);
}

static gboolean add_payload(AXEventKeyValueSet* kv, const Channel* channel,
                            gboolean active, gint duration, gint count, GError** error) {
    gint conveyor_id = 1;
    return ax_event_key_value_set_add_key_value(kv, "ConveyorId", NULL,
               &conveyor_id, AX_VALUE_TYPE_INT, error) &&
           ax_event_key_value_set_add_key_value(kv, "PackageCount", NULL,
               &count, AX_VALUE_TYPE_INT, error) &&
           (!channel->stateful ||
             (ax_event_key_value_set_add_key_value(kv, "active", NULL,
                  &active, AX_VALUE_TYPE_BOOL, error) &&
              ax_event_key_value_set_add_key_value(kv, "BlockedSeconds", NULL,
                  &duration, AX_VALUE_TYPE_INT, error)));
}

static gboolean publish(Channel* channel, gboolean active, gint duration) {
    App* app = channel->app;
    GError* error = NULL;
    AXEventKeyValueSet* kv = ax_event_key_value_set_new();
    gboolean ok = add_payload(kv, channel, active, duration, app->conveyor.package_count, &error);
    AXEvent* event = ok ? ax_event_new2(kv, NULL) : NULL;
    ax_event_key_value_set_free(kv);
    ok = event && ax_event_handler_send_event(app->publisher, channel->declaration, event, &error);
    if (event) ax_event_free(event);
    if (!ok) {
        report_error(app, "Publish", error);
        return FALSE;
    }
    ++app->published;
    record(app, "Published", channel, active, duration, app->conveyor.package_count);
    return TRUE;
}

static const gchar* parameter_names[] = {"Enabled", "JamThresholdSeconds"};

static gboolean apply_parameter(App* app, const gchar* name, const gchar* value) {
    if (g_str_equal(name, "Enabled")) {
        if (!g_str_equal(value, "yes") && !g_str_equal(value, "no")) return FALSE;
        conveyor_set_enabled(&app->conveyor, g_str_equal(value, "yes"));
        /* Clear an active published jam even if off/on changes arrive between
         * simulation ticks. A failed clear is retried by the normal tick. */
        if (!app->conveyor.enabled && app->published_jam && app->channels[0].ready &&
            publish(&app->channels[0], FALSE, app->conveyor.blocked_seconds)) {
            app->published_jam = FALSE;
            app->state_sent = TRUE;
        }
    } else if (g_str_equal(name, "JamThresholdSeconds")) {
        guint64 seconds;
        if (!g_ascii_string_to_unsigned(value, 10, 1, 86400, &seconds, NULL)) return FALSE;
        app->conveyor.threshold_seconds = (int)seconds;
    } else return FALSE;
    syslog(LOG_INFO, "Configuration: %s=%s", name, value);
    return TRUE;
}

static void parameter_changed(const gchar* name, const gchar* value, gpointer user_data) {
    const gchar* separator = strrchr(name, '.');
    const gchar* local_name = separator ? separator + 1 : name;
    if (!apply_parameter(user_data, local_name, value))
        syslog(LOG_WARNING, "Rejected invalid setting for %s; keeping running value", local_name);
}

static gboolean setup_parameters(App* app) {
    GError* error = NULL;
    app->parameters = ax_parameter_new(APP_NAME, &error);
    if (!app->parameters) goto failure;
    for (guint i = 0; i < G_N_ELEMENTS(parameter_names); ++i) {
        if (!ax_parameter_register_callback(app->parameters, parameter_names[i],
                                            parameter_changed, app, &error)) goto failure;
        ++app->parameter_callbacks;
        gchar* value = NULL;
        if (!ax_parameter_get(app->parameters, parameter_names[i], &value, &error)) goto failure;
        gboolean valid = apply_parameter(app, parameter_names[i], value);
        g_free(value);
        if (!valid) {
            syslog(LOG_ERR, "Invalid saved setting: %s", parameter_names[i]);
            return FALSE;
        }
    }
    return TRUE;
failure:
    report_error(app, "Parameter initialization", error);
    return FALSE;
}

static void received(guint subscription, AXEvent* event, gpointer user_data) {
    (void)subscription;
    Channel* channel = user_data;
    App* app = channel->app;
    const AXEventKeyValueSet* kv = ax_event_get_key_value_set(event);
    GError* error = NULL;
    gboolean active = FALSE;
    gint conveyor_id = 0, duration = 0, count = 0;
    gboolean ok = ax_event_key_value_set_get_integer(kv, "ConveyorId", NULL, &conveyor_id, &error) &&
                  ax_event_key_value_set_get_integer(kv, "PackageCount", NULL, &count, &error);
    if (ok && channel->stateful)
        ok = ax_event_key_value_set_get_boolean(kv, "active", NULL, &active, &error) &&
             ax_event_key_value_set_get_integer(kv, "BlockedSeconds", NULL, &duration, &error);
    if (ok && conveyor_id == 1) {
        ++app->received;
        if (channel->stateful) {
            app->received_known = TRUE;
            app->received_jam = active;
        }
        record(app, "Received", channel, active, duration, count);
    } else if (!ok) {
        report_error(app, "Decode subscription", error);
    }
    ax_event_free(event);
}

static void publish_snapshot(App* app) {
    Conveyor* c = &app->conveyor;
    json_t* root = json_pack("{s:b,s:b,s:b,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:b}",
        "ready", app->channels[0].ready && app->channels[1].ready,
        "blocked", c->blocked, "jam", c->jam,
        "blockedSeconds", c->blocked_seconds, "phaseSeconds", c->phase_seconds,
        "thresholdSeconds", c->threshold_seconds, "packageCount", c->package_count,
        "published", app->published, "received", app->received, "errors", app->errors,
        "simulated", TRUE);
    json_object_set_new(root, "enabled", json_boolean(c->enabled));
    json_object_set_new(root, "monitoredSeconds", json_integer(c->monitored_seconds));
    json_object_set_new(root, "receivedJam", app->received_known
                        ? json_boolean(app->received_jam) : json_null());
    json_object_set(root, "history", app->history);
    char* text = json_dumps(root, JSON_COMPACT);
    if (text) { status_server_publish(text); free(text); }
    json_decref(root);
}

static gboolean tick(gpointer user_data) {
    App* app = user_data;
    gboolean passed = conveyor_step(&app->conveyor,
        (g_get_monotonic_time() - app->started) / G_USEC_PER_SEC);
    if (!app->state_sent || app->published_jam != app->conveyor.jam) {
        /* Physical blockage age on policy changes; completed duration on recovery. */
        gint duration = app->conveyor.blocked ? app->conveyor.blocked_seconds :
                        (app->published_jam ? 20 : 0);
        if (publish(&app->channels[0], app->conveyor.jam, duration)) {
            app->state_sent = TRUE;
            app->published_jam = app->conveyor.jam;
        }
    }
    if (passed) publish(&app->channels[1], FALSE, 0);
    publish_snapshot(app);
    return G_SOURCE_CONTINUE;
}

static void declared(guint declaration, gpointer user_data) {
    Channel* channel = user_data;
    App* app = channel->app;
    channel->declaration = declaration;
    channel->ready = TRUE;
    syslog(LOG_INFO, "Declared %s", channel->topic);
    if (app->channels[0].ready && app->channels[1].ready && !app->timer) {
        app->started = g_get_monotonic_time();
        tick(app); /* Publish an explicit initial JamActive=false. */
        app->timer = g_timeout_add_seconds(1, tick, app);
    }
}

static gboolean setup_channel(Channel* channel) {
    App* app = channel->app;
    GError* error = NULL;
    AXEventKeyValueSet* kv = ax_event_key_value_set_new();
    gint conveyor_id = 1;
    gboolean ok = add_topics(kv, channel, &error) &&
        ax_event_key_value_set_add_key_value(kv, "ConveyorId", NULL, &conveyor_id, AX_VALUE_TYPE_INT, &error) &&
        ax_event_handler_subscribe(app->subscriber, kv, &channel->subscription, received, channel, &error);
    ax_event_key_value_set_free(kv);
    if (!ok) { report_error(app, "Subscribe", error); return FALSE; }
    kv = ax_event_key_value_set_new();
    ok = add_topics(kv, channel, &error) && add_payload(kv, channel, FALSE, 0, 0, &error) &&
        ax_event_key_value_set_add_nice_names(kv, "topic1", "tnsaxis", NULL, "Conveyor Monitor", &error) &&
        ax_event_key_value_set_add_nice_names(kv, "topic2", "tnsaxis", NULL,
            channel->stateful ? "Conveyor jam active" : "Package passed", &error) &&
        ax_event_key_value_set_mark_as_source(kv, "ConveyorId", NULL, &error) &&
        ax_event_key_value_set_mark_as_data(kv, "PackageCount", NULL, &error);
    if (ok && channel->stateful)
        ok = ax_event_key_value_set_mark_as_data(kv, "active", NULL, &error) &&
             ax_event_key_value_set_mark_as_data(kv, "BlockedSeconds", NULL, &error);
    if (ok)
        ok = ax_event_handler_declare2(app->publisher, kv, !channel->stateful,
            channel->stateful ? "active" : NULL, &channel->declaration, declared, channel, &error);
    ax_event_key_value_set_free(kv);
    if (!ok) report_error(app, "Declare", error);
    return ok;
}

static gboolean stop(gpointer user_data) {
    g_main_loop_quit(user_data);
    return G_SOURCE_CONTINUE;
}

int main(void) {
    App app = {0};
    int result = EXIT_FAILURE;
    openlog(APP_NAME, LOG_PID, LOG_USER);
    signal(SIGPIPE, SIG_IGN);
    app.loop = g_main_loop_new(NULL, FALSE);
    app.history = json_array();
    app.publisher = ax_event_handler_new();
    app.subscriber = ax_event_handler_new();
    app.channels[0] = (Channel){.app = &app, .topic = "JamActive", .stateful = TRUE};
    app.channels[1] = (Channel){.app = &app, .topic = "PackagePassed", .stateful = FALSE};
    if (!app.publisher || !app.subscriber) goto cleanup;
    if (!setup_parameters(&app)) goto cleanup;
    if (!setup_channel(&app.channels[0]) || !setup_channel(&app.channels[1])) goto cleanup;
    publish_snapshot(&app);
    if (!status_server_start()) { syslog(LOG_ERR, "Cannot start status server"); goto cleanup; }
    guint sigterm = g_unix_signal_add(SIGTERM, stop, app.loop);
    guint sigint = g_unix_signal_add(SIGINT, stop, app.loop);
    g_main_loop_run(app.loop);
    g_source_remove(sigterm);
    g_source_remove(sigint);
    result = EXIT_SUCCESS;
cleanup:
    if (app.timer) g_source_remove(app.timer);
    status_server_stop();
    if (app.parameters) {
        for (guint i = 0; i < app.parameter_callbacks; ++i)
            ax_parameter_unregister_callback(app.parameters, parameter_names[i]);
        ax_parameter_free(app.parameters);
    }
    /* Free cancels pending callbacks and removes declarations/subscriptions. */
    if (app.subscriber) ax_event_handler_free(app.subscriber);
    if (app.publisher) ax_event_handler_free(app.publisher);
    json_decref(app.history);
    g_main_loop_unref(app.loop);
    closelog();
    return result;
}
