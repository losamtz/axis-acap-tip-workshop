#ifndef POLICY_H
#define POLICY_H

#include <glib.h>
#include <jansson.h>

/* Validated application settings; durations are in seconds. */
typedef struct {
    char site[65];
    gboolean enabled;
    guint limit;
    guint cooldown;
} Policy;

/* Reads return an allocated string that the caller releases with g_free().
 * Writes must persist the supplied value before reporting success. */
typedef gboolean (*ReadParameter)(void* context, const char* name, char** value);
typedef gboolean (*WriteParameter)(void* context, const char* name, const char* value);

/* main.c supplies AXParameter operations; unit tests supply an in-memory store. */
typedef struct {
    void* context;
    ReadParameter read;
    WriteParameter write;
} Store;

/* Collect field errors without modifying out unless every field is valid. */
gboolean policy_validate(json_t* input, Policy* out, json_t* errors);

/* Returned JSON objects are owned by the caller: release with json_decref(). */
json_t* policy_json(const Policy* policy);
gboolean policy_read(Store* store, Policy* out);
json_t* policy_save(Store* store, json_t* input, int* status);
json_t* policy_test(Store* store, json_t* input, int* status);

#endif
