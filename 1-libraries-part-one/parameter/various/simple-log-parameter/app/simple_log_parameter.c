#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>

#define APP_NAME "simple_log_parameter"



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


int main(void) {
    GError* error   = NULL;
    GMainLoop* loop = NULL;

    openlog(APP_NAME, LOG_PID, LOG_USER);

    AXParameter* handle = ax_parameter_new(APP_NAME, &error);
    if (handle == NULL)
        panic("%s", error->message);

    // Parameters outside the application's group requires qualification.
    gchar* root_image_rotation;
    if (!ax_parameter_get(handle, "root.ImageSource.I0.Rotation", &root_image_rotation, &error))
        syslog(LOG_ERR, "[error root.ImageSource.I0.Rotation] - %s", error->message);
    syslog(LOG_INFO, "RootImageRotation: '%s'", root_image_rotation);
    g_free(root_image_rotation);

    // Parameters outside the application's group requires qualification.
    gchar* image_exposure;
    if (!ax_parameter_get(handle, "ImageSource.I0.Sensor.Exposure", &image_exposure, &error))
        syslog(LOG_ERR, "[error ImageSource.I0.Sensor.Exposure] - %s", error->message);
    syslog(LOG_INFO, "ImageExposure: '%s'", image_exposure);
    g_free(image_exposure);

    // Start listening to callbacks by launching a GLib main loop.
    loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    ax_parameter_free(handle);
}
