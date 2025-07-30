#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <syslog.h>
#include <libgen.h>
#include <assert.h>
#include <stdarg.h>


static GMainLoop *main_loop = NULL;
static AXParameter *axparameter = NULL;
guint intervalSecs = 0;       // Default value for the IntervalSecs parameter
guint32 multicastPort = 0;    // Default value for the MulticastPort parameter
guint16 multicastAddress = 0; // Default value for the MulticastAddress parameter
char *password = NULL;
// Keep this flag globally or static at file level
static gboolean login_initialized = FALSE;

// Assuming these globals hold current credentials (make sure they are initialized to NULL)
static char *onvifUsername = NULL;

guint32 passwordLength = 0; // Length of the password

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
// Print an error to syslog and exit the application if a fatal error occurs.
__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    vsyslog(LOG_ERR, format, arg);
    va_end(arg);
    exit(1);
}
static void add_param(AXParameter *axparameter, const char *name, const char *default_value, const char *meta)
{
    GError *error = NULL;
    if (!ax_parameter_add(axparameter, name, default_value, meta, &error))
    {
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
static void add_parameters(AXParameter *axparameter)
{
    add_param(axparameter, "IntervalSecs", "10", "hidden:int:maxlen=2;min=0;max=60");
    add_param(axparameter, "MulticastAddress", "224.0.0.1", "hidden:string:maxlen=64");
    add_param(axparameter, "MulticastPort", "1024", "hidden:int:maxlen=5;min=1024;max=65535");
    /* Add parameter "OnvifUsername" */
    add_param(axparameter, "OnvifUsername", "", "hidden:string:maxlen=64");
    /* Add parameter "Password" */
    add_param(axparameter, "Password", "", "hidden:string:maxlen=64");
    /* Add parameter "Length" */
    add_param(axparameter, "Length", "0", "hidden:int:maxlen=2;min=0;max=12");
    syslog(LOG_INFO, "All parameters have been added to the handle.");
}
static void print_all_parameters(AXParameter *axparameter)
{
    GError *error = NULL;
    GList *list = ax_parameter_list(axparameter, &error);

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
static char *get_param(AXParameter *axparameter, const char *name)
{
    assert(NULL != axparameter);
    GError *error = NULL;
    gchar *value = NULL;
    if (!ax_parameter_get(axparameter, name, &value, &error))
    {
        syslog(LOG_ERR, "[get-param] failed to get %s parameter", name);
        if (NULL != error)
        {
            syslog(LOG_ERR, "[get-param] %s", (char *)error->message);
            g_error_free(error);
        }
        return NULL;
    }
    syslog(LOG_INFO, "[get-param] Got %s value: %s", name, value);
    return value;
}
static void interval_callback(const gchar *name, const gchar *value, gpointer user_data)
{
    (void)user_data; // Unused parameter
    if (NULL == value)
    {
        syslog(LOG_ERR, "[interval-callback] Unexpected NULL value for %s", name);
        return;
    }
    syslog(LOG_INFO, "[interval-callback] IntervalSecs callback triggered with value: %s", (char *)value);
    intervalSecs = atoi(value);
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
static void onvif_username_callback(const gchar *name, const gchar *value, gpointer user_data)
{
    (void)user_data; // Unused parameter
    if (NULL == value)
    {
        syslog(LOG_ERR, "[onvif-username-callback] Unexpected NULL value for %s", name);
        return;
    }
    syslog(LOG_INFO, "[onvif-username-callback] OnvifUsername parameter changed to '%s'", (char *)value);

    if (!login_initialized)
    {
        // First time: accept username if non-empty
        if (value[0] != '\0')
        {
            free(onvifUsername);
            onvifUsername = strdup(value);

            // If password is already set and non-empty, mark login initialized
            if (password != NULL && password[0] != '\0')
            {
                login_initialized = true;
                syslog(LOG_INFO, "[onvif-username-callback] Initial login recorded with username '%s'", onvifUsername);
            }
        }
        else
        {
            syslog(LOG_WARNING, "[onvif-username-callback] Attempt to set empty OnvifUsername during initial login");

            struct message *msg = malloc(sizeof(struct message));

            msg->name = strdup("OnvifUsername");
            msg->value = strdup(onvifUsername ? onvifUsername : "");

            // Reject change by resetting param back to old value
            // set_parameter(axparameter, "OnvifUsername", onvifUsername ? onvifUsername : "");
            g_timeout_add_seconds(1, set_parameter, msg);
        }
    }
    else
    {
        // After login initialized: reject changes to username
        if (strcmp(onvifUsername, value) != 0)
        {
            syslog(LOG_WARNING, "[onvif-username-callback] Rejected OnvifUsername change after initial login");

            struct message *msg = malloc(sizeof(struct message));

            msg->name = strdup("OnvifUsername");
            msg->value = strdup(onvifUsername);
            // set_parameter(axparameter, "OnvifUsername", onvifUsername);
            g_timeout_add_seconds(1, set_parameter, msg);
        }
    }
}
static void password_callback(const gchar *name, const gchar *value, gpointer user_data)
{
    (void)user_data; // Unused parameter
    if (NULL == value)
    {
        syslog(LOG_ERR, "[password-callback] Unexpected NULL value for %s", name);
        return;
    }
    syslog(LOG_INFO, "[password-callback] Password parameter changed to '%s'", value);

    if (!login_initialized)
    {
        // First time: accept password if non-empty
        if (value[0] != '\0')
        {
            free(password);
            password = strdup(value);

            // If username is already set and non-empty, mark login initialized
            if (onvifUsername != NULL && onvifUsername[0] != '\0')
            {
                login_initialized = true;
                syslog(LOG_INFO, "[password-callback] Initial login recorded with password (username '%s')", onvifUsername);
            }
            passwordLength = strlen(password);
        }
        else
        {
            syslog(LOG_WARNING, "[password-callback] Attempt to set empty Password during initial login");
            // Reject change by resetting param back to old value

            // set_parameter(axparameter, "Password", password ? password : "");
            struct message *msg = malloc(sizeof(struct message));

            msg->name = strdup("Password");
            msg->value = strdup(password ? password : "");
            g_timeout_add_seconds(1, set_parameter, msg);
        }
    }
    else
    {
        // After login initialized: reject changes to password
        if (strcmp(password, value) != 0)
        {
            syslog(LOG_WARNING, "[password-callback] Rejected Password change after initial login");
            // Reset to the previously set password

            // set_parameter(axparameter, "Password", password);
            struct message *msg = malloc(sizeof(struct message));

            msg->name = strdup("Password");
            msg->value = strdup(password);
            g_timeout_add_seconds(1, set_parameter, msg);
        }
    }
}
static gboolean setup_param(const gchar *name, AXParameterCallback callbackfn)
{
    GError *error = NULL;
    gchar *value = NULL;

    assert(NULL != name);
    assert(NULL != axparameter);
    assert(NULL != callbackfn);

    if (!ax_parameter_register_callback(axparameter, name, callbackfn, NULL, &error))
    {
        syslog(LOG_ALERT, "[setup-param] - failed to register %s callback", name);
        if (NULL != error)
        {
            syslog(LOG_ERR, "[setup-param] - %s", error->message);
            g_error_free(error);
        }
        return FALSE;
    }
    value = get_param(axparameter, name);
    if (NULL == value)
    {
        return FALSE;
    }

    syslog(LOG_INFO, "[setup-param] - Got %s value: %s", name, value);
    callbackfn(name, value, NULL);
    g_free(value);

    return TRUE;
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
    // Add parameters to the handle
    add_parameters(axparameter);

    syslog(LOG_INFO, "Parameters added to the handle");
    // Print all parameters
    print_all_parameters(axparameter);
    syslog(LOG_INFO, "All parameters printed");

    // clang-format off
    if (!setup_param("IntervalSecs", interval_callback) ||
        !setup_param("MulticastAddress", multicast_address_callback) ||
        !setup_param("MulticastPort", multicast_port_callback) ||
        !setup_param("OnvifUsername", onvif_username_callback) ||
        !setup_param("Password", password_callback))
    // clang-format on
    {
        ret = EXIT_FAILURE;
        goto exit_param;
    }
    syslog(LOG_INFO, "All parameters callbacks registered successfully");

    // Start listening to callbacks by launching a GLib main loop.
    main_loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, signal_handler, main_loop);
    g_unix_signal_add(SIGINT, signal_handler, main_loop);
    g_main_loop_run(main_loop);

    // Cleanup
    syslog(LOG_INFO, "Cleaning up resources ...");
    g_free(onvifUsername);
    g_free(password);
    

    syslog(LOG_INFO, "Stopping %s", app_name);
    // Unregister the callbacks
    g_main_loop_unref(main_loop);

exit_param:
    syslog(LOG_INFO, "Unregistering callbacks and freeing parameter handle ...");
    ax_parameter_free(axparameter);

    syslog(LOG_INFO, "Closing syslog ...");
    closelog();
    return ret;
}
