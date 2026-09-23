#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    SIMULATE,   /* the engine runs as soon as the world is loaded */
    HOLD        /* the engine waits for the operator */
} state_on_start_t;

typedef struct {
    bool save_state_on_shutdown;    /* write the world back on exit */
    state_on_start_t state_on_start;
    uint64_t incremental_steps;     /* world milliseconds advanced by key i */
    bool step_delay_set;            /* false when step_delay_us is omitted */
    uint64_t step_delay_us;         /* artificial pause after each tick */
    char world_file[PATH_MAX];      /* path relative to the executable, or absolute */

} configuration_t;

#endif