#ifndef SIMULATION_LAYER_FERTILITY_H
#define SIMULATION_LAYER_FERTILITY_H

#include "world.h"

#include <stdint.h>

/*
 * Fraction of the stored cell that moves with the humidity
 * flux. The rest stays in the cell (organic matter, bound
 * biome). 0.5 is half dissolved, half static.
 */
#define FERTILITY_HUMIDITY_DRAG 0.5

/* 0 is sterile. 255 is the maximum on disk. */
#define FERTILITY_MAX 255

/*
 * Fertility units spent when grass grows one millimetre, and
 * returned when that millimetre dies. Death returns more than
 * growth spent, because the plant built mass from air, water,
 * and light.
 */
#define FERTILITY_UPTAKE_PER_MM 1
#define FERTILITY_RETURN_PER_MM 2

/*
 * One fertility wake. Folds grass deltas, loses a little under
 * standing flood, and advects the mobile fraction with the
 * published humidity flux. Writes only this layer's pending grid.
 */
void simulate_fertility(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

/*
 * Grass pushes a signed delta at this world point. Negative is
 * uptake. Positive is death. The value sits until the next
 * fertility wake folds it. A missing fertility layer is a no-op.
 * Birth and growth subtract FERTILITY_UPTAKE_PER_MM per millimetre.
 * Old-age death adds twice the fertility that would grow the height
 * that dies: half on that cell, half split among eight neighbours.
 */
void fertility_add_grass_delta(
    const world_state_t *world,
    uint32_t east_mm,
    uint32_t north_mm,
    int32_t delta);

void fertility_add_grass_delta_at(
    const world_state_t *world,
    uint32_t index,
    int32_t delta);

/*
 * Published fertility plus unposted grass deltas at this point,
 * not yet clamped. Missing fertility is 0.
 */
int32_t fertility_available(
    const world_state_t *world,
    uint32_t east_mm,
    uint32_t north_mm);

int32_t fertility_available_at(
    const world_state_t *world,
    uint32_t index);

#endif
