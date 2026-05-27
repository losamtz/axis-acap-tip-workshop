#pragma once

#include <stdarg.h>

// Function declaration for panic
__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) void panic(const char* format, ...);