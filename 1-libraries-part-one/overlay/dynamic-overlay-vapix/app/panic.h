#ifndef PANIC_H
#define PANIC_H


// Declare the panic function
 __attribute__((noreturn)) __attribute__((format(printf, 1, 2)))void 
 panic(const char *format, ...);

#endif // PANIC_H