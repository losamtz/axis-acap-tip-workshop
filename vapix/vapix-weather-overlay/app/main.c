#include <axsdk/axparameter.h>
#include <curl/curl.h>
#include <glib-unix.h>
#include <signal.h>
#include <syslog.h>
#include "weather.h"
#include "http.h"
#include "overlay_protocol.h"
#include "status_server.h"

#define APP_NAME "vapix_weather_overlay"
static const char* names[] = {"Enabled", "Latitude", "Longitude", "RefreshSeconds", "LocationName"};
typedef struct {
    GMutex lock;
    GCond changed;
    Config config;
    gint64 changed_at;
    gboolean stop;
} Shared;

typedef struct {
    Config config;
    Weather weather;
    gboolean valid, failed, synced, supported;
    gint64 fetched, fetched_at, next_fetch, last_attempt, next_overlay;
    gchar* credentials;
    gchar* applied;
    gchar* weather_error;
    gchar* overlay_error;
} Worker;

static void set_error(gchar** destination, const char* operation, GError** error) {
    g_free(*destination);
    *destination = g_strdup(*error ? (*error)->message : "Unknown error");
    syslog(LOG_WARNING, "%s: %s", operation, *destination);
    g_clear_error(error);
}

static gboolean sync_overlay(Worker* worker, const char* text, gboolean enabled, GError** error) {
    json_t* params = json_object();
    json_t* response = overlay_call(worker->credentials, "list", params, error);
    if (!response) { json_decref(params); return FALSE; }
    json_t* entries = json_object_get(json_object_get(response, "data"), "textOverlays");
    if (!json_is_array(entries)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "VAPIX list missing textOverlays");
        json_decref(response); json_decref(params); return FALSE;
    }
    json_t* owned = owned_overlays(response);
    json_decref(response);
    /* Re-discover IDs each time: camera reboot can renumber overlay identities.
     * Our reserved text prefix identifies only this workshop app's overlays. */
    size_t count = json_array_size(owned);
    gboolean ok = TRUE;
    for (size_t i = enabled ? 1 : 0; ok && i < count; ++i) {
        json_object_set(params, "identity", json_array_get(owned, i));
        response = overlay_call(worker->credentials, "remove", params, error);
        ok = response != NULL; json_decref(response);
    }
    if (ok && enabled) {
        json_object_clear(params);
        json_object_set_new(params, "text", json_string(text));
        if (count) json_object_set(params, "identity", json_array_get(owned, 0));
        else {
            json_object_set_new(params, "camera", json_integer(1));
            json_object_set_new(params, "position", json_string("topLeft"));
            json_object_set_new(params, "textColor", json_string("white"));
            json_object_set_new(params, "textBGColor", json_string("black"));
            json_object_set_new(params, "fontSize", json_integer(24));
        }
        response = overlay_call(worker->credentials, count ? "setText" : "addText", params, error);
        ok = response != NULL;
        if (ok && !count && !json_is_integer(json_object_get(json_object_get(response, "data"), "identity"))) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "VAPIX addText missing identity");
            ok = FALSE;
        }
        json_decref(response);
    }
    json_decref(params); json_decref(owned);
    return ok;
}

static void snapshot(Worker* w, const char* activity) {
    gint64 now = g_get_monotonic_time() / G_USEC_PER_SEC;
    gboolean stale = w->valid && weather_stale(&w->weather, g_get_real_time() / G_USEC_PER_SEC,
                                               now - w->fetched, w->config.refresh, w->failed);
    json_t* root = json_pack("{s:b,s:f,s:f,s:i,s:b,s:b,s:b,s:s,s:s,s:s,s:s,s:I,s:I}",
        "enabled", w->config.enabled, "latitude", w->config.latitude, "longitude", w->config.longitude,
        "refreshSeconds", w->config.refresh, "valid", w->valid, "stale", stale, "overlaySynced", w->synced,
        "activity", activity, "overlayText", w->applied ? w->applied : "",
        "weatherError", w->weather_error ? w->weather_error : "",
        "overlayError", w->overlay_error ? w->overlay_error : "",
        "fetchedAt", (json_int_t)w->fetched_at,
        "nextFetchSeconds", (json_int_t)MAX(w->next_fetch - now, 0));
    json_object_set_new(root, "locationName", json_string(w->config.location_name));
    if (w->valid) {
        json_object_set_new(root, "temperature", json_real(w->weather.temperature));
        json_object_set_new(root, "wind", json_real(w->weather.wind));
        json_object_set_new(root, "condition", json_string(weather_description(w->weather.code)));
        json_object_set_new(root, "weatherTime", json_integer(w->weather.time));
    }
    char* text = json_dumps(root, JSON_COMPACT);
    if (text) { status_server_publish(text); free(text); }
    json_decref(root);
}

static gboolean changed_during_request(Shared* shared, guint generation) {
    g_mutex_lock(&shared->lock);
    gboolean changed = shared->stop || shared->config.generation != generation;
    g_mutex_unlock(&shared->lock);
    return changed;
}

static gpointer run_worker(gpointer data) {
    Shared* shared = data;
    Worker w = {0};
    guint generation = 0;
    gint64 credential_retry = 0;
    while (TRUE) {
        g_mutex_lock(&shared->lock);
        if (shared->stop) { g_mutex_unlock(&shared->lock); break; }
        /* Coalesce settings callbacks from a single form submission. */
        if (g_get_monotonic_time() < shared->changed_at + G_USEC_PER_SEC) {
            g_cond_wait_until(&shared->changed, &shared->lock, shared->changed_at + G_USEC_PER_SEC);
            g_mutex_unlock(&shared->lock); continue;
        }
        Config config = shared->config;
        g_mutex_unlock(&shared->lock);
        gint64 now = g_get_monotonic_time() / G_USEC_PER_SEC;
        if (config.generation != generation) {
            gboolean location_changed = !generation || config.latitude != w.config.latitude || config.longitude != w.config.longitude;
            if (location_changed) { w.valid = FALSE; w.fetched_at = 0; g_clear_pointer(&w.weather_error, g_free); }
            w.next_fetch = MAX(now, w.last_attempt + 60);
            w.next_overlay = 0; w.synced = FALSE;
            w.config = config; generation = config.generation;
        }
        GError* error = NULL;
        if (!w.credentials && now >= credential_retry) {
            snapshot(&w, "Acquiring local camera credentials");
            w.credentials = camera_credentials(&error);
            if (!w.credentials) set_error(&w.overlay_error, "Credentials", &error);
            credential_retry = now + 60;
        }
        if (w.credentials && !w.supported && now >= w.next_overlay) {
            json_t* params = json_object();
            json_t* response = overlay_call(w.credentials, "getSupportedVersions", params, &error);
            json_decref(params);
            w.supported = overlay_supports_v1(response);
            if (!w.supported) {
                if (!error) g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                                "Dynamic Overlay API 1.0 is not advertised by this camera");
                set_error(&w.overlay_error, "Overlay support", &error);
                w.next_overlay = now + 60;
            }
            json_decref(response);
        }
        if (changed_during_request(shared, generation)) continue;
        if (config.enabled && now >= w.next_fetch) {
            gchar lat[G_ASCII_DTOSTR_BUF_SIZE], lon[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_dtostr(lat, sizeof(lat), config.latitude); g_ascii_dtostr(lon, sizeof(lon), config.longitude);
            gchar* url = g_strdup_printf("https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                "&current=temperature_2m,wind_speed_10m,weather_code&temperature_unit=celsius"
                "&wind_speed_unit=kmh&timeformat=unixtime&timezone=GMT", lat, lon);
            snapshot(&w, "Fetching weather over HTTPS");
            w.last_attempt = now;
            json_t* response = http_json(url, NULL, NULL, &error);
            Weather weather = {0};
            gboolean ok = response && weather_parse(response, &weather, &error);
            json_decref(response); g_free(url);
            if (changed_during_request(shared, generation)) { g_clear_error(&error); continue; }
            w.next_fetch = (g_get_monotonic_time() / G_USEC_PER_SEC) + config.refresh;
            w.failed = !ok;
            if (ok) {
                w.weather = weather; w.valid = TRUE;
                w.fetched = g_get_monotonic_time() / G_USEC_PER_SEC;
                w.fetched_at = g_get_real_time() / G_USEC_PER_SEC;
                g_clear_pointer(&w.weather_error, g_free);
                syslog(LOG_INFO, "Weather fetched for %.4f,%.4f", config.latitude, config.longitude);
            } else set_error(&w.weather_error, "Weather fetch", &error);
        }
        now = g_get_monotonic_time() / G_USEC_PER_SEC;
        gboolean stale = w.valid && weather_stale(&w.weather, g_get_real_time() / G_USEC_PER_SEC,
                                                   now - w.fetched, config.refresh, w.failed);
        gchar* desired = config.enabled ? weather_text(&w.weather, &config, w.valid, stale) : g_strdup("");
        if (g_strcmp0(desired, w.applied)) w.synced = FALSE;
        if (w.supported && now >= w.next_overlay && (!w.synced || g_strcmp0(desired, w.applied))) {
            w.synced = FALSE;
            snapshot(&w, config.enabled ? "Updating camera overlay" : "Removing weather overlay");
            if (sync_overlay(&w, desired, config.enabled, &error)) {
                g_free(w.applied); w.applied = g_strdup(desired); w.synced = TRUE;
                g_clear_pointer(&w.overlay_error, g_free);
                syslog(LOG_INFO, "Weather overlay %s", config.enabled ? "updated" : "removed");
            } else set_error(&w.overlay_error, "Overlay", &error);
            w.next_overlay = now + 60;
        }
        g_free(desired);
        snapshot(&w, config.enabled ? "Monitoring weather" : "Disabled");
        g_mutex_lock(&shared->lock);
        if (!shared->stop && shared->config.generation == generation)
            g_cond_wait_until(&shared->changed, &shared->lock, g_get_monotonic_time() + G_USEC_PER_SEC);
        g_mutex_unlock(&shared->lock);
    }
    if (w.credentials && w.supported) {
        GError* error = NULL;
        if (!sync_overlay(&w, "", FALSE, &error)) set_error(&w.overlay_error, "Stop cleanup", &error);
    }
    g_free(w.credentials); g_free(w.applied); g_free(w.weather_error); g_free(w.overlay_error);
    return NULL;
}

static void parameter_changed(const gchar* name, const gchar* value, gpointer data) {
    Shared* shared = data;
    g_mutex_lock(&shared->lock);
    gboolean valid = config_apply(&shared->config, name, value);
    if (valid) { shared->changed_at = g_get_monotonic_time(); g_cond_signal(&shared->changed); }
    g_mutex_unlock(&shared->lock);
    syslog(valid ? LOG_INFO : LOG_WARNING, "%s %s=%s", valid ? "Configuration" : "Rejected setting", name, value);
}
static gboolean stop_application(gpointer loop) { g_main_loop_quit(loop); return G_SOURCE_CONTINUE; }

int main(void) {
    Shared shared = {0};
    GError* error = NULL;
    guint registered = 0;
    int result = EXIT_FAILURE;
    GThread* worker = NULL;
    openlog(APP_NAME, LOG_PID, LOG_USER); signal(SIGPIPE, SIG_IGN);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return EXIT_FAILURE;
    AXParameter* parameters = ax_parameter_new(APP_NAME, &error);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    if (!parameters) goto cleanup;
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
        if (!ax_parameter_register_callback(parameters, names[i], parameter_changed, &shared, &error)) goto cleanup;
        ++registered;
        gchar* value = NULL;
        if (!ax_parameter_get(parameters, names[i], &value, &error)) goto cleanup;
        gboolean valid = config_apply(&shared.config, names[i], value);
        g_free(value);
        if (!valid) { syslog(LOG_ERR, "Invalid saved parameter %s", names[i]); goto cleanup; }
    }
    if (!status_server_start()) { syslog(LOG_ERR, "Cannot start status endpoint"); goto cleanup; }
    worker = g_thread_new("weather", run_worker, &shared);
    guint term = g_unix_signal_add(SIGTERM, stop_application, loop);
    guint interrupt = g_unix_signal_add(SIGINT, stop_application, loop);
    g_main_loop_run(loop);
    g_source_remove(term); g_source_remove(interrupt);
    result = EXIT_SUCCESS;
cleanup:
    if (error) { syslog(LOG_ERR, "%s", error->message); g_clear_error(&error); }
    if (parameters) {
        for (guint i = 0; i < registered; ++i) ax_parameter_unregister_callback(parameters, names[i]);
        ax_parameter_free(parameters);
    }
    if (worker) {
        g_mutex_lock(&shared.lock); shared.stop = TRUE; g_cond_signal(&shared.changed); g_mutex_unlock(&shared.lock);
        g_thread_join(worker);
    }
    status_server_stop(); g_main_loop_unref(loop); curl_global_cleanup();
    g_mutex_clear(&shared.lock); g_cond_clear(&shared.changed); closelog();
    return result;
}
