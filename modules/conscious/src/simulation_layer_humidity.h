#ifndef SIMULATION_LAYER_HUMIDITY_H
#define SIMULATION_LAYER_HUMIDITY_H

#include "world.h"

/* Saturated soil. 0 is dry. */
#define HUMIDITY_SATURATED 65535u

/*
 * Share of the difference with one neighbour that moves in one
 * hour on a 100 mm cell. One CLK_0016 wake (65536 ms) then moves
 * a quarter of that difference: the largest explicit step that
 * stays stable with four neighbours.
 */
#define HUMIDITY_DIFFUSION_CELL_MM 100.0
#define HUMIDITY_DIFFUSION_PER_HOUR (0.25 * 3600000.0 / 65536.0)

/* Largest fraction of one difference an explicit step may move. */
#define HUMIDITY_DIFFUSION_STEP_LIMIT 0.25

/*
 * Millimetres of free-surface rise that, at equilibrium, spend the
 * whole humidity range. Missing height or water leaves dz at 0.
 */
#define HUMIDITY_CAPILLARY_RISE_MM 1500

/*
 * One humidity wake. Reads standing water, published daylight,
 * published grass, and the slope of terrain and standing water.
 * Writes only this layer's pending grid.
 */
void simulate_humidity(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick);

/*
 * Hourly diffusion rate on a cell of this size. The stored value
 * is a concentration, so a larger cell moves less by
 * (100 / cell_mm)^2.
 */
double humidity_diffusion_rate(uint32_t cell_mm);

/*
 * Free-surface rise toward this neighbour, in millimetres of one
 * humidity cell step: heightmap plus standing water. Missing
 * layers contribute 0.
 */
int64_t humidity_free_surface_dz(
    const world_state_t *world,
    uint32_t humidity_cell_mm,
    uint32_t east_mm,
    uint32_t north_mm,
    world_direction_t direction);

/*
 * Humidity this cell gains from one neighbour in an explicit
 * step. Positive is incoming. dz_mm is humidity_free_surface_dz
 * toward that neighbour.
 */
double humidity_edge_flow(
    int64_t humidity_here,
    int64_t humidity_neighbor,
    int64_t dz_mm,
    double coefficient);

#endif
