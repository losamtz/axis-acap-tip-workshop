#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <signal.h>
#include <jansson.h>
#include "status_server.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define APP_NAME "parameter_loading_bay"
#define OCCUPIED_SECONDS 90
#define FREE_SECONDS 15

#define ALERT_HISTORY_SIZE 20

typedef struct {
    gint64 timestamp_ms;
    gint64 elapsed;
    guint limit;
    guint cooldown;
} Alert;

typedef struct {
    Alert alerts[ALERT_HISTORY_SIZE];
    guint alert_count;
    guint next_alert;
    guint total_alerts;
    gboolean enabled;
    guint max_occupancy_seconds;
    guint alert_cooldown_seconds;
    gint64 simulation_started;
    gint64 occupied_since;
    gint64 last_alert;
    gboolean occupied;
    gboolean tracking;
    gboolean alerted;
} Monitor;

static const gchar* parameter_names[] = {
    "Enabled", "MaxOccupancySeconds", "AlertCooldownSeconds"
};

/* Validate in C as well as in the manifest. Never replace a valid running
 * setting with an invalid value received from parameter storage. */
static gboolean apply_parameter(Monitor* monitor, const gchar* name, const gchar* value) {
    if (g_str_equal(name, "Enabled")) {
        if (!g_str_equal(value, "yes") && !g_str_equal(value, "no"))
            return FALSE;
        gboolean enabled = g_str_equal(value, "yes");
        if (monitor->enabled != enabled) {
            monitor->tracking = FALSE;
            monitor->alerted = FALSE;
        }
        monitor->enabled = enabled;
    } else {
        guint64 seconds = 0;
        if (!g_ascii_string_to_unsigned(value, 10, 1, 86400, &seconds, NULL))
            return FALSE;
        if (g_str_equal(name, "MaxOccupancySeconds"))
            monitor->max_occupancy_seconds = (guint)seconds;
        else if (g_str_equal(name, "AlertCooldownSeconds"))
            monitor->alert_cooldown_seconds = (guint)seconds;
        else
            return FALSE;
    }
    syslog(LOG_INFO, "Configuration: %s=%s", name, value);
    return TRUE;
}

static void parameter_changed(const gchar* name, const gchar* value, gpointer user_data) {
    /* Callbacks are registered for our three app parameters. The camera may
     * capitalize the app group (root.Parameter_loading_bay), so dispatch by
     * the final component instead of assuming it matches APP_NAME exactly. */
    const gchar* separator = strrchr(name, '.');
    const gchar* local_name = separator ? separator + 1 : name;
    if (!apply_parameter(user_data, local_name, value))
        syslog(LOG_WARNING, "Rejected invalid setting for %s; keeping previous running value",
               local_name);
}

/* Occupancy is an input to this policy. A real integration replaces the
 * simulator with an analytics signal, delivered on this same main context. */
static void evaluate_occupancy(Monitor* monitor, gboolean occupied, gint64 now) {
    if (!monitor->enabled || !occupied) {
        monitor->tracking = FALSE;
        monitor->alerted = FALSE;
        return;
    }
    if (!monitor->tracking) {
        monitor->occupied_since = now;
        monitor->tracking = TRUE;
        monitor->alerted = FALSE;
        syslog(LOG_INFO, "Monitoring: started timing occupied bay");
    }

    gint64 elapsed = (now - monitor->occupied_since) / G_USEC_PER_SEC;
    gint64 since_alert = (now - monitor->last_alert) / G_USEC_PER_SEC;
    if (elapsed >= monitor->max_occupancy_seconds &&
        (!monitor->alerted || since_alert >= monitor->alert_cooldown_seconds)) {
        syslog(LOG_WARNING,
               "SIMULATED ALERT: bay occupied for %" G_GINT64_FORMAT
               " seconds (limit=%u, cooldown=%u)",
               elapsed, monitor->max_occupancy_seconds, monitor->alert_cooldown_seconds);
        monitor->alerts[monitor->next_alert] = (Alert){
            .timestamp_ms = g_get_real_time() / 1000,
            .elapsed = elapsed,
            .limit = monitor->max_occupancy_seconds,
            .cooldown = monitor->alert_cooldown_seconds
        };
        monitor->next_alert = (monitor->next_alert + 1) % ALERT_HISTORY_SIZE;
        monitor->alert_count = MIN(monitor->alert_count + 1, ALERT_HISTORY_SIZE);
        ++monitor->total_alerts;
        monitor->last_alert = now;
        monitor->alerted = TRUE;
    }
}

/* Publish on the main context; the HTTP worker reads only this serialized copy. */
static void publish_status(const Monitor* monitor, gint64 now) {
    gint64 elapsed = monitor->tracking ? (now - monitor->occupied_since) / G_USEC_PER_SEC : 0;
    gint64 threshold_wait = MAX((gint64)monitor->max_occupancy_seconds - elapsed, 0);
    gint64 cooldown_wait = monitor->alerted
        ? MAX((gint64)monitor->alert_cooldown_seconds -
              (now - monitor->last_alert) / G_USEC_PER_SEC, 0) : 0;
    gint64 phase = ((now - monitor->simulation_started) / G_USEC_PER_SEC) %
                   (OCCUPIED_SECONDS + FREE_SECONDS);
    json_t* root = json_object();
    json_object_set_new(root, "simulated", json_true());
    json_object_set_new(root, "enabled", json_boolean(monitor->enabled));
    json_object_set_new(root, "occupied", json_boolean(monitor->occupied));
    json_object_set_new(root, "tracking", json_boolean(monitor->tracking));
    json_object_set_new(root, "maxOccupancySeconds", json_integer(monitor->max_occupancy_seconds));
    json_object_set_new(root, "alertCooldownSeconds", json_integer(monitor->alert_cooldown_seconds));
    json_object_set_new(root, "elapsedSeconds", json_integer(elapsed));
    json_object_set_new(root, "nextAlertSeconds", monitor->tracking
                        ? json_integer(MAX(threshold_wait, cooldown_wait)) : json_null());
    json_object_set_new(root, "phaseRemainingSeconds", json_integer(monitor->occupied
                        ? OCCUPIED_SECONDS - phase : OCCUPIED_SECONDS + FREE_SECONDS - phase));
    json_object_set_new(root, "totalAlerts", json_integer(monitor->total_alerts));
    json_object_set_new(root, "timestampMs", json_integer(g_get_real_time() / 1000));
    json_t* alerts = json_array();
    for (guint i = 0; i < monitor->alert_count; ++i) {
        guint index = (monitor->next_alert + ALERT_HISTORY_SIZE - 1 - i) % ALERT_HISTORY_SIZE;
        const Alert* alert = &monitor->alerts[index];
        json_t* entry = json_object();
        json_object_set_new(entry, "timestampMs", json_integer(alert->timestamp_ms));
        json_object_set_new(entry, "elapsedSeconds", json_integer(alert->elapsed));
        json_object_set_new(entry, "limitSeconds", json_integer(alert->limit));
        json_object_set_new(entry, "cooldownSeconds", json_integer(alert->cooldown));
        json_array_append_new(alerts, entry);
    }
    json_object_set_new(root, "alerts", alerts);
    char* serialized = json_dumps(root, JSON_COMPACT);
    if (serialized) {
        status_server_publish(serialized);
        free(serialized);
    }
    json_decref(root);
}

static gboolean simulation_tick(gpointer user_data) {
    Monitor* monitor = user_data;
    gint64 now = g_get_monotonic_time();
    gint64 elapsed = (now - monitor->simulation_started) / G_USEC_PER_SEC;
    gboolean occupied = elapsed % (OCCUPIED_SECONDS + FREE_SECONDS) < OCCUPIED_SECONDS;
    if (occupied != monitor->occupied) {
        monitor->occupied = occupied;
        syslog(LOG_INFO, "SIMULATOR: bay is %s", occupied ? "OCCUPIED" : "FREE");
    }
    evaluate_occupancy(monitor, occupied, now);
    publish_status(monitor, now);
    return G_SOURCE_CONTINUE;
}

static gboolean stop_application(gpointer user_data) {
    g_main_loop_quit(user_data);
    /* Keep the source registered until the common cleanup path removes it. */
    return G_SOURCE_CONTINUE;
}

int main(void) {
    Monitor monitor = {0};
    GError* error = NULL;
    GMainLoop* loop = NULL;
    guint registered = 0;
    int result = EXIT_FAILURE;

    openlog(APP_NAME, LOG_PID, LOG_USER);
    signal(SIGPIPE, SIG_IGN); /* A disconnected HTTP client must not stop monitoring. */
    AXParameter* parameters = ax_parameter_new(APP_NAME, &error);
    if (!parameters)
        goto cleanup;

    for (guint i = 0; i < G_N_ELEMENTS(parameter_names); ++i) {
        if (!ax_parameter_register_callback(parameters, parameter_names[i],
                                             parameter_changed, &monitor, &error))
            goto cleanup;
        ++registered;
        gchar* value = NULL;
        if (!ax_parameter_get(parameters, parameter_names[i], &value, &error))
            goto cleanup;
        gboolean valid = apply_parameter(&monitor, parameter_names[i], value);
        g_free(value);
        if (!valid) {
            syslog(LOG_ERR, "Invalid saved value for %s; correct it before starting",
                   parameter_names[i]);
            goto cleanup;
        }
    }

    loop = g_main_loop_new(NULL, FALSE);
    monitor.simulation_started = g_get_monotonic_time();
    syslog(LOG_INFO, "Simulation started: %d seconds occupied, %d seconds free; alerts appear in logs and the local page",
           OCCUPIED_SECONDS, FREE_SECONDS);
    simulation_tick(&monitor);
    if (!status_server_start()) {
        syslog(LOG_ERR, "Cannot start status endpoint");
        goto cleanup;
    }
    guint timer = g_timeout_add_seconds(1, simulation_tick, &monitor);
    guint sigterm = g_unix_signal_add(SIGTERM, stop_application, loop);
    guint sigint = g_unix_signal_add(SIGINT, stop_application, loop);
    g_main_loop_run(loop);
    g_source_remove(timer);
    g_source_remove(sigterm);
    g_source_remove(sigint);
    result = EXIT_SUCCESS;

cleanup:
    status_server_stop();
    if (error) {
        syslog(LOG_ERR, "Parameter API: %s", error->message);
        g_clear_error(&error);
    }
    if (parameters) {
        for (guint i = 0; i < registered; ++i)
            ax_parameter_unregister_callback(parameters, parameter_names[i]);
        ax_parameter_free(parameters);
    }
    if (loop)
        g_main_loop_unref(loop);
    syslog(LOG_INFO, "Application stopped (%s)", result == EXIT_SUCCESS ? "normal" : "error");
    closelog();
    return result;
}
