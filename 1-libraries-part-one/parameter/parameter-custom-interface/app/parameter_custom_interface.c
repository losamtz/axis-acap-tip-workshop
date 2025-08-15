#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>
#include <libgen.h>
#include <assert.h>
#include <stdarg.h>
#include "panic.h"


static GMainLoop *main_loop = NULL;
static AXParameter *axparameter = NULL;

guint32 multicastPort = 0;    // Default value for the MulticastPort parameter
guint16 multicastAddress = 0; // Default value for the MulticastAddress parameter

struct message
{
    char *name;
    char *value;
};

static gboolean signal_handler(gpointer main_loop)
{
    g_main_loop_quit((GMainLoop *)main_loop);
    syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");
    return G_SOURCE_REMOVE;
}

static gboolean set_parameter(void *msg_ptr)
{
    GError *error = NULL;
    struct message *msg = msg_ptr;

    // Set the parameter with the given name to the given value.
    if (!ax_parameter_set(axparameter, msg->name, msg->value, TRUE, &error))
        panic("%s", error->message);

    syslog(LOG_INFO, "[set-param] Parameter '%s' set to '%s'", msg->name, msg->value);

    free(msg->name);
    free(msg->value);
    free(msg);
    return FALSE;
}


static void multicast_address_callback(const gchar *name, const gchar *value, gpointer user_data)
{
    (void)user_data; // Unused parameter
    if (NULL == value)
    {
        syslog(LOG_ERR, "[multicast-address-callback] Unexpected NULL value for %s", name);
        return;
    }
    syslog(LOG_INFO, "[multicast-address-callback] MulticastAddress parameter changed to '%s'", (char *)value);

    struct message *msg = malloc(sizeof(struct message));
    char *param_to_change = "root.Network.RTP.R0.VideoAddress";

    ;
    msg->name = strdup(param_to_change);
    msg->value = strdup(value);

    // set_parameter(axparameter, "root.Network.RTP.R0.VideoAddress", (char*)value); // might need a settimeout

    g_timeout_add_seconds(1, set_parameter, msg);

    const int new_multicastAddress = atoi(value);
    multicastAddress = new_multicastAddress;
    syslog(LOG_INFO, "[multicast-address-callback] MulticastAddress set to %s", value);
}
static void multicast_port_callback(const gchar *name, const gchar *value, gpointer user_data)
{
    (void)user_data; // Unused parameter
    if (NULL == value)
    {
        syslog(LOG_ERR, "[multicast-port-callback] Unexpected NULL value for %s", name);
        return;
    }
    syslog(LOG_INFO, "[multicast-port-callback] MulticastPort parameter changed to '%s'", (char *)value);

    struct message *msg = malloc(sizeof(struct message));
    char *param_to_change = "root.Network.RTP.R0.VideoPort";

    msg->name = strdup(param_to_change);
    msg->value = strdup(value);

    // set_parameter(axparameter, "root.Network.RTP.R0.VideoPort", (char*)value); // might need a settimeout
    g_timeout_add_seconds(1, set_parameter, msg);

    multicastPort = atoi(value);
}



int main(int argc, char **argv)
{
    (void)argc;
    GError *error = NULL;
    char *app_name = basename(argv[0]);

    openlog(app_name, LOG_PID, LOG_USER);

    int ret = EXIT_SUCCESS;
    syslog(LOG_INFO, "Starting %s", app_name);

    // Initialize the AXParameter handle
    syslog(LOG_INFO, "Initializing AXParameter handle ...");

    axparameter = ax_parameter_new(app_name, &error);

    if (axparameter == NULL)
        panic("%s", error->message);

    syslog(LOG_INFO, "Starting handle");
    
    // 2- register callbacks for the parameters
    if(!ax_parameter_register_callback(axparameter, "MulticastAddress", multicast_address_callback, NULL, &error))
        panic("%s", error->message);

    if(!ax_parameter_register_callback(axparameter, "MulticastPort", multicast_port_callback, NULL, &error))
        panic("%s", error->message);

    syslog(LOG_INFO, "All parameters callbacks registered successfully");

    // 3 - Start listening to callbacks by launching a GLib main loop.
    main_loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, signal_handler, main_loop);
    g_unix_signal_add(SIGINT, signal_handler, main_loop);
    g_main_loop_run(main_loop);

    // 4 - Cleanup
    syslog(LOG_INFO, "Cleaning up resources ...");
    syslog(LOG_INFO, "Stopping %s", app_name);
    // Unregister the callbacks
    g_main_loop_unref(main_loop);

    syslog(LOG_INFO, "Unregistering callbacks and freeing parameter handle ...");
    ax_parameter_free(axparameter);

    syslog(LOG_INFO, "Closing syslog ...");
    closelog();
    return ret;
}
