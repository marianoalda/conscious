#include "world.h"
#include "world_internal.h"

#include <stdio.h>

world_error_t world_v2_load(
    FILE *file,
    world_state_t *world)
{
    (void)file;
    (void)world;

    return WORLD_ERROR_INVALID_FORMAT;
}

world_error_t world_v2_serialize(
    FILE *file,
    const world_state_t *world)
{
    (void)file;
    (void)world;

    return WORLD_ERROR_INVALID_FORMAT;
}