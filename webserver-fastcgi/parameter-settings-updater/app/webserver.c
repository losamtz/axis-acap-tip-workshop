#include "fcgi_stdio.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <syslog.h>
#include "webserver.h"
#include <glib-unix.h>



#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

extern char* password; // This should be defined in your main application file
extern GMutex password_mutex;

// Function to run the FastCGI server
void* fcgi_run(void* data) {
    
    (void)data; // Unused parameter
    // Initialize the FastCGI library
    syslog(LOG_INFO, "Starting FastCGI server...");
    int sock;
    FCGX_Request request;
    char* socket_path = NULL;
    int status;

    // Open the syslog for logging
    // openlog("webfcgi", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "password: %s", password ? password : "NULL");
    // Get the socket path from the environment variable
    socket_path = getenv(FCGI_SOCKET_NAME);

    if (!socket_path) {
        syslog(LOG_ERR, "Environment variable %s not set", FCGI_SOCKET_NAME);
        return NULL;
    }

    // Initialize the FastCGI request
    status =FCGX_Init();

    if (status != 0) {
        syslog(LOG_ERR, "Failed to get environment variable FCGI_SOCKET_NAME");
        return NULL;
    }

    sock = FCGX_OpenSocket(socket_path, 5);

    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO );

    status = FCGX_InitRequest(&request, sock, 0);

    if (status != 0) {
        syslog(LOG_ERR, "Failed to initialize FastCGI request");
        return NULL;
    }

    syslog(LOG_INFO, "FastCGI server started on socket %s", socket_path);
    while(FCGX_Accept_r(&request) == 0) {
        syslog(LOG_INFO, "FCGX_Accept_r OK");

        // Write the http header
        FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n");

        
        // Parse the uri and the query string
        const char* uriString = FCGX_GetParam("REQUEST_URI", request.envp);
        syslog(LOG_INFO, "URI: %s", uriString);
        syslog(LOG_INFO, "password: %s", password ? password : "NULL");

        g_mutex_lock(&password_mutex);
        FCGX_FPrintF(request.out, "%s", password ? password : "");
        g_mutex_unlock(&password_mutex);
            
        FCGX_Finish_r(&request);
        
    }
    return NULL;
}
