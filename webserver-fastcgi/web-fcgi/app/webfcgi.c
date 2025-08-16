/**
 * Copyright (C) 2022 Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fcgi_stdio.h"
#include "uriparser/Uri.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <syslog.h>

#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

static char* parse_uri(char* uriString) {
    // This function is a placeholder for URI parsing logic.
    UriUriA uri;
    const char* errorPos;

    // Initialize the URI structure
    if (uriParseSingleUriA(&uri, uriString, &errorPos) != URI_SUCCESS) {
        FCGX_FPrintF(request.out, "Failed to parse URI: %s\n", uriString);
        syslog(LOG_ERR, "Failed to parse URI: %s", uriString);
        FCGX_Finish_r(&request);
        continue;

    }
    syslog(LOG_INFO, "Parsing URI: %s", uriString);
    FCGX_FPrintF(request.out, "<div>URI: %s</div>", uriString);
    return uriString;

}
static char* parse_query_string(char* queryString) {

}

static int fcgi_run(void) {
    int sock;
    FCGX_Request request;
    char* socket_path = NULL;
    int status;

    // Open the syslog for logging
    openlog("webfcgi", LOG_PID, LOG_DAEMON);

    // Get the socket path from the environment variable
    socket_path = getenv(FCGI_SOCKET_NAME);

    if (!socket_path) {
        syslog(LOG_ERR, "Environment variable %s not set", FCGI_SOCKET_NAME);
        return EXIT_FAILURE;
    }

    // Initialize the FastCGI request
    status =FCGX_Init();

    if (status != 0) {
        syslog(LOG_ERR, "Failed to get environment variable FCGI_SOCKET_NAME");
        return EXIT_FAILURE;
    }

    sock = FCGX_OpenSocket(socket_path, 5);

    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO );

    status = FCGX_InitRequest(&request, sock, 0);

    if (status != 0) {
        syslog(LOG_ERR, "Failed to initialize FastCGI request");
        return status;
    }

    syslog(LOG_INFO, "FastCGI server started on socket %s", socket_path);
    while(FCGX_Accept_r(&request) == 0) {
        syslog(LOG_INFO, "FCGX_Accept_r OK");

        // Write the http header
        FCGX_FPrintF(request.out, "Content-type: text/html\r\n\r\n");
        // Parse the uri and the query string
        const char* uriString = FCGX_GetParam("REQUEST_URI", request.envp);
        syslog(LOG_INFO, "URI: %s", uriString);


        if(uriString)
            FCGX_FPrintF(request.out, "<div>REQUEST_URI: %s</div>", uriString);

       

        
        FCGX_Finish_r(&request);
        
    }
    return EXIT_SUCCESS;
}

int main(void) {
    int ret;
    ret = fcgi_run();
    
    // Close the syslog
    closelog();
    return ret;
}