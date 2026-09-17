#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    SIMULATE,
    HOLD
} state_on_start_t;

typedef struct {
    bool save_state_on_shutdown;
    state_on_start_t state_on_start;
} configuration_t;

int parse_configuration(FILE *config_file, configuration_t *configuration);

#endif