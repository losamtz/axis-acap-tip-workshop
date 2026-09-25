#include "weather.h"
#include "overlay_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    json_t* params = json_pack("{s:i}", "identity", 0);
    json_t* request = overlay_request("getSupportedVersions", params);
    assert(json_object_size(request) == 1);
    assert(!json_object_get(request, "apiVersion") && !json_object_get(request, "params"));
    assert(!strcmp(json_string_value(json_object_get(request, "method")), "getSupportedVersions"));
    json_decref(request);
    request = overlay_request("setText", params);
    assert(!strcmp(json_string_value(json_object_get(request, "apiVersion")), "1.0"));
    assert(json_object_get(request, "params") == params);
    json_decref(request); json_decref(params);
    json_t* discovery = json_loads("{\"data\":{\"apiVersions\":[\"1.4\",\"2.0\"]}}", 0, NULL);
    assert(overlay_supports_v1(discovery)); json_decref(discovery);
    discovery = json_loads("{\"data\":{\"apiVersions\":[\"2.0\",\"11.0\",\"1.bad\"]}}", 0, NULL);
    assert(!overlay_supports_v1(discovery)); json_decref(discovery);
    assert(!overlay_supports_v1(NULL));
    Config config = {0};
    assert(config_apply(&config, "root.Vapix_weather_overlay.Enabled", "yes"));
    assert(config_apply(&config, "root.vapix_weather_overlay.Latitude", "55.7047"));
    assert(config_apply(&config, "Longitude", "13.1910"));
    assert(config_apply(&config, "RefreshSeconds", "600"));
    assert(!config_apply(&config, "Latitude", "nan"));
    assert(!config_apply(&config, "Latitude", "91"));
    assert(!config_apply(&config, "Longitude", "180.1"));
    assert(!config_apply(&config, "Longitude", "13&evil=1"));
    assert(!config_apply(&config, "RefreshSeconds", "299"));
    assert(!config_apply(&config, "RefreshSeconds", "3601"));
    assert(!config_apply(&config, "Enabled", "maybe"));
    assert(config.generation == 4);
    json_t* root = json_loads("{\"current\":{\"temperature_2m\":8.5,\"wind_speed_10m\":24,\"weather_code\":61,\"time\":1700000000},"
       "\"current_units\":{\"temperature_2m\":\"°C\",\"wind_speed_10m\":\"km/h\",\"time\":\"unixtime\"}}", 0, NULL);
    Weather weather;
    GError* error = NULL;
    assert(weather_parse(root, &weather, &error));
    assert(weather.temperature == 8.5 && weather.wind == 24 && weather.code == 61);
    assert(!strcmp(weather_description(weather.code), "Rain"));
    assert(!weather_stale(&weather, 1700000100, 100, 600, FALSE));
    assert(weather_stale(&weather, 1700000100, 100, 600, TRUE));
    assert(weather_stale(&weather, 1700000100, 1201, 600, FALSE));
    assert(weather_stale(&weather, 1700007201, 1, 600, FALSE));
    assert(weather_stale(&weather, 1699990000, 1, 600, FALSE));
    gchar* text = weather_text(&weather, &config, TRUE, TRUE);
    assert(g_str_has_prefix(text, OVERLAY_PREFIX));
    assert(strstr(text, "STALE") && strstr(text, "Open-Meteo.com") && strlen(text) < 512);
    g_free(text);
    text = weather_text(&weather, &config, FALSE, FALSE);
    assert(strstr(text, "Weather unavailable") && !strstr(text, "8.5")); g_free(text);
    json_object_set_new(json_object_get(root, "current"), "temperature_2m", json_null());
    assert(!weather_parse(root, &weather, &error)); g_clear_error(&error);
    json_object_set_new(json_object_get(root, "current"), "temperature_2m", json_real(8.5));
    json_object_set_new(json_object_get(root, "current_units"), "wind_speed_10m", json_string("mph"));
    assert(!weather_parse(root, &weather, &error)); g_clear_error(&error); json_decref(root);
    root = json_loads("{\"data\":{\"textOverlays\":["
        "{\"camera\":1,\"identity\":0,\"text\":\"WX-WORKSHOP | old weather\"},"
        "{\"camera\":1,\"identity\":1,\"text\":\"Existing user overlay\"},"
        "{\"camera\":2,\"identity\":2,\"text\":\"WX-WORKSHOP | other channel\"},"
        "{\"camera\":1,\"identity\":\"invalid\",\"text\":\"WX-WORKSHOP | bad ID\"}]}}",0,NULL);
    json_t* owned = owned_overlays(root);
    assert(json_array_size(owned) == 1 && json_integer_value(json_array_get(owned, 0)) == 0);
    json_decref(owned); json_decref(root);
    puts("PASS: settings validation, capitalization, JSON validation, units, freshness, text, overlay ownership");
    return 0;
}
