#ifndef WORLD_SPECIES_RABBIT_H
#define WORLD_SPECIES_RABBIT_H

#include "world.h"

#include <stdio.h>

/*
 * Static rabbit species for TYPE_INDIVIDUAL layers.
 * Analogous to simulation_layer_*.h: species-specific constants and I/O,
 * while world_v4.c owns the generic individual-layer frame.
 */

#define WORLD_SPECIES_RABBIT_FUNCTIONAL "SPECIES_RABBIT_FUNCTIONAL"
#define WORLD_SPECIES_VERSION_RABBIT_FUNCTIONAL_V1 1u

/* Short tag for individual thread names: "cons RAB <id>". */
#define WORLD_SPECIES_RABBIT_THREAD_TAG "RAB"

/* Pose-only record size for FUNCTIONAL species_version 1. */
#define WORLD_SPECIES_RABBIT_FUNCTIONAL_V1_RECORD_BYTES 24

bool world_species_rabbit_is(const char *species_field);

world_error_t world_species_rabbit_validate(
    const char *species,
    world_storage_depth_t depth,
    uint32_t species_version);

/*
 * Read count individuals (_IND + id + x,y,z + orientation).
 * On success *out owns the array (or NULL if count is 0).
 */
world_error_t world_species_rabbit_read_records(
    FILE *file,
    uint32_t count,
    world_individual_t **out);

world_error_t world_species_rabbit_write_records(
    FILE *file,
    uint32_t count,
    const world_individual_t *individuals);

#endif
