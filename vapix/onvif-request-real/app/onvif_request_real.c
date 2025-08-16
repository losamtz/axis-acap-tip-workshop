#include <gio/gio.h>
#include <curl/curl.h>
#include <glib.h>
#include <glib-unix.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <libgen.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>

// #define APP_NAME "onvif-multicast-updater"

// guint intervalSecs = 0;       // Default value for the IntervalSecs parameter
// guint32 multicastPort = 0;    // Default value for the MulticastPort parameter
// guint16 multicastAddress = 0; // Default value for the MulticastAddress parameter
// Keep this flag globally or static at file level

// static gboolean signal_handler(gpointer main_loop)
// {
//     g_main_loop_quit((GMainLoop *)main_loop);
//     syslog(LOG_INFO, "Application was stopped by SIGTERM or SIGINT.");
//     return G_SOURCE_REMOVE;
// }
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

static char *parse_credentials(GVariant *result)
{
    char *credentials_string = NULL;
    char *id = NULL;
    char *password = NULL;

    g_variant_get(result, "(&s)", &credentials_string);
    if (sscanf(credentials_string, "%m[^:]:%ms", &id, &password) != 2)
        panic("Error parsing credential string '%s'", credentials_string);
    char *credentials = g_strdup_printf("%s:%s", id, password);

    free(id);
    free(password);
    return credentials;
}

static char *retrieve_onvif_credentials(const char *username)
{
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection)
        panic("Error connecting to D-Bus: %s", error->message);

    const char *bus_name = "com.axis.HTTPConf1";
    const char *object_path = "/com/axis/HTTPConf1/VAPIXServiceAccounts1";
    const char *interface_name = "com.axis.HTTPConf1.VAPIXServiceAccounts1";
    const char *method_name = "GetCredentials";

    GVariant *result = g_dbus_connection_call_sync(connection,
                                                   bus_name,
                                                   object_path,
                                                   interface_name,
                                                   method_name,
                                                   g_variant_new("(s)", username),
                                                   NULL,
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1,
                                                   NULL,
                                                   &error);
    if (!result)
        panic("Error invoking D-Bus method: %s", error->message);

    char *credentials = parse_credentials(result);
    syslog(LOG_INFO, "Retrieved VAPIX credentials for user '%s': %s", username, credentials);

    g_variant_unref(result);
    g_object_unref(connection);
    return credentials;
}

static size_t append_to_gstring_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t processed_bytes = size * nmemb;
    g_string_append_len((GString *)userdata, ptr, processed_bytes);
    return processed_bytes;
}

static char *onvif_post(CURL *handle, const char *credentials, const char *endpoint, const char *soap_xml_body)
{
    (void)endpoint; // Unused parameter, but kept for consistency with the original code
    syslog(LOG_INFO, "[vapix-post]: %s", credentials);
    syslog(LOG_INFO, "[vapix-post]: %s", soap_xml_body);

    if (!handle || !credentials || !soap_xml_body)
        panic("Invalid parameters passed to onvif_post");

    GString *response = g_string_new(NULL);
    char *url = "http://127.0.0.12/vapix/services";

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/soap+xml;charset=UTF-8;action=\"http://www.onvif.org/ver20/media/wsdl/SetVideoEncoderConfiguration\"");

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(handle, CURLOPT_USERPWD, credentials);
    // Disable certificate validation (INSECURE)
    // curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    // curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, soap_xml_body);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_to_gstring_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);

    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK)
        panic("curl_easy_perform error %d: '%s'", res, curl_easy_strerror(res));

    long response_code;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);

    syslog(LOG_INFO, "[onvif-post]: Response code: %ld", response_code);

    if (response_code != 200)
        panic("Got response code %ld from request to %s with response '%s'",
              response_code,
              soap_xml_body,
              response->str);

    syslog(LOG_INFO, "[onvif-post]: Cleaning up resources ...");
    curl_easy_cleanup(handle);
    return g_string_free(response, FALSE);
}

static char *onvif_post_xml(CURL *handle, const char *credentials, const char *endpoint, const char *soap_xml_body)
{
    (void)endpoint; // Unused parameter, but kept for consistency with the original code

    char *text_response = onvif_post(handle, credentials, endpoint, soap_xml_body);
    syslog(LOG_INFO, "[onvif-post-xml]: %s", text_response);
    if (!text_response)
    {
        syslog(LOG_ERR, "Failed to perform request to ONVIF service");
        free(text_response);
        return NULL;
    }

    return text_response;
}
static char *set_onvif_properties(CURL *handle, const char *credentials)
{

    // const char *soap_xml_body = g_strdup_printf(
    //     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    //     "<Envelope xmlns=\"http://www.w3.org/2003/05/soap-envelope\">\n"
    //     " <Header/>\n"
    //     " <Body>\n"
    //     "   <tr2:SetVideoEncoderConfiguration\n"
    //     "     xmlns:tr2=\"http://www.onvif.org/ver20/media/wsdl\"\n"
    //     "     xmlns=\"http://www.onvif.org/ver10/schema\">\n"
    //     "    <tr2:Configuration\n"
    //     "     token=\"default_1_h264\"\n"
    //     "     GovLength=\"32\"\n"
    //     "     Profile=\"Main\">\n"
    //     "     <UseCount>0</UseCount>\n"
    //     "     <Name>default_1 h264</Name>\n"
    //     "     <Encoding>H264</Encoding>\n"
    //     "     <Resolution>\n"
    //     "      <Width>3840</Width>\n"
    //     "      <Height>2160</Height>\n"
    //     "     </Resolution>\n"
    //     "     <RateControl>\n"
    //     "      <FrameRateLimit>30</FrameRateLimit>\n"
    //     "      <BitrateLimit>2147483647</BitrateLimit>\n"
    //     "     </RateControl>\n"
    //     "     <Quality>70</Quality>\n"
    //     "     <Multicast>\n"
    //     "      <Address>\n"
    //     "       <Type>IPv4</Type>\n"
    //     "       <IPv4Address>224.0.0.69</IPv4Address>\n"
    //     "      </Address>\n"
    //     "      <Port>6969</Port>\n"
    //     "      <TTL>5</TTL>\n"
    //     "      <AutoStart>false</AutoStart>\n"
    //     "     </Multicast>\n"
    //     "    </tr2:Configuration>\n"
    //     "   </tr2:SetVideoEncoderConfiguration>\n"
    //     " </Body>\n"
    //     "</Envelope>\n");

    const char *soap_body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsdl=\"http://www.onvif.org/ver20/media/wsdl\" xmlns:sch=\"http://www.onvif.org/ver10/schema\">"
        "<soap:Header/>"
        "<soap:Body>"
        "<wsdl:SetVideoEncoderConfiguration>"
        "<wsdl:Configuration token=\"default_1_h264\" GovLength=\"32\">"
        "<sch:Name>default_1 h264</sch:Name>"
        "<sch:UseCount>0</sch:UseCount>"
        "<sch:Encoding>H264</sch:Encoding>"
        "<sch:Resolution>"
        "<sch:Width>3840</sch:Width>"
        "<sch:Height>2160</sch:Height>"
        "</sch:Resolution>"
        "<sch:RateControl ConstantBitRate=\"false\">"
        "<sch:FrameRateLimit>25</sch:FrameRateLimit>"
        "<sch:BitrateLimit>2147483647</sch:BitrateLimit>"
        "</sch:RateControl>"
        "<sch:Multicast>"
        "<sch:Address>"
        "<sch:Type>IPv4</sch:Type>"
        "<sch:IPv4Address>224.0.0.72</sch:IPv4Address>"
        "</sch:Address>"
        "<sch:Port>7072</sch:Port>"
        "<sch:TTL>5</sch:TTL>"
        "<sch:AutoStart>false</sch:AutoStart>"
        "</sch:Multicast>"
        "<sch:Quality>70</sch:Quality>"
        "</wsdl:Configuration>"
        "</wsdl:SetVideoEncoderConfiguration>"
        "</soap:Body>"
        "</soap:Envelope>";

    return onvif_post_xml(handle, credentials, NULL, soap_body);
}

int main(int argc, char **argv)
{
    (void)argc;
    // GError *error = NULL;
    char *app_name = basename(argv[0]);
    // Initialize the mutex

    // Open syslog for logging
    openlog(app_name, LOG_PID, LOG_USER);

    int ret = EXIT_SUCCESS;
    syslog(LOG_INFO, "Starting %s", app_name);

    CURL *handle = curl_easy_init();

    syslog(LOG_INFO, "Initializing CURL handle ...");

    if (!handle)
    {

        syslog(LOG_ERR, "Failed to initialize CURL");
        ret = EXIT_FAILURE;
        goto clean_up;
    }

    char *credentials = retrieve_onvif_credentials("random_user");
    syslog(LOG_INFO, "[main] - Retrieving VAPIX credentials ... %s", credentials ? "success" : "failure");

    if (!credentials)
    {
        syslog(LOG_ERR, "Failed to retrieve VAPIX credentials");
        ret = EXIT_FAILURE;
        goto clean_up;
    }
    syslog(LOG_INFO, "VAPIX credentials retrieved successfully");

    char *response = set_onvif_properties(handle, credentials);
    syslog(LOG_INFO, "[main] - Setting ONVIF properties ... %s", response ? "success" : "failure");

    if (!response)
    {
        syslog(LOG_ERR, "Failed to send ONVIF request");
        ret = EXIT_FAILURE;
        g_free(response);
        goto clean_up;
    }

    g_free(response);
    g_free(credentials);
clean_up:

    // Cleanup
    syslog(LOG_INFO, "Cleaning up resources ...");

    curl_easy_cleanup(handle);

    syslog(LOG_INFO, "Unregistering callbacks and freeing parameter handle ...");

    syslog(LOG_INFO, "Closing syslog ...");
    closelog();
    return ret;
}
