#ifndef SIMULATION_SPECIES_RABBIT_H
#define SIMULATION_SPECIES_RABBIT_H

#include "world.h"

#include <stdbool.h>

#define RABBIT_MS_PER_DAY 86400000ULL
#define RABBIT_STEP_MM 1000
/* Provisional lifespan: die after this many calendar days of life. */
#define RABBIT_LIFESPAN_DAYS 3u

typedef enum {
    SIM_RABBIT_OK = 0,
    SIM_RABBIT_DIED
} sim_rabbit_result_t;

/*
 * Provisional per-individual step for SPECIES_RABBIT_FUNCTIONAL.
 * Moves 1 m per calendar day along orientation, then dies when
 * calendar days since birth_tick reach RABBIT_LIFESPAN_DAYS.
 * Runs on the individual's own thread only to verify motion and
 * death in the world — not real behaviour.
 *
 * Must not read or write dense simulated layers; may use static world
 * layout (extents, modularity) for wrapping.
 */
sim_rabbit_result_t simulate_species_rabbit_one(
    const world_state_t *world,
    world_individual_t *individual,
    world_tick_t birth_tick,
    world_tick_t *last_tick,
    world_tick_t tick);

#endif
