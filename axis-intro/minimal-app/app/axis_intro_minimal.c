/*
 * Minimal ACAP application for explaining package structure.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void signal_handler(int signal_number) {
    (void)signal_number;
    running = 0;
}

int main(void) {
    openlog("axis_intro_minimal", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Axis intro minimal app started");

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    while (running)
        sleep(1);

    syslog(LOG_INFO, "Axis intro minimal app stopped");
    closelog();
    return EXIT_SUCCESS;
}
