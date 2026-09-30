#ifndef SIMULATION_INTERNAL_H
#define SIMULATION_INTERNAL_H

#include "simulation.h"
#include "world.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SIMULATION_PAUSED,
    SIMULATION_RUNNING
} simulation_state_t;

typedef struct simulation_individual simulation_individual_t;

struct simulation {
    pthread_t thread;

    pthread_mutex_t mutex;
    pthread_cond_t condition;

    simulation_state_t requested_state;
    simulation_state_t actual_state;

    world_state_t *world;
    world_tick_t world_tick;
    bool stop_armed;
    world_tick_t stop_tick;
    bool step_delay_set;
    uint64_t step_delay_us;

    bool terminate_requested;
    bool thread_started;

    simulation_individual_t *individuals;
    size_t individual_count;
    size_t individual_capacity;
};

int simulation_individuals_start(simulation_t *simulation);
void simulation_individuals_stop(simulation_t *simulation);

#endif
