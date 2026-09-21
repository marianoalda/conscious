#include "world.h"
#include "world_internal.h"

#include <stdio.h>

world_error_t world_v0_load(
    FILE *file,
    world_state_t *world)
{
    (void)file;
    (void)world;

    return WORLD_OK;
}