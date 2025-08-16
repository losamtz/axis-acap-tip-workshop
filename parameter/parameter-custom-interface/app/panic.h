#ifndef PANIC_H
#define PANIC_H

// Print an error to syslog and exit the application if a fatal error occurs.
 __attribute__((noreturn)) __attribute__((format(printf, 1, 2))) void 
panic(const char *format, ...);

#endif // PANIC_H