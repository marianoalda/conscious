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

world_error_t world_append_u16_layer(
    world_state_t *world,
    world_layer_type_t type,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint16_t *published);

world_error_t world_append_u8_layer(
    world_state_t *world,
    world_layer_type_t type,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint8_t *published);

world_error_t world_append_difflight_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint32_t max_irradiance,
    uint32_t *published);

/*
 * Rebuild the orthogonal slope buffer of a heightmap or static-water
 * layer from its published grid. No-op for other types.
 */
void world_refresh_slopes(
    const world_state_t *world,
    world_layer_t *layer);

/*
 * Clamp published and pending to the layer's stored range.
 * Daylight is 0..max_irradiance. Humidity is 0..65535.
 * Fertility, grass, and grass age are 0..255.
 * Heightmap and static water are already uint32.
 */
void world_clamp_layer(
    const world_state_t *world,
    world_layer_t *layer);

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

/* Dense-layer helpers shared with v4 (type already read / non-individual). */
world_error_t world_v3_load_one_layer(
    FILE *file,
    world_state_t *world);

world_error_t world_v3_write_one_layer(
    FILE *file,
    const world_state_t *world,
    const world_layer_t *layer);

world_error_t world_v3_write_header(
    FILE *file,
    const world_state_t *world);

world_error_t world_v3_read_header(
    FILE *file,
    world_state_t *world);

world_error_t world_v3_read_next_layer_marker(
    FILE *file,
    int *present);

world_error_t world_v3_check_staticwater_grid(
    const world_state_t *world);

world_error_t world_v3_read_clock(
    FILE *file,
    world_clock_t *clock);

world_error_t world_v3_write_clock(
    FILE *file,
    const world_clock_t *clock);

world_error_t world_v3_write_padded_string(
    FILE *file,
    const char *text,
    size_t field_size);

world_error_t world_v4_load(
    FILE *file,
    world_state_t *world);

world_error_t world_v4_serialize(
    FILE *file,
    const world_state_t *world);

world_error_t world_append_individual_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    const char *species,
    world_storage_depth_t depth,
    uint32_t species_version,
    uint32_t count,
    world_individual_t *individuals);

const world_layer_t *world_find_individual_layer(
    const world_state_t *world,
    const char *species);

#endif