#include "policy.h"
#include <string.h>

static void add_field_error(json_t* errors, const char* field, const char* message) {
    json_object_set_new(errors, field, json_string(message));
}

static gboolean read_integer_field(json_t* root, const char* key, guint min, guint max,
                                   guint* value, json_t* errors) {
    json_t* values = json_object_get(root, key);
    if (!json_is_integer(values) || json_integer_value(values) < min ||
        json_integer_value(values) > max) {
        add_field_error(errors, key, "Expected an integer within the allowed range");
        return FALSE;
    }
    *value = (guint)json_integer_value(values);
    return TRUE;
}

gboolean policy_validate(json_t* input, Policy* out, json_t* errors) {
    if (!json_is_object(input)) {
        add_field_error(errors, "request", "Expected a JSON object");
        return FALSE;
    }
    const char* key;
    json_t* value;
    json_object_foreach(input, key, value) {
        if (strcmp(key, "siteName") && strcmp(key, "enabled") &&
            strcmp(key, "occupancyLimitSeconds") && strcmp(key, "alertCooldownSeconds")) {
            add_field_error(errors, key, "Unknown field");
        }
    }
    Policy policy = {0};
    json_t* name = json_object_get(input, "siteName");
    const char* text = json_string_value(name);
    gboolean valid = text && json_string_length(name) > 0 && json_string_length(name) <= 64 &&
                     strlen(text) == json_string_length(name) && g_utf8_validate(text, -1, NULL);
    gboolean has_visible_text = FALSE;
    if (valid) {
        for (const char* character = text; *character; character = g_utf8_next_char(character)) {
            gunichar codepoint = g_utf8_get_char(character);
            if (!g_unichar_isprint(codepoint)) {
                valid = FALSE;
            }
            if (!g_unichar_isspace(codepoint)) {
                has_visible_text = TRUE;
            }
        }
    }
    if (!valid || !has_visible_text) {
        add_field_error(errors, "siteName",
                        "Use 1–64 UTF-8 bytes of printable text, not only spaces");
    } else {
        g_strlcpy(policy.site, text, sizeof(policy.site));
    }
    if (!json_is_boolean(json_object_get(input, "enabled"))) {
        add_field_error(errors, "enabled", "Expected true or false");
    } else {
        policy.enabled = json_is_true(json_object_get(input, "enabled"));
    }
    read_integer_field(input, "occupancyLimitSeconds", 1, 86400, &policy.limit, errors);
    read_integer_field(input, "alertCooldownSeconds", 1, 86400, &policy.cooldown, errors);
    if (json_object_size(errors)) {
        return FALSE;
    }
    /* Do not expose partially validated values to the caller. */
    *out = policy;
    return TRUE;
}

json_t* policy_json(const Policy* policy) {
    return json_pack("{s:s,s:b,s:i,s:i}", "siteName", policy->site, "enabled", policy->enabled,
                     "occupancyLimitSeconds", policy->limit, "alertCooldownSeconds",
                     policy->cooldown);
}

gboolean policy_read(Store* store, Policy* out) {
    const char* names[] = {"SiteName", "Enabled", "OccupancyLimitSeconds", "AlertCooldownSeconds"};
    char* values[4] = {0};
    gboolean ok = TRUE;
    for (guint i = 0; i < 4; i++) {
        if (!store->read(store->context, names[i], &values[i]) || !values[i]) {
            ok = FALSE;
        }
    }
    guint64 limit = 0, cooldown = 0;
    if (ok) {
        ok = (!strcmp(values[1], "yes") || !strcmp(values[1], "no")) &&
             g_ascii_string_to_unsigned(values[2], 10, 1, 86400, &limit, NULL) &&
             g_ascii_string_to_unsigned(values[3], 10, 1, 86400, &cooldown, NULL);
    }
    if (ok) {
        json_t* input = json_pack("{s:s,s:b,s:I,s:I}", "siteName", values[0], "enabled",
                                  !strcmp(values[1], "yes"), "occupancyLimitSeconds",
                                  (json_int_t)limit, "alertCooldownSeconds", (json_int_t)cooldown);
        json_t* errors = json_object();
        ok = policy_validate(input, out, errors);
        json_decref(input);
        json_decref(errors);
    }
    for (guint i = 0; i < 4; i++) {
        g_free(values[i]);
    }
    return ok;
}

static json_t* make_error_response(int* status, int code, const char* error) {
    *status = code;
    return json_pack("{s:b,s:s}", "ok", FALSE, "error", error);
}

json_t* policy_save(Store* store, json_t* input, int* status) {
    /* 1. Validate the complete request before changing any parameter. */
    Policy requested = {0};
    json_t* errors = json_object();
    if (!policy_validate(input, &requested, errors)) {
        json_t* response = make_error_response(status, 422, "Correct the highlighted settings");
        json_object_set_new(response, "errors", errors);
        return response;
    }
    json_decref(errors);

    /* 2. Every write syncs to persistent storage. Multiple writes are NOT a transaction. */
    char limit[16], cooldown[16];
    g_snprintf(limit, sizeof(limit), "%u", requested.limit);
    g_snprintf(cooldown, sizeof(cooldown), "%u", requested.cooldown);
    const char* names[] = {"SiteName", "Enabled", "OccupancyLimitSeconds", "AlertCooldownSeconds"};
    const char* values[] = {requested.site, requested.enabled ? "yes" : "no", limit, cooldown};
    gboolean ok = TRUE;
    const char* failed_parameter = NULL;
    for (guint i = 0; i < 4; i++) {
        if (!store->write(store->context, names[i], values[i])) {
            ok = FALSE;
            failed_parameter = names[i];
            break;
        }
    }

    /* 3. Read back even after failure: the UI must show a possible partial save. */
    Policy actual = {0};
    gboolean readable = policy_read(store, &actual);
    gboolean matches = readable && !strcmp(actual.site, requested.site) &&
                       actual.enabled == requested.enabled && actual.limit == requested.limit &&
                       actual.cooldown == requested.cooldown;
    json_t* result;
    if (ok && matches) {
        *status = 200;
        result = json_pack("{s:b}", "ok", TRUE);
    } else {
        result = make_error_response(status, 500,
                                     "Save not confirmed. Some values may have changed; inspect "
                                     "saved settings before retrying.");
    }
    if (failed_parameter) {
        json_object_set_new(result, "failedParameter", json_string(failed_parameter));
    }
    json_object_set_new(result, "settings", readable ? policy_json(&actual) : json_null());
    return result;
}

json_t* policy_test(Store* store, json_t* input, int* status) {
    json_t* errors = json_object();
    guint occupied_seconds = 0, seconds_since_last_alert = 0;
    if (!json_is_object(input)) {
        add_field_error(errors, "request", "Expected a JSON object");
    } else {
        const char* key;
        json_t* value;
        json_object_foreach(input, key, value) if (strcmp(key, "occupiedSeconds") &&
                                                   strcmp(key, "secondsSinceLastAlert"))
            add_field_error(errors, key, "Unknown field");
        read_integer_field(input, "occupiedSeconds", 0, 604800, &occupied_seconds, errors);
        json_t* values = json_object_get(input, "secondsSinceLastAlert");
        if (!values || !json_is_null(values)) {
            read_integer_field(input, "secondsSinceLastAlert", 0, 604800, &seconds_since_last_alert,
                               errors);
        }
    }
    if (json_object_size(errors)) {
        json_t* response = make_error_response(status, 422, "Invalid test inputs");
        json_object_set_new(response, "errors", errors);
        return response;
    }
    json_decref(errors);
    Policy policy = {0};
    if (!policy_read(store, &policy)) {
        return make_error_response(status, 500, "Cannot read saved policy");
    }
    gboolean overdue = policy.enabled && occupied_seconds >= policy.limit;
    gboolean eligible = overdue && (json_is_null(json_object_get(input, "secondsSinceLastAlert")) ||
                                    seconds_since_last_alert >= policy.cooldown);
    /* Classification and alert eligibility are different decisions. An overdue
     * area stays overdue even while the cooldown suppresses another alert. */
    const char* state;
    const char* explanation;
    if (!policy.enabled) {
        state = "Disabled";
        explanation = "Monitoring is disabled.";
    } else if (!overdue) {
        state = "Within limit";
        explanation = "Occupancy has not reached the limit.";
    } else {
        state = "Overdue";
        if (eligible) {
            explanation = "An alert would be eligible. No alert was sent.";
        } else {
            explanation = "Overdue, but the alert cooldown has not elapsed.";
        }
    }

    *status = 200;
    json_t* response = json_pack("{s:b,s:s,s:b,s:s}", "ok", TRUE, "state", state, "alertEligible",
                                 eligible, "explanation", explanation);
    json_object_set_new(response, "settings", policy_json(&policy));
    return response;
}
