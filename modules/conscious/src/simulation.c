#include "simulation.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

    world_state_t *world;
    world_tick_t world_tick;
    bool stop_armed;
    world_tick_t stop_tick;
    bool step_delay_set;
    uint64_t step_delay_us;

    bool terminate_requested;
    bool thread_started;
};

static void simulate_heightmap(
    world_layer_t *layer,
    world_tick_t tick)
{
    (void)layer;
    (void)tick;
}

/*
 * Fraction of diffuse daylight at an absolute world tick.
 * Tick 0 is 00:00. The curve is 0 at night, and a half sine
 * from 06:00 through 12:00 to 18:00.
 */
static double day_12h_night_12h(world_tick_t tick)
{
    const world_tick_t milliseconds_per_day = 86400000ULL;
    const world_tick_t dawn = 21600000ULL;
    const world_tick_t dusk = 64800000ULL;
    const world_tick_t daylight = 43200000ULL;
    world_tick_t time_of_day;
    double angle;

    time_of_day = tick % milliseconds_per_day;

    if (time_of_day <= dawn || time_of_day >= dusk) {
        return 0.0;
    }

    angle = 3.14159265358979323846 *
        (double)(time_of_day - dawn) /
        (double)daylight;

    return sin(angle);
}

static void simulate_difflight(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_difflight_payload_t *light;
    double fraction;
    double scaled;
    uint32_t irradiance;
    uint64_t cell_count;
    uint64_t i;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    light = layer->payload;

    if (light->published == NULL || light->cell_size == 0) {
        return;
    }

    if (world->width % light->cell_size != 0 ||
        world->depth % light->cell_size != 0) {
        return;
    }

    cell_count =
        (uint64_t)(world->width / light->cell_size) *
        (uint64_t)(world->depth / light->cell_size);

    fraction = day_12h_night_12h(tick);
    scaled = fraction * (double)light->max_irradiance;

    if (scaled <= 0.0) {
        irradiance = 0;
    } else if (scaled >= (double)UINT32_MAX) {
        irradiance = UINT32_MAX;
    } else {
        irradiance = (uint32_t)llround(scaled);
    }

    if (light->pending == NULL) {
        return;
    }

    /* Fill pending. published stays as it was at the start of the tick. */
    for (i = 0; i < cell_count; i++) {
        light->pending[i] = irradiance;
    }
}

/*
 * Both grids of a layer. pending is the buffer the tick fills.
 * published is the one already visible.
 */
static void layer_grids(
    world_layer_t *layer,
    uint32_t **published,
    uint32_t **pending,
    uint32_t *cell_size)
{
    *published = NULL;
    *pending = NULL;
    *cell_size = 0;

    if (layer == NULL || layer->payload == NULL) {
        return;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            world_heightmap_payload_t *heightmap = layer->payload;

            *published = heightmap->published;
            *pending = heightmap->pending;
            *cell_size = heightmap->cell_size;
            break;
        }

        case WORLD_LAYER_STATICWATER: {
            world_staticwater_payload_t *water = layer->payload;

            *published = water->published;
            *pending = water->pending;
            *cell_size = water->cell_size;
            break;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            world_difflight_payload_t *light = layer->payload;

            *published = light->published;
            *pending = light->pending;
            *cell_size = light->cell_size;
            break;
        }
    }
}

/* Keep pending equal to published when a step changes only some cells. */
static void mirror_published(const world_state_t *world, world_layer_t *layer)
{
    uint32_t *published;
    uint32_t *pending;
    uint32_t cell_size;
    uint64_t cell_count;

    layer_grids(layer, &published, &pending, &cell_size);

    if (published == NULL ||
        pending == NULL ||
        cell_size == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return;
    }

    cell_count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);
    memcpy(pending, published, (size_t)cell_count * sizeof(uint32_t));
}

/* Make the tick's grid visible. Called after every due layer has read. */
static void publish_layer(world_layer_t *layer)
{
    uint32_t *published;
    uint32_t *pending;
    uint32_t cell_size;
    uint32_t *previous;

    layer_grids(layer, &published, &pending, &cell_size);
    (void)cell_size;

    if (published == NULL || pending == NULL) {
        return;
    }

    previous = published;

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            world_heightmap_payload_t *heightmap = layer->payload;

            heightmap->published = heightmap->pending;
            heightmap->pending = previous;
            break;
        }

        case WORLD_LAYER_STATICWATER: {
            world_staticwater_payload_t *water = layer->payload;

            water->published = water->pending;
            water->pending = previous;
            break;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            world_difflight_payload_t *light = layer->payload;

            light->published = light->pending;
            light->pending = previous;
            break;
        }
    }
}

static void simulate_layer(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP:
            simulate_heightmap(layer, tick);
            break;

        case WORLD_LAYER_STATICWATER:
            break;

        case WORLD_LAYER_DIFFLIGHT:
            simulate_difflight(world, layer, tick);
            break;
    }

    layer->last_simulation_tick = tick;
}

/*
 * A layer is due from its clock alone. Static water never is, so it
 * stays put even if the file carried a divisor. The stored grid is
 * still readable on ticks when this returns false.
 */
static bool layer_is_due(
    const world_layer_t *layer,
    world_tick_t tick)
{
    world_tick_t period;

    if (layer->type == WORLD_LAYER_STATICWATER) {
        return false;
    }

    if (layer->clock.mode != WORLD_CLOCK_DIVISOR) {
        return false;
    }

    if (layer->clock.exponent >= 64) {
        return false;
    }

    period = (world_tick_t)1 << layer->clock.exponent;

    return (tick & (period - 1)) == 0;
}

static void simulate_step(simulation_t *simulation)
{
    world_state_t *world;
    world_tick_t tick;
    uint32_t i;
    struct timespec duration;
    uint64_t seconds;
    uint64_t remainder_us;

    world = simulation->world;
    tick = simulation->world_tick;

    if (world != NULL && world->layers != NULL) {
        for (i = 0; i < world->layer_count; i++) {
            world_layer_t *layer = world->layers[i];

            if (layer == NULL || !layer_is_due(layer, tick)) {
                continue;
            }

            mirror_published(world, layer);
            simulate_layer(world, layer, tick);
        }

        /*
         * Publish only once every due layer has read the grids
         * from the start of this tick. Array order does not decide
         * who sees a new value.
         */
        for (i = 0; i < world->layer_count; i++) {
            world_layer_t *layer = world->layers[i];

            if (layer == NULL || !layer_is_due(layer, tick)) {
                continue;
            }

            publish_layer(layer);
        }
    }

    /*
     * Optional wall-clock pause. Absent from the configuration,
     * the step does not wait.
     */
    if (!simulation->step_delay_set) {
        return;
    }

    seconds = simulation->step_delay_us / 1000000ULL;
    remainder_us = simulation->step_delay_us % 1000000ULL;
    duration.tv_sec = (time_t)seconds;
    duration.tv_nsec = (long)(remainder_us * 1000ULL);
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

        simulate_step(simulation);

        /* 
         * Here, either:
         *   simulate the beings or
         *   wait until they notify their simulation is finished or even better
         *   include their simulation inside simulate_step()
         * The definitive implementation depends on the architecture
         * of the beings.
         */

        pthread_mutex_lock(&simulation->mutex);

        /* the tick is considered done AFTER the simulation is done */
        simulation->world_tick++;

        if (simulation->stop_armed &&
            simulation->world_tick >= simulation->stop_tick) {
            simulation->requested_state = SIMULATION_PAUSED;
            simulation->stop_armed = false;
        }
    }
}

int simulation_init(
    simulation_t **simulation,
    world_state_t *world,
    bool step_delay_set,
    uint64_t step_delay_us)
{
    simulation_t *new_simulation;

    if (world == NULL) {
        return -1;
    }

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

    new_simulation->world = world;
    new_simulation->world_tick = world->age;
    new_simulation->step_delay_set = step_delay_set;
    new_simulation->step_delay_us = step_delay_us;

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
    simulation->stop_armed = false;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    return 0;
}

int simulation_resume(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    simulation->stop_armed = false;
    simulation->requested_state = SIMULATION_RUNNING;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    return 0;
}

int simulation_resume_for(
    simulation_t *simulation,
    world_tick_t steps)
{
    pthread_mutex_lock(&simulation->mutex);

    if (steps > UINT64_MAX - simulation->world_tick) {
        pthread_mutex_unlock(&simulation->mutex);
        return -1;
    }

    simulation->stop_tick = simulation->world_tick + steps;
    simulation->stop_armed = true;
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