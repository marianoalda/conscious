#ifndef SIMULATION_H
#define SIMULATION_H

#include "world.h"

typedef struct simulation simulation_t;

int simulation_init(
    simulation_t **simulation,
    world_state_t *world);

int simulation_start(simulation_t *simulation);

int simulation_pause(simulation_t *simulation);

int simulation_resume(simulation_t *simulation);

int simulation_resume_for(
    simulation_t *simulation,
    world_tick_t steps);

int simulation_wait_until_paused(simulation_t *simulation);

world_tick_t simulation_get_world_tick(simulation_t *simulation);

void simulation_destroy(simulation_t *simulation);

#endif