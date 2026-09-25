#include "overlay_protocol.h"

json_t* overlay_request(const char* method, json_t* params) {
    /* Discovery has no API version or params in its request schema. */
    if (g_str_equal(method, "getSupportedVersions"))
        return json_pack("{s:s}", "method", method);
    return json_pack("{s:s,s:s,s:O}", "apiVersion", "1.0", "method", method, "params", params);
}

gboolean overlay_supports_v1(json_t* response) {
    json_t* versions = json_object_get(json_object_get(response, "data"), "apiVersions");
    size_t i; json_t* item;
    json_array_foreach(versions, i, item) {
        const char* version = json_string_value(item);
        guint64 minor;
        /* Discovery lists the highest minor per major, e.g. 1.4 rather than 1.0. */
        if (version && g_str_has_prefix(version, "1.") &&
            g_ascii_string_to_unsigned(version + 2, 10, 0, G_MAXUINT, &minor, NULL))
            return TRUE;
    }
    return FALSE;
}
