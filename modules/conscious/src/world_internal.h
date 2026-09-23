#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

#include <stdio.h>

#include "world.h"

void world_initialize_defaults(
    world_state_t *world);

const world_layer_t *world_find_layer(
    const world_state_t *world,
    world_layer_type_t type);

world_error_t world_append_heightmap_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t min_height,
    uint32_t *values);

world_error_t world_v0_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v1_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v1_serialize(
    FILE *file,
    const world_state_t *world);

world_error_t world_v2_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v2_serialize(
    FILE *file,
    const world_state_t *world);

world_error_t world_v3_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v3_serialize(
    FILE *file,
    const world_state_t *world);

#endif