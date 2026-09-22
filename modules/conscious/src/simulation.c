#include "simulation.h"
#include "world.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

typedef enum {
    SIMULATION_PAUSED,
    SIMULATION_RUNNING
} simulation_state_t;

struct simulation {
    pthread_t thread;

    pthread_mutex_t mutex;
    pthread_cond_t condition;

    simulation_state_t requested_state;
    simulation_state_t actual_state;

    world_tick_t world_tick;

    bool terminate_requested;
    bool thread_started;
};

static void simulate_step(void)
{
    struct timespec duration;

    duration.tv_sec = 0;
    duration.tv_nsec = 1000000L; /* 1 ms */

    nanosleep(&duration, NULL);
}

static void *simulation_run(void *arg)
{
    simulation_t *simulation = arg;

    pthread_mutex_lock(&simulation->mutex);

    for (;;) {
        while (simulation->requested_state == SIMULATION_PAUSED &&
               !simulation->terminate_requested) {

            simulation->actual_state = SIMULATION_PAUSED;

            pthread_cond_broadcast(&simulation->condition);

            pthread_cond_wait(
                &simulation->condition,
                &simulation->mutex);
        }

        if (simulation->terminate_requested) {
            pthread_mutex_unlock(&simulation->mutex);
            return NULL;
        }

        simulation->actual_state = SIMULATION_RUNNING;

        pthread_cond_broadcast(&simulation->condition);

        pthread_mutex_unlock(&simulation->mutex);

        simulate_step();

        /* Here, either
         *   simulate the beings or
         *   wait until they notify their simulation is finished or
         *   include their simulation inside simulate_step()
         * The definitive implementation depends on the architecture
         * of the beings.
         */

        pthread_mutex_lock(&simulation->mutex);

        /* the tick is considered done AFTER the simulation is done */
        simulation->world_tick++;
    }
}

int simulation_init(
    simulation_t **simulation,
    world_tick_t initial_tick)
{
    simulation_t *new_simulation;

    new_simulation = calloc(1, sizeof(*new_simulation));
    if (new_simulation == NULL) {
        return -1;
    }

    if (pthread_mutex_init(&new_simulation->mutex, NULL) != 0) {
        free(new_simulation);
        return -1;
    }

    if (pthread_cond_init(&new_simulation->condition, NULL) != 0) {
        pthread_mutex_destroy(&new_simulation->mutex);
        free(new_simulation);
        return -1;
    }

    new_simulation->requested_state = SIMULATION_PAUSED;
    new_simulation->actual_state = SIMULATION_PAUSED;
    
    new_simulation->world_tick = initial_tick;

    *simulation = new_simulation;

    return 0;
}

int simulation_start(simulation_t *simulation)
{
    int result;

    result = pthread_create(
        &simulation->thread,
        NULL,
        simulation_run,
        simulation);

    if (result != 0) {
        return -1;
    }

    simulation->thread_started = true;

    return 0;
}

int simulation_pause(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    simulation->requested_state = SIMULATION_PAUSED;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    return 0;
}

int simulation_resume(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    simulation->requested_state = SIMULATION_RUNNING;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    return 0;
}

int simulation_wait_until_paused(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    while (simulation->actual_state != SIMULATION_PAUSED) {
        pthread_cond_wait(
            &simulation->condition,
            &simulation->mutex);
    }

    pthread_mutex_unlock(&simulation->mutex);

    return 0;
}

world_tick_t simulation_get_world_tick(simulation_t *simulation)
{
    world_tick_t world_tick;

    pthread_mutex_lock(&simulation->mutex);
    world_tick = simulation->world_tick;
    pthread_mutex_unlock(&simulation->mutex);

    return world_tick;
}

void simulation_destroy(simulation_t *simulation)
{
    if (simulation == NULL) {
        return;
    }

    if (simulation->thread_started) {
        pthread_mutex_lock(&simulation->mutex);

        simulation->terminate_requested = true;

        pthread_cond_broadcast(&simulation->condition);

        pthread_mutex_unlock(&simulation->mutex);

        pthread_join(simulation->thread, NULL);
    }

    pthread_cond_destroy(&simulation->condition);
    pthread_mutex_destroy(&simulation->mutex);

    free(simulation);
}