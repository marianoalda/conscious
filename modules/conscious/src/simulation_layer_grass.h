#ifndef SIMULATION_LAYER_GRASS_H
#define SIMULATION_LAYER_GRASS_H

#include "simulation_layer_grassage.h"
#include "world.h"

/* Height written on birth. */
#define GRASS_BIRTH_HEIGHT_MM 1

/* Millimetres gained in one calendar day at full sun, fertility
 * and humidity at or above half of each layer maximum. */
#define GRASS_GROWTH_MM_PER_DAY 5

/* Birth needs humidity and fertility strictly above this fraction
 * of each layer maximum. */
#define GRASS_BIRTH_FRACTION 0.1

/* A parent neighbour must be a living plant whose age is strictly
 * above this fraction of GRASS_AGE_MAX. */
#define GRASS_PARENT_AGE_FRACTION 0.25

/* Fertility and humidity do not limit growth at or above half of
 * the stored maximum. Below that the factor is value / (half max). */
#define GRASS_LIMIT_FRACTION 0.5

#define GRASS_MS_PER_DAY 86400000.0

void grass_cycle_stats(
    uint64_t *births,
    uint64_t *deaths);

void grass_total_stats(
    uint64_t *births,
    uint64_t *deaths);

/*
 * One grass wake. Empty cells may birth. Living cells at
 * GRASS_AGE_MAX die. The rest grow. Writes this layer's pending
 * height and posts fertility deltas.
 */
void simulate_grass(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

#endif
