#ifndef CURL_REQUEST_H
#define CURL_REQUEST_H

#include <curl/curl.h>

char* vapix_post(CURL* , const char* , const char* , const char* );

#endif // CURL_REQUEST_H