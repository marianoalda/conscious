#ifndef SIMULATION_LAYER_HUMIDITY_H
#define SIMULATION_LAYER_HUMIDITY_H

#include "world.h"

/*
 * One humidity wake. Reads standing water, published daylight, and
 * published grass. Writes only this layer's pending grid.
 */
void simulate_humidity(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

#endif
