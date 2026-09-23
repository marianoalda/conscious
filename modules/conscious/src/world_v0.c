#include "world.h"
#include "world_internal.h"

#include <stdio.h>

/* Version 0 stores only the magic and the version, already consumed by world_load. */
world_error_t world_v0_load(
    FILE *file,
    world_state_t *world)
{
    (void)file;
    (void)world;

    return WORLD_OK;
}