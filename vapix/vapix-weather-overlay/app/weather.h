#ifndef WEATHER_H
#define WEATHER_H
#include <glib.h>
#include <jansson.h>
#define OVERLAY_PREFIX "WX-WORKSHOP | "
typedef struct { gboolean enabled; double latitude, longitude; guint refresh; guint generation; } Config;
typedef struct { double temperature, wind; int code; gint64 time; } Weather;
gboolean config_apply(Config* config, const char* name, const char* value);
gboolean weather_parse(json_t* root, Weather* weather, GError** error);
const char* weather_description(int code);
gboolean weather_stale(const Weather* weather, gint64 now, gint64 fetched_age, guint refresh, gboolean failed);
gchar* weather_text(const Weather* weather, const Config* config, gboolean valid, gboolean stale);
/* Only overlays carrying our reserved prefix on camera 1 are candidates. */
json_t* owned_overlays(json_t* response);
#endif
