#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>

#define APP_NAME "checkparameter"

struct message {
    AXParameter* handle;
    char* name;
    char* value;
};

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

static gboolean monitor_parameter(void* msg_void_ptr) {
    
    GError* error = NULL;
    struct message* msg = msg_void_ptr;
    AXParameter* handle = msg->handle;
    
    gchar* parameter_check;
    if(!ax_parameter_get(handle, msg->value, &parameter_check, &error ))
        syslog(LOG_WARNING, "%s", error->message);
        //panic("Panic %s", error->message);

    syslog(LOG_INFO, "Camera parameter '%s' has value '%s'", msg->value, parameter_check);

    g_free(parameter_check);

    // Free all memory of the message struct and tell GLib not to make this call again.
    free(msg->name);
    free(msg->value);
    free(msg);
    return FALSE; //FALSE to stop the timer
}

static void parameter_changed(const gchar* name, const gchar* value, gpointer handle_void_ptr){
    
    const char* name_without_qualifiers = &name[strlen("root." APP_NAME ".")];
    syslog(LOG_INFO, "Checking parameter '%s' value", value);

    struct message* msg = malloc(sizeof(struct message));

    msg->handle = handle_void_ptr;
    msg->name = strdup(name_without_qualifiers);
    msg->value = strdup(value);

    g_timeout_add_seconds(1, monitor_parameter, msg);
}



int main(void) {
    GError* error   = NULL;
    GMainLoop* loop = NULL;

    openlog(APP_NAME, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting %s", APP_NAME);
    AXParameter* handle = ax_parameter_new(APP_NAME, &error);

    if (handle == NULL)
        panic("%s", error->message);

    syslog(LOG_INFO, "Starting handle");

    // Act on changes to IsCustomized as soon as they happen.
    if (!ax_parameter_register_callback(handle, "CameraParameter", parameter_changed, handle, &error))
        panic("%s", error->message);
    
    // Start listening to callbacks by launching a GLib main loop.
    loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);
    g_main_loop_run(loop);

    
    g_main_loop_unref(loop);
    ax_parameter_free(handle);

}
