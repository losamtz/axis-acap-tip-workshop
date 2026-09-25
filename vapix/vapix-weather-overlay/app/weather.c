#include "weather.h"
#include <math.h>
#include <string.h>

gboolean config_apply(Config* config, const char* name, const char* value) {
    const char* dot = strrchr(name, '.');
    name = dot ? dot + 1 : name;
    Config next = *config;
    if (g_str_equal(name, "Enabled")) {
        if (!g_str_equal(value, "yes") && !g_str_equal(value, "no")) return FALSE;
        next.enabled = g_str_equal(value, "yes");
    } else if (g_str_equal(name, "RefreshSeconds")) {
        guint64 seconds;
        if (!g_ascii_string_to_unsigned(value, 10, 300, 3600, &seconds, NULL)) return FALSE;
        next.refresh = (guint)seconds;
    } else if (g_str_equal(name, "Latitude") || g_str_equal(name, "Longitude")) {
        char* end;
        double number = g_ascii_strtod(value, &end);
        double limit = g_str_equal(name, "Latitude") ? 90 : 180;
        if (!*value || *end || !isfinite(number) || fabs(number) > limit) return FALSE;
        if (g_str_equal(name, "Latitude")) next.latitude = number;
        else next.longitude = number;
    } else return FALSE;
    next.generation++;
    *config = next;
    return TRUE;
}

gboolean weather_parse(json_t* root, Weather* weather, GError** error) {
    json_t* current = json_object_get(root, "current");
    json_t* units = json_object_get(root, "current_units");
    json_t* temperature = json_object_get(current, "temperature_2m");
    json_t* wind = json_object_get(current, "wind_speed_10m");
    json_t* code = json_object_get(current, "weather_code");
    json_t* time = json_object_get(current, "time");
    if (json_object_get(root, "error") || !json_is_number(temperature) || !json_is_number(wind) ||
        !json_is_integer(code) || !json_is_integer(time) || json_integer_value(time) <= 0 ||
        json_integer_value(code) < 0 || json_integer_value(code) > 99 ||
        !isfinite(json_number_value(temperature)) || !isfinite(json_number_value(wind)) ||
        json_number_value(temperature) < -100 || json_number_value(temperature) > 70 ||
        json_number_value(wind) < 0 || json_number_value(wind) > 500 ||
        g_strcmp0(json_string_value(json_object_get(units, "temperature_2m")), "°C") ||
        g_strcmp0(json_string_value(json_object_get(units, "wind_speed_10m")), "km/h") ||
        g_strcmp0(json_string_value(json_object_get(units, "time")), "unixtime")) {
        g_set_error_literal(error, g_quark_from_static_string("weather"), 1,
                            "Weather response has missing, invalid, or unexpected-unit fields");
        return FALSE;
    }
    *weather = (Weather){json_number_value(temperature), json_number_value(wind),
                         (int)json_integer_value(code), json_integer_value(time)};
    return TRUE;
}

const char* weather_description(int code) {
    if (code == 0) return "Clear";
    if (code <= 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code >= 95) return "Thunderstorm";
    return "Unknown conditions";
}

gboolean weather_stale(const Weather* weather, gint64 now, gint64 fetched_age, guint refresh, gboolean failed) {
    return failed || fetched_age > 2 * refresh || now - weather->time > 7200 || weather->time > now + 900;
}

gchar* weather_text(const Weather* weather, const Config* config, gboolean valid, gboolean stale) {
    gchar lat[G_ASCII_DTOSTR_BUF_SIZE], lon[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(lat, sizeof(lat), "%.4f", config->latitude);
    g_ascii_formatd(lon, sizeof(lon), "%.4f", config->longitude);
    if (!valid) return g_strdup_printf(OVERLAY_PREFIX "%s,%s | Weather unavailable | Open-Meteo.com", lat, lon);
    GDateTime* time = g_date_time_new_from_unix_utc(weather->time);
    gchar* stamp = time ? g_date_time_format(time, "%m-%d %H:%MZ") : g_strdup("unknown time");
    gchar temp[G_ASCII_DTOSTR_BUF_SIZE], wind[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(temp, sizeof(temp), "%.1f", weather->temperature);
    g_ascii_formatd(wind, sizeof(wind), "%.0f", weather->wind);
    gchar* text = g_strdup_printf(OVERLAY_PREFIX "%s,%s | %sC Wind %skm/h | %s | %s%s | Open-Meteo.com",
        lat, lon, temp, wind, weather_description(weather->code), stale ? "STALE " : "", stamp);
    g_free(stamp);
    if (time) g_date_time_unref(time);
    return text;
}

json_t* owned_overlays(json_t* response) {
    json_t* matches = json_array();
    json_t* overlays = json_object_get(json_object_get(response, "data"), "textOverlays");
    size_t i; json_t* overlay;
    json_array_foreach(overlays, i, overlay) {
        const char* text = json_string_value(json_object_get(overlay, "text"));
        json_t* id = json_object_get(overlay, "identity");
        if (text && g_str_has_prefix(text, OVERLAY_PREFIX) && json_is_integer(id) &&
            json_integer_value(id) >= 0 && json_integer_value(json_object_get(overlay, "camera")) == 1)
            json_array_append(matches, id);
    }
    return matches;
}
