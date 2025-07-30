#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>

#define APP_NAME "printall"



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

static void print_all_parameters(AXParameter* handle) {
    GError* error = NULL;
    GList* list = ax_parameter_list(handle, &error);

    if(!list)
        panic("panic: %s", error->message);

    for(GList* x = list; x != NULL; x = g_list_next(x)) {
        syslog(LOG_INFO, "%s", (gchar*)x->data);
        g_free(x->data);
    }
    g_list_free(list);
    syslog(LOG_INFO, "All parameter at acap scope have been printed!");
}

int main(void) {
    GError* error   = NULL;
    GMainLoop* loop = NULL;

    openlog(APP_NAME, LOG_PID, LOG_USER);

    AXParameter* handle = ax_parameter_new(APP_NAME, &error);
    if (handle == NULL)
        panic("%s", error->message);

    // Print all parameters
    print_all_parameters(handle);

    // Start listening to callbacks by launching a GLib main loop.
    loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    ax_parameter_free(handle);
}
