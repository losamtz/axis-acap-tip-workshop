#include "policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    char* values[4];
    int writes, fail_write;
    gboolean fail_read;
} Fake;
static int index_for(const char* name) {
    const char* names[] = {"SiteName", "Enabled", "OccupancyLimitSeconds", "AlertCooldownSeconds"};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(name, names[i])) {
            return i;
        }
    }
    assert(0);
    return 0;
}
static gboolean read_value(void* ctx, const char* name, char** value) {
    Fake* f = ctx;
    if (f->fail_read) {
        return FALSE;
    }
    *value = g_strdup(f->values[index_for(name)]);
    return TRUE;
}
static gboolean write_value(void* ctx, const char* name, const char* value) {
    Fake* f = ctx;
    if (++f->writes == f->fail_write) {
        return FALSE;
    }
    int i = index_for(name);
    g_free(f->values[i]);
    f->values[i] = g_strdup(value);
    return TRUE;
}
static json_t* settings(void) {
    return json_pack("{s:s,s:b,s:i,s:i}", "siteName", "Warehouse B", "enabled", TRUE,
                     "occupancyLimitSeconds", 30, "alertCooldownSeconds", 20);
}
static json_t* evaluate(Store* store, int occupied, json_t* last) {
    int code = 0;
    json_t* input = json_pack("{s:i}", "occupiedSeconds", occupied);
    json_object_set_new(input, "secondsSinceLastAlert", last);
    json_t* r = policy_test(store, input, &code);
    assert(code == 200);
    json_decref(input);
    return r;
}
int main(void) {
    Fake f = {.values = {g_strdup("Original"), g_strdup("yes"), g_strdup("10"), g_strdup("5")}};
    Store store = {&f, read_value, write_value};
    int code;
    json_t* input = settings();
    json_object_set_new(input, "extra", json_integer(1));
    json_t* r = policy_save(&store, input, &code);
    assert(code == 422 && f.writes == 0);
    json_decref(r);
    json_object_del(input, "extra");
    json_object_set_new(input, "occupancyLimitSeconds", json_string("30"));
    r = policy_save(&store, input, &code);
    assert(code == 422 && f.writes == 0);
    json_decref(r);
    json_object_set_new(input, "occupancyLimitSeconds", json_integer(0));
    r = policy_save(&store, input, &code);
    assert(code == 422 && f.writes == 0);
    json_decref(r);
    json_decref(input);
    input = settings();
    json_object_set_new(input, "siteName", json_stringn("A\0B", 3));
    r = policy_save(&store, input, &code);
    assert(code == 422);
    json_decref(r);
    json_decref(input);
    input = settings();
    r = policy_save(&store, input, &code);
    assert(code == 200 && f.writes == 4);
    assert(!strcmp(f.values[0], "Warehouse B"));
    json_decref(r);
    r = evaluate(&store, 29, json_null());
    assert(!strcmp(json_string_value(json_object_get(r, "state")), "Within limit"));
    json_decref(r);
    r = evaluate(&store, 30, json_null());
    assert(json_is_true(json_object_get(r, "alertEligible")));
    json_decref(r);
    r = evaluate(&store, 30, json_integer(19));
    assert(json_is_false(json_object_get(r, "alertEligible")));
    assert(!strcmp(json_string_value(json_object_get(r, "state")), "Overdue"));
    json_decref(r);
    r = evaluate(&store, 30, json_integer(20));
    assert(json_is_true(json_object_get(r, "alertEligible")));
    json_decref(r);
    assert(f.writes == 4);
    g_free(f.values[1]);
    f.values[1] = g_strdup("no");
    r = evaluate(&store, 100, json_null());
    assert(!strcmp(json_string_value(json_object_get(r, "state")), "Disabled"));
    json_decref(r);
    f.writes = 0;
    f.fail_write = 2;
    json_object_set_new(input, "siteName", json_string("Partial"));
    r = policy_save(&store, input, &code);
    assert(code == 500 && json_is_false(json_object_get(r, "ok")) && f.writes == 2);
    assert(!strcmp(json_string_value(json_object_get(json_object_get(r, "settings"), "siteName")),
                   "Partial"));
    assert(json_is_false(json_object_get(json_object_get(r, "settings"), "enabled")));
    json_decref(r);
    f.fail_write = 0;
    f.fail_read = TRUE;
    r = policy_save(&store, input, &code);
    assert(code == 500 && json_is_null(json_object_get(r, "settings")));
    json_decref(r);
    json_t* test = json_pack("{s:i,s:n}", "occupiedSeconds", 10, "secondsSinceLastAlert");
    r = policy_test(&store, test, &code);
    assert(code == 500);
    json_decref(r);
    f.fail_read = FALSE;
    json_object_set_new(test, "occupiedSeconds", json_integer(-1));
    r = policy_test(&store, test, &code);
    assert(code == 422);
    json_decref(r);
    json_decref(test);
    json_decref(input);
    for (guint i = 0; i < 4; i++) {
        g_free(f.values[i]);
    }
    puts("PASS: complete validation before writes, readback, partial failure, cooldown boundaries, "
         "disabled policy and side-effect-free tests");
}
