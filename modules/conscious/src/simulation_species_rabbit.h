#ifndef SIMULATION_SPECIES_RABBIT_H
#define SIMULATION_SPECIES_RABBIT_H

#include "world.h"

/*
 * One wake of a SPECIES_RABBIT_FUNCTIONAL individual layer.
 *
 * Provisional: each calendar day (86400000 ms) since
 * last_simulation_tick moves every rabbit 1 m along its orientation
 * (0 = east, CCW positive). Only to verify that rabbits move in the
 * world; replace with real behaviour later.
 */
void simulate_species_rabbit(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

#endif
