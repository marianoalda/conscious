#include "simulation_individuals.h"
#include "simulation_internal.h"
#include "simulation_species_rabbit.h"
#include "thread_name.h"
#include "world_species_rabbit.h"
#include "debug.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct simulation_individual {
    pthread_t thread;
    bool thread_started;
    bool thread_exited;

    simulation_t *simulation;
    world_layer_t *layer;
    char species[WORLD_SPECIES_FIELD_BYTES];
    char thread_tag[8];
    uint32_t id;
    world_individual_t *record;
    world_tick_t birth_tick;
    world_tick_t last_tick;

    pthread_mutex_t mutex;
    bool disappear_requested;
    bool terminate_requested;
};

static const char *species_thread_tag(const char *species)
{
    if (world_species_rabbit_is(species)) {
        return WORLD_SPECIES_RABBIT_THREAD_TAG;
    }

    return "???";
}

static void individual_thread_name(
    const simulation_individual_t *individual,
    char *out,
    size_t out_size)
{
    uint32_t id;

    id = individual != NULL ? individual->id : 0;
    if (id > CONSCIOUS_THREAD_INDIVIDUAL_ID_MAX) {
        id = CONSCIOUS_THREAD_INDIVIDUAL_ID_MAX;
    }

    snprintf(
        out,
        out_size,
        "cons %s %" PRIu32,
        individual != NULL && individual->thread_tag[0] != '\0' ?
            individual->thread_tag :
            "???",
        id);
}

static int payload_remove_individual(
    world_individual_payload_t *payload,
    uint32_t id)
{
    uint32_t i;
    uint32_t j;

    if (payload == NULL || payload->individuals == NULL || id == 0) {
        return -1;
    }

    for (i = 0; i < payload->count; i++) {
        if (payload->individuals[i].id == id) {
            break;
        }
    }

    if (i >= payload->count) {
        return -1;
    }

    for (j = i + 1; j < payload->count; j++) {
        payload->individuals[j - 1] = payload->individuals[j];
    }

    payload->count--;

    if (payload->count == 0) {
        free(payload->individuals);
        payload->individuals = NULL;
    }

    return 0;
}

static world_individual_t *payload_find_individual(
    world_individual_payload_t *payload,
    uint32_t id)
{
    uint32_t i;

    if (payload == NULL || payload->individuals == NULL || id == 0) {
        return NULL;
    }

    for (i = 0; i < payload->count; i++) {
        if (payload->individuals[i].id == id) {
            return &payload->individuals[i];
        }
    }

    return NULL;
}

static void refresh_live_record_pointers(simulation_t *simulation)
{
    size_t i;

    if (simulation == NULL || simulation->individuals == NULL) {
        return;
    }

    for (i = 0; i < simulation->individual_count; i++) {
        simulation_individual_t *slot = &simulation->individuals[i];
        world_individual_payload_t *payload;

        if (!slot->thread_started || slot->thread_exited || slot->layer == NULL) {
            continue;
        }

        payload = slot->layer->payload;
        slot->record = payload_find_individual(payload, slot->id);
    }
}

static void *individual_run(void *arg)
{
    simulation_individual_t *individual = arg;
    simulation_t *simulation;
    char thread_name[16];
    struct timespec pause = {
        .tv_sec = 0,
        .tv_nsec = 1000000L /* 1 ms poll; unsynchronised with grid */
    };

    if (individual == NULL || individual->simulation == NULL) {
        return NULL;
    }

    simulation = individual->simulation;
    individual_thread_name(individual, thread_name, sizeof(thread_name));
    conscious_thread_name_set(thread_name);
    debug_log("individual start %s", thread_name);

    for (;;) {
        bool stop;
        bool disappear;
        world_tick_t tick;
        world_state_t *world;
        sim_rabbit_result_t rabbit_result;

        pthread_mutex_lock(&individual->mutex);
        stop = individual->terminate_requested;
        disappear = individual->disappear_requested;
        pthread_mutex_unlock(&individual->mutex);

        if (stop || disappear) {
            break;
        }

        tick = simulation_get_world_tick(simulation);
        world = simulation->world;

        /*
         * Provisional rabbit walk and lifespan: only this individual's
         * pose. Reads world extents/modularity for wrap (static layout).
         * Does not read or write dense simulated layers.
         */
        rabbit_result = SIM_RABBIT_OK;
        if (world != NULL &&
            individual->record != NULL &&
            world_species_rabbit_is(individual->species)) {
            rabbit_result = simulate_species_rabbit_one(
                world,
                individual->record,
                individual->birth_tick,
                &individual->last_tick,
                tick);
        }

        if (rabbit_result == SIM_RABBIT_DIED) {
            pthread_mutex_lock(&individual->mutex);
            individual->disappear_requested = true;
            pthread_mutex_unlock(&individual->mutex);
            debug_log("individual died %s", thread_name);
            break;
        }

        nanosleep(&pause, NULL);
    }

    pthread_mutex_lock(&individual->mutex);
    individual->thread_exited = true;
    pthread_mutex_unlock(&individual->mutex);

    debug_log(
        "individual stop %s (%s)",
        thread_name,
        individual->disappear_requested ? "disappear" : "terminate");
    return NULL;
}

static int individual_slot_start(
    simulation_t *simulation,
    simulation_individual_t *slot,
    world_layer_t *layer,
    const char *species,
    world_individual_t *record,
    world_tick_t birth_tick)
{
    size_t species_len;

    if (simulation == NULL ||
        slot == NULL ||
        layer == NULL ||
        species == NULL ||
        record == NULL ||
        record->id == 0) {
        return -1;
    }

    memset(slot, 0, sizeof(*slot));
    species_len = strlen(species);
    if (species_len > WORLD_SPECIES_FIELD_BYTES) {
        return -1;
    }

    if (pthread_mutex_init(&slot->mutex, NULL) != 0) {
        return -1;
    }

    slot->simulation = simulation;
    slot->layer = layer;
    memcpy(slot->species, species, species_len);
    snprintf(
        slot->thread_tag,
        sizeof(slot->thread_tag),
        "%s",
        species_thread_tag(species));
    slot->id = record->id;
    slot->record = record;
    slot->birth_tick = birth_tick;
    slot->last_tick = birth_tick;

    if (pthread_create(&slot->thread, NULL, individual_run, slot) != 0) {
        pthread_mutex_destroy(&slot->mutex);
        memset(slot, 0, sizeof(*slot));
        return -1;
    }

    slot->thread_started = true;
    return 0;
}

static void individual_slot_join(simulation_individual_t *slot)
{
    if (slot == NULL) {
        return;
    }

    if (slot->thread_started) {
        pthread_mutex_lock(&slot->mutex);
        slot->terminate_requested = true;
        pthread_mutex_unlock(&slot->mutex);
        pthread_join(slot->thread, NULL);
        slot->thread_started = false;
        slot->thread_exited = true;
    }

    pthread_mutex_destroy(&slot->mutex);
    memset(slot, 0, sizeof(*slot));
}

void simulation_individual_request_disappear(
    simulation_individual_t *individual)
{
    if (individual == NULL) {
        return;
    }

    pthread_mutex_lock(&individual->mutex);
    individual->disappear_requested = true;
    pthread_mutex_unlock(&individual->mutex);
}

simulation_individual_t *simulation_individual_find(
    simulation_t *simulation,
    const char *species,
    uint32_t id)
{
    size_t i;

    if (simulation == NULL || species == NULL || id == 0) {
        return NULL;
    }

    for (i = 0; i < simulation->individual_count; i++) {
        simulation_individual_t *slot = &simulation->individuals[i];

        if (!slot->thread_started) {
            continue;
        }

        if (slot->id == id &&
            strncmp(
                slot->species,
                species,
                WORLD_SPECIES_FIELD_BYTES) == 0) {
            return slot;
        }
    }

    return NULL;
}

size_t simulation_individuals_live_count(const simulation_t *simulation)
{
    size_t i;
    size_t live;

    if (simulation == NULL || simulation->individuals == NULL) {
        return 0;
    }

    live = 0;
    for (i = 0; i < simulation->individual_count; i++) {
        if (simulation->individuals[i].thread_started &&
            !simulation->individuals[i].thread_exited) {
            live++;
        }
    }

    return live;
}

void simulation_individuals_reap(simulation_t *simulation)
{
    size_t i;
    size_t live;

    if (simulation == NULL || simulation->individuals == NULL) {
        return;
    }

    for (i = 0; i < simulation->individual_count; i++) {
        simulation_individual_t *slot = &simulation->individuals[i];
        bool exited;
        bool disappear;
        uint32_t id;
        world_layer_t *layer;

        if (!slot->thread_started) {
            continue;
        }

        pthread_mutex_lock(&slot->mutex);
        exited = slot->thread_exited;
        disappear = slot->disappear_requested;
        id = slot->id;
        layer = slot->layer;
        pthread_mutex_unlock(&slot->mutex);

        if (!exited || !disappear) {
            continue;
        }

        pthread_join(slot->thread, NULL);
        slot->thread_started = false;

        if (layer != NULL && layer->payload != NULL) {
            world_individual_payload_t *payload = layer->payload;

            if (payload_remove_individual(payload, id) == 0) {
                debug_log(
                    "individual reaped id=%" PRIu32 " remaining=%" PRIu32,
                    id,
                    payload->count);
                refresh_live_record_pointers(simulation);
            }
        }

        pthread_mutex_destroy(&slot->mutex);
        memset(slot, 0, sizeof(*slot));
    }

    live = simulation_individuals_live_count(simulation);
    if (live == 0) {
        free(simulation->individuals);
        simulation->individuals = NULL;
        simulation->individual_count = 0;
        simulation->individual_capacity = 0;
        debug_log("individuals registry freed");
    }
}

void simulation_individuals_stop(simulation_t *simulation)
{
    size_t i;

    if (simulation == NULL || simulation->individuals == NULL) {
        return;
    }

    for (i = 0; i < simulation->individual_count; i++) {
        individual_slot_join(&simulation->individuals[i]);
    }

    free(simulation->individuals);
    simulation->individuals = NULL;
    simulation->individual_count = 0;
    simulation->individual_capacity = 0;
}

int simulation_individuals_start(simulation_t *simulation)
{
    world_state_t *world;
    size_t needed;
    uint32_t layer_i;
    size_t written;
    world_tick_t birth_tick;

    if (simulation == NULL || simulation->world == NULL) {
        return -1;
    }

    simulation_individuals_stop(simulation);

    world = simulation->world;
    birth_tick = simulation_get_world_tick(simulation);
    needed = 0;

    for (layer_i = 0; layer_i < world->layer_count; layer_i++) {
        const world_layer_t *layer = world->layers[layer_i];
        const world_individual_payload_t *payload;

        if (layer == NULL ||
            layer->type != WORLD_LAYER_INDIVIDUAL ||
            layer->payload == NULL) {
            continue;
        }

        payload = layer->payload;
        needed += payload->count;
    }

    if (needed == 0) {
        return 0;
    }

    simulation->individuals = calloc(needed, sizeof(*simulation->individuals));
    if (simulation->individuals == NULL) {
        return -1;
    }

    simulation->individual_capacity = needed;
    simulation->individual_count = needed;
    written = 0;

    for (layer_i = 0; layer_i < world->layer_count; layer_i++) {
        world_layer_t *layer = world->layers[layer_i];
        world_individual_payload_t *payload;
        uint32_t n;

        if (layer == NULL ||
            layer->type != WORLD_LAYER_INDIVIDUAL ||
            layer->payload == NULL) {
            continue;
        }

        payload = layer->payload;
        if (payload->individuals == NULL) {
            continue;
        }

        for (n = 0; n < payload->count; n++) {
            world_individual_t *record = &payload->individuals[n];

            if (record->id == 0) {
                continue;
            }

            if (written >= needed) {
                simulation_individuals_stop(simulation);
                return -1;
            }

            if (individual_slot_start(
                    simulation,
                    &simulation->individuals[written],
                    layer,
                    payload->species,
                    record,
                    birth_tick) != 0) {
                simulation_individuals_stop(simulation);
                return -1;
            }

            written++;
        }
    }

    simulation->individual_count = written;
    debug_log("individuals started count=%zu", written);
    return 0;
}
