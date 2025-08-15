#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include "panic.h"

void panic(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    vsyslog(LOG_ERR, format, arg);
    va_end(arg);
    exit(1);
}