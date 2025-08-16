#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>

#define APP_NAME "parameter_runtime"



static gboolean signal_handler(gpointer loop) {
    g_main_loop_quit((GMainLoop*)loop);
    syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");
    return G_SOURCE_REMOVE;
}
// Print an error to syslog and exit the application if a fatal error occurs.
__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list arg;
    va_start(arg, format);
    vsyslog(LOG_ERR, format, arg);
    va_end(arg);
    exit(1);
}

static void add_parameter(AXParameter* handle, const char* name, const char* default_value, const char *meta) {
    GError *error = NULL;
    if (!ax_parameter_add(handle, name, default_value, meta, &error)) {
        if (error->code == AX_PARAMETER_PARAM_ADDED_ERROR)
        {
            /* parameter is already added. Nothing to care about */
            g_error_free(error);
            error = NULL;
        }
        else
        {
            syslog(LOG_ERR, "[add-param] Failed to add parameter %s: %s", name, error->message);
            g_error_free(error);
            panic("Panic: Failed to add parameter %s", name);
        }
    }
    syslog(LOG_INFO, "[add-param] - The parameter %s was added!", name);
}
static void set_parameter(AXParameter* handle, const char* name, const char* value) {
    GError *error = NULL;
    if (!ax_parameter_set(handle, name, value, TRUE, &error)) { // TRUE to run the callback
        g_error_free(error);
        syslog(LOG_ERR, "[set-param] Failed to set parameter '%s' to '%s': %s", name, value, error->message);
        panic("Failed to set parameter '%s' to '%s': %s", name, value, error->message);
    }
    syslog(LOG_INFO, "[set-param] Parameter '%s' set to '%s'", name, value);
}
static void remove_parameter(AXParameter* handle, const char* name) {
    GError *error;

    if (!ax_parameter_remove(handle, name, &error)) {
        g_error_free(error);
        syslog(LOG_ERR, "[remove-param] Failed to remove parameter '%s': %s", name, error->message);
        panic("Failed to remove parameter '%s': %s", name, error->message);
    }
    syslog(LOG_INFO, "[remove-param] Parameter '%s' removed", name);
}
static void print_parameters(AXParameter* handle) {
    GError* error = NULL;
    GList* list = ax_parameter_list(handle, &error);

    if (!list)
        panic("panic: %s", error->message);

    for (GList *x = list; x != NULL; x = g_list_next(x))
    {
        syslog(LOG_INFO, "[list-param] - %s", (gchar *)x->data);
        g_free(x->data);
    }
    g_list_free(list);
    syslog(LOG_INFO, "[list-param] - All parameter at acap scope have been printed!");
}

static void acap_parameter_changed(const gchar* name, const gchar* value, gpointer handle_void_ptr){

    // This function is called when the parameter changes.
    (void)handle_void_ptr; // Unused parameter, but required by the callback signature.
    
    const char* name_without_qualifiers = &name[strlen("root." APP_NAME ".")];
    syslog(LOG_INFO, "%s was changed to '%s' just now", name_without_qualifiers, value);

    
}
int main(void) {
    GError* error   = NULL;
    GMainLoop* loop = NULL;

    openlog(APP_NAME, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting %s", APP_NAME);

    // 1. Create a new AXParameter handle.
    AXParameter* handle = ax_parameter_new(APP_NAME, &error);
    if(handle == NULL)
        panic("%s", error->message);

    syslog(LOG_INFO, "Starting handle");

    // 2. Add parameters to the handle.
    add_parameter(handle, "ParameterRuntime", "no", "string");
    add_parameter(handle, "ParameterToRemoveRuntime", "yes", "string");

    // 3. print all parameters in the handle.
    print_parameters(handle);

    // 4. remove a parameter from the handle.
    remove_parameter(handle, "ParameterToRemoveRuntime");

    // 5. Set a parameter to a new value.
    set_parameter(handle, "ParameterRuntime", "yes");

    // 6. print all parameters in the handle after removing one.
    print_parameters(handle);

    // 7. Act on changes to IsCustomized as soon as they happen.
    if(!ax_parameter_register_callback(handle, "ParameterRuntime", acap_parameter_changed, NULL, &error))
        panic("%s", error->message);

    if(!ax_parameter_register_callback(handle, "ParameterToRemoveRuntime", acap_parameter_changed, NULL, &error))
        panic("%s", error->message);
    
    // 3. Start listening to callbacks by launching a GLib main loop.
    loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);
    g_main_loop_run(loop);

    
    g_main_loop_unref(loop);
    ax_parameter_free(handle);
}