#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int debug_enabled;
static struct timespec debug_origin;

void debug_set(int on)
{
    debug_enabled = on != 0;

    if (debug_enabled) {
        clock_gettime(CLOCK_MONOTONIC, &debug_origin);
    }
}

int debug_on(void)
{
    return debug_enabled;
}

void debug_log(const char *fmt, ...)
{
    va_list args;
    struct timespec now;
    double seconds;

    if (!debug_enabled || fmt == NULL) {
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    seconds =
        (double)(now.tv_sec - debug_origin.tv_sec) +
        (double)(now.tv_nsec - debug_origin.tv_nsec) / 1000000000.0;

    fprintf(stderr, "\ndebug %8.3f: ", seconds);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
    fflush(stderr);
}

void debug_clock(struct timespec *now)
{
    if (now != NULL) {
        clock_gettime(CLOCK_MONOTONIC, now);
    }
}

double debug_ms(const struct timespec *start, const struct timespec *end)
{
    if (start == NULL || end == NULL) {
        return 0.0;
    }

    return (double)(end->tv_sec - start->tv_sec) * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
}
