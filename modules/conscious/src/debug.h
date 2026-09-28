#ifndef DEBUG_H
#define DEBUG_H

#include <time.h>

/*
 * Operator debug log. Off unless debug_set(1). Lines go to stderr
 * so the status line on stdout does not erase them.
 */
void debug_set(int on);
int debug_on(void);

/* One line prefixed with seconds since debug was turned on. */
void debug_log(const char *fmt, ...);

void debug_clock(struct timespec *now);
double debug_ms(const struct timespec *start, const struct timespec *end);

#endif
