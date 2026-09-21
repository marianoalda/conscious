#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

#include <stdio.h>

#include "world.h"

world_error_t world_v0_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v1_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v1_serialize(
    FILE *file,
    const world_state_t *world);

#endif