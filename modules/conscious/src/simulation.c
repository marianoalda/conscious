#include "simulation.h"
#include "simulation_internal.h"
#include "world.h"
#include "world_internal.h"
#include "simulation_layer_difflight.h"
#include "simulation_layer_humidity.h"
#include "simulation_layer_grass.h"
#include "simulation_layer_grassage.h"
#include "thread_name.h"
#include "debug.h"

#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void simulate_heightmap(
    world_layer_t *layer,
    world_tick_t tick)
{
    (void)layer;
    (void)tick;
}

/*
 * Both grids of a layer. pending is the buffer the tick fills.
 * published is the one already visible.
 */
static void layer_grids(
    world_layer_t *layer,
    void **published,
    void **pending,
    uint32_t *cell_size,
    size_t *value_size)
{
    *published = NULL;
    *pending = NULL;
    *cell_size = 0;
    *value_size = 0;

    if (layer == NULL || layer->payload == NULL) {
        return;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            world_heightmap_payload_t *heightmap = layer->payload;

            *published = heightmap->published;
            *pending = heightmap->pending;
            *cell_size = heightmap->cell_size;
            *value_size = sizeof(uint32_t);
            break;
        }

        case WORLD_LAYER_STATICWATER: {
            world_staticwater_payload_t *water = layer->payload;

            *published = water->published;
            *pending = water->pending;
            *cell_size = water->cell_size;
            *value_size = sizeof(uint32_t);
            break;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            world_difflight_payload_t *light = layer->payload;

            *published = light->published;
            *pending = light->pending;
            *cell_size = light->cell_size;
            *value_size = sizeof(uint32_t);
            break;
        }

        case WORLD_LAYER_HUMIDITY: {
            world_u16_payload_t *grid = layer->payload;

            *published = grid->published;
            *pending = grid->pending;
            *cell_size = grid->cell_size;
            *value_size = sizeof(uint16_t);
            break;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            world_u8_payload_t *grid = layer->payload;

            *published = grid->published;
            *pending = grid->pending;
            *cell_size = grid->cell_size;
            *value_size = sizeof(uint8_t);
            break;
        }

        case WORLD_LAYER_INDIVIDUAL:
            break;
    }
}

/* Keep pending equal to published when a step changes only some cells. */
static void mirror_published(const world_state_t *world, world_layer_t *layer)
{
    void *published;
    void *pending;
    uint32_t cell_size;
    size_t value_size;
    uint64_t cell_count;

    layer_grids(layer, &published, &pending, &cell_size, &value_size);

    if (published == NULL ||
        pending == NULL ||
        value_size == 0 ||
        cell_size == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return;
    }

    cell_count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);
    memcpy(pending, published, (size_t)cell_count * value_size);
}

/* Make the tick's grid visible. Called after every due layer has read. */
static void publish_layer(world_state_t *world, world_layer_t *layer)
{
    void *published;
    void *pending;
    uint32_t cell_size;
    size_t value_size;
    void *previous;

    layer_grids(layer, &published, &pending, &cell_size, &value_size);
    (void)cell_size;
    (void)value_size;

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

        case WORLD_LAYER_HUMIDITY: {
            world_u16_payload_t *grid = layer->payload;

            grid->published = grid->pending;
            grid->pending = previous;
            break;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            world_u8_payload_t *grid = layer->payload;

            grid->published = grid->pending;
            grid->pending = previous;
            break;
        }

        case WORLD_LAYER_INDIVIDUAL:
            break;
    }

    world_refresh_slopes(world, layer);
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

        case WORLD_LAYER_HUMIDITY:
            simulate_humidity(world, layer, tick);
            break;

        case WORLD_LAYER_FERTILITY:
            simulate_fertility(world, layer, tick);
            break;

        case WORLD_LAYER_GRASS:
            simulate_grass(world, layer, tick);
            break;

        case WORLD_LAYER_GRASSAGE:
            simulate_grassage(world, layer, tick);
            break;

        case WORLD_LAYER_INDIVIDUAL:
            /* Individuals run on their own threads. */
            break;
    }

    world_clamp_layer(world, layer);
    layer->last_simulation_tick = tick;
}

/*
 * A layer is due from its clock alone. Static water and TYPE_INDIVIDUAL
 * never wake here: water stays put; individuals have their own threads.
 */
static bool layer_is_due(
    const world_layer_t *layer,
    world_tick_t tick)
{
    world_tick_t period;

    if (layer->type == WORLD_LAYER_STATICWATER ||
        layer->type == WORLD_LAYER_INDIVIDUAL) {
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

/*
 * First tick at or after `tick` on which this layer is due.
 * CLK_NOEV, static water, and TYPE_INDIVIDUAL never return a finite tick.
 */
static world_tick_t layer_next_due_tick(
    const world_layer_t *layer,
    world_tick_t tick)
{
    world_tick_t period;
    world_tick_t aligned;

    if (layer == NULL ||
        layer->type == WORLD_LAYER_STATICWATER ||
        layer->type == WORLD_LAYER_INDIVIDUAL ||
        layer->clock.mode != WORLD_CLOCK_DIVISOR ||
        layer->clock.exponent >= 64) {
        return UINT64_MAX;
    }

    period = (world_tick_t)1 << layer->clock.exponent;

    if (period == 0) {
        return UINT64_MAX;
    }

    aligned = tick & ~(period - 1);

    if (aligned == tick) {
        return tick;
    }

    if (aligned > UINT64_MAX - period) {
        return UINT64_MAX;
    }

    return aligned + period;
}

static world_tick_t world_next_due_tick(
    const world_state_t *world,
    world_tick_t tick)
{
    world_tick_t next;
    uint32_t i;

    if (world == NULL || world->layers == NULL) {
        return UINT64_MAX;
    }

    next = UINT64_MAX;

    for (i = 0; i < world->layer_count; i++) {
        world_tick_t layer_next;

        layer_next = layer_next_due_tick(world->layers[i], tick);

        if (layer_next < next) {
            next = layer_next;
        }
    }

    return next;
}

static const char *debug_layer_tag(world_layer_type_t type)
{
    switch (type) {
        case WORLD_LAYER_HEIGHTMAP:
            return "height";

        case WORLD_LAYER_STATICWATER:
            return "water";

        case WORLD_LAYER_DIFFLIGHT:
            return "light";

        case WORLD_LAYER_HUMIDITY:
            return "humidity";

        case WORLD_LAYER_FERTILITY:
            return "fertility";

        case WORLD_LAYER_GRASS:
            return "grass";

        case WORLD_LAYER_GRASSAGE:
            return "grassage";

        case WORLD_LAYER_INDIVIDUAL:
            return "individual";

        default:
            return "?";
    }
}

static double debug_timespec_ms(
    const struct timespec *start,
    const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
}

static void debug_simulation_beat(world_tick_t tick)
{
    static int started;
    static world_tick_t last_tick;
    static struct timespec last_time;
    struct timespec now;
    double elapsed;

    if (!debug_on()) {
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!started) {
        started = 1;
        last_tick = tick;
        last_time = now;
        return;
    }

    elapsed =
        (double)(now.tv_sec - last_time.tv_sec) +
        (double)(now.tv_nsec - last_time.tv_nsec) / 1000000000.0;

    if (elapsed < 1.0) {
        return;
    }

    debug_log(
        "sim  tick=%" PRIu64 "  delta=%" PRIu64 "  wall=%.3fs  %.1f tick/s",
        tick,
        tick - last_tick,
        elapsed,
        (double)(tick - last_tick) / elapsed);

    last_tick = tick;
    last_time = now;
}

static void simulate_step(simulation_t *simulation)
{
    world_state_t *world;
    world_tick_t tick;
    uint32_t i;
    struct timespec duration;
    struct timespec step_start;
    struct timespec step_end;
    struct timespec layer_start;
    struct timespec layer_end;
    uint64_t seconds;
    uint64_t remainder_us;
    int tracing;
    char due[128];
    size_t due_len;

    world = simulation->world;
    tick = simulation->world_tick;
    tracing = debug_on();
    due[0] = '\0';
    due_len = 0;

    if (tracing) {
        clock_gettime(CLOCK_MONOTONIC, &step_start);
    }

    if (world != NULL && world->layers != NULL) {
        for (i = 0; i < world->layer_count; i++) {
            world_layer_t *layer = world->layers[i];

            if (layer == NULL || !layer_is_due(layer, tick)) {
                continue;
            }

            if (tracing && due_len + 1 < sizeof(due)) {
                int written;

                if (due_len > 0) {
                    due[due_len++] = ',';
                    due[due_len] = '\0';
                }

                written = snprintf(
                    due + due_len,
                    sizeof(due) - due_len,
                    "%s",
                    debug_layer_tag(layer->type));

                if (written > 0) {
                    due_len += (size_t)written;

                    if (due_len >= sizeof(due)) {
                        due_len = sizeof(due) - 1;
                    }
                }
            }

            if (tracing) {
                debug_clock(&layer_start);
            }

            mirror_published(world, layer);
            simulate_layer(world, layer, tick);

            if (tracing) {
                debug_clock(&layer_end);
                debug_log(
                    "prof layer %s %.1fms",
                    debug_layer_tag(layer->type),
                    debug_ms(&layer_start, &layer_end));
            }
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

            publish_layer(world, layer);
        }
    }

    if (tracing) {
        double wall_ms;

        clock_gettime(CLOCK_MONOTONIC, &step_end);
        wall_ms = debug_timespec_ms(&step_start, &step_end);

        if (wall_ms >= 10.0) {
            debug_log(
                "step tick=%" PRIu64 " wall=%.1fms due=%s",
                tick,
                wall_ms,
                due[0] != '\0' ? due : "-");
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

    conscious_thread_name_set(CONSCIOUS_THREAD_GRID);

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
        simulation_individuals_reap(simulation);

        /*
         * Individuals advance on their own threads
         * (simulation_individuals.c), unsynchronised for now.
         * Reap joins disappearances and frees their records.
         */

        pthread_mutex_lock(&simulation->mutex);

        /* the tick is considered done AFTER the simulation is done */
        simulation->world_tick++;

        /*
         * Empty milliseconds change no layer. Jump to the next due
         * tick so a long incremental run is not one loop per ms.
         * A configured step delay keeps real-time pacing instead.
         */
        if (!simulation->step_delay_set) {
            world_tick_t next;

            next = world_next_due_tick(
                simulation->world,
                simulation->world_tick);

            if (simulation->stop_armed &&
                simulation->stop_tick < next) {
                next = simulation->stop_tick;
            }

            if (next > simulation->world_tick &&
                next != UINT64_MAX) {
                simulation->world_tick = next;
            }
        }

        debug_simulation_beat(simulation->world_tick);

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

    if (simulation_individuals_start(simulation) != 0) {
        pthread_mutex_lock(&simulation->mutex);
        simulation->terminate_requested = true;
        pthread_cond_broadcast(&simulation->condition);
        pthread_mutex_unlock(&simulation->mutex);
        pthread_join(simulation->thread, NULL);
        simulation->thread_started = false;
        return -1;
    }

    return 0;
}

int simulation_pause(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    simulation->requested_state = SIMULATION_PAUSED;
    simulation->stop_armed = false;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    debug_log("sim  pause at tick=%" PRIu64, simulation_get_world_tick(simulation));

    return 0;
}

int simulation_resume(simulation_t *simulation)
{
    pthread_mutex_lock(&simulation->mutex);

    simulation->stop_armed = false;
    simulation->requested_state = SIMULATION_RUNNING;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    debug_log("sim  resume at tick=%" PRIu64, simulation_get_world_tick(simulation));

    return 0;
}

int simulation_resume_for(
    simulation_t *simulation,
    world_tick_t steps)
{
    world_tick_t until;

    pthread_mutex_lock(&simulation->mutex);

    if (steps > UINT64_MAX - simulation->world_tick) {
        pthread_mutex_unlock(&simulation->mutex);
        return -1;
    }

    until = simulation->world_tick + steps;
    simulation->stop_tick = until;
    simulation->stop_armed = true;
    simulation->requested_state = SIMULATION_RUNNING;

    pthread_cond_broadcast(&simulation->condition);

    pthread_mutex_unlock(&simulation->mutex);

    debug_log(
        "sim  resume_for steps=%" PRIu64 " until=%" PRIu64,
        steps,
        until);

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

    simulation_individuals_stop(simulation);

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