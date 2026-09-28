#ifndef SIMULATION_LAYER_GRASSAGE_H
#define SIMULATION_LAYER_GRASSAGE_H

#include "simulation_layer_fertility.h"
#include "world.h"

/* Grass height in millimetres. 0 is bare. */
#define GRASS_HEIGHT_MAX 255

/* Age in days of this layer's clock. 0 is a dead cell. */
#define GRASS_AGE_MAX 255

/*
 * Soil fertility spent to grow from height 0 to GRASS_HEIGHT_MAX.
 * Growth, when it runs, subtracts this budget in proportion to
 * millimetres gained. Death of old age returns twice as much:
 * the soil N plus the mass the plant built from photosynthesis.
 */
#define GRASS_SOIL_FERTILITY_TO_MAX \
    ((int32_t)FERTILITY_UPTAKE_PER_MM * (int32_t)GRASS_HEIGHT_MAX)
#define GRASS_DEATH_FERTILITY_RETURN (2 * GRASS_SOIL_FERTILITY_TO_MAX)

/*
 * One grass-age wake. Living cells (published grass height > 0)
 * gain one day per layer period. Height 0 keeps age at 0. A cell
 * that reaches GRASS_AGE_MAX dies: height goes to 0, age to 0,
 * and fertility receives GRASS_DEATH_FERTILITY_RETURN.
 */
void simulate_grassage(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

#endif
