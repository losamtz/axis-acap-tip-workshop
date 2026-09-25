#ifndef HTTP_H
#define HTTP_H
#include <gio/gio.h>
#include <jansson.h>
char* camera_credentials(GError** error);
json_t* http_json(const char* url, const char* credentials, json_t* request, GError** error);
json_t* overlay_call(const char* credentials, const char* method, json_t* params, GError** error);
#endif
