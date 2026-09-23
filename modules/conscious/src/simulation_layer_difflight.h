#ifndef SIMULATION_LAYER_DIFFLIGHT_H
#define SIMULATION_LAYER_DIFFLIGHT_H

#include "world.h"

/*
 * One daylight wake. Writes every pending cell from the absolute
 * world age. Does not read or write another layer.
 */
void simulate_difflight(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

#endif
