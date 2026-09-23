#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    SIMULATE,
    HOLD
} state_on_start_t;

typedef struct {
    bool save_state_on_shutdown;
    state_on_start_t state_on_start;
    uint64_t incremental_steps;
    char world_file[PATH_MAX];

} configuration_t;

#endif