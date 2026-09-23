#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

#include <stdio.h>

#include "world.h"

/* Closed world, age 0, no layers. Used before a version loader fills the rest. */
void world_initialize_defaults(
    world_state_t *world);

/* First layer of this type, or NULL. A world holds at most one of each type. */
const world_layer_t *world_find_layer(
    const world_state_t *world,
    world_layer_type_t type);

/*
 * On success the layer owns published and a pending grid of the same size.
 * On failure the caller still owns published.
 */
world_error_t world_append_heightmap_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t min_height,
    uint32_t *published);

world_error_t world_append_staticwater_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t min_depth,
    uint32_t *published);

world_error_t world_append_difflight_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint32_t max_irradiance,
    uint32_t *published);

/* Version loaders. v0 has no payload. v1 and v2 append one heightmap. v3 appends every layer in the file. */
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