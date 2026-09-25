#ifndef OVERLAY_PROTOCOL_H
#define OVERLAY_PROTOCOL_H
#include <glib.h>
#include <jansson.h>
json_t* overlay_request(const char* method, json_t* params);
gboolean overlay_supports_v1(json_t* response);
#endif
