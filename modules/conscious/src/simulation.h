#ifndef SIMULATION_H
#define SIMULATION_H

#include "world.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct simulation simulation_t;

/*
 * step_delay_set is false when the configuration omits step_delay_us.
 * Then each tick runs without the artificial pause.
 */
int simulation_init(
    simulation_t **simulation,
    world_state_t *world,
    bool step_delay_set,
    uint64_t step_delay_us);

/* Start the thread. The world stays paused until resume. */
int simulation_start(simulation_t *simulation);

/* Pause and cancel a pending incremental stop. */
int simulation_pause(simulation_t *simulation);

/* Run until pause or shutdown. Clears an incremental stop. */
int simulation_resume(simulation_t *simulation);

/* Run for steps milliseconds of world time, then pause. */
int simulation_resume_for(
    simulation_t *simulation,
    world_tick_t steps);

int simulation_wait_until_paused(simulation_t *simulation);

/* Age the running world has reached, which may be ahead of world->age. */
world_tick_t simulation_get_world_tick(simulation_t *simulation);

void simulation_destroy(simulation_t *simulation);

#endif