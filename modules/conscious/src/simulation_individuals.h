#ifndef SIMULATION_INDIVIDUALS_H
#define SIMULATION_INDIVIDUALS_H

#include "simulation.h"

#include <stdint.h>

typedef struct simulation_individual simulation_individual_t;

/*
 * Ask this being's thread to exit (death / vanish). The grid thread
 * reaps finished disappearances: joins the thread and removes the
 * record from the species layer payload.
 */
void simulation_individual_request_disappear(
    simulation_individual_t *individual);

/*
 * Find a running individual thread by species field and id, or NULL.
 */
simulation_individual_t *simulation_individual_find(
    simulation_t *simulation,
    const char *species,
    uint32_t id);

/* Join dead/disappeared beings and free their records and slots. */
void simulation_individuals_reap(simulation_t *simulation);

/* Threads still running (not yet reaped). */
size_t simulation_individuals_live_count(const simulation_t *simulation);

#endif
