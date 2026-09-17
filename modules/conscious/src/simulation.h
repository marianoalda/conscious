#ifndef SIMULATION_H
#define SIMULATION_H

typedef struct simulation simulation_t;

int simulation_init(simulation_t **simulation);
int simulation_start(simulation_t *simulation);

int simulation_pause(simulation_t *simulation);
int simulation_resume(simulation_t *simulation);

int simulation_wait_until_paused(simulation_t *simulation);

long long simulation_get_step_count(simulation_t *simulation);

void simulation_destroy(simulation_t *simulation);

#endif