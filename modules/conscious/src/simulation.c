#include "simulation.h"
#include "world.h"
#include "world_internal.h"

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

/* Saturated soil. 0 is dry. */
#define HUMIDITY_SATURATED 65535u

/*
 * Share of the difference with one neighbour that moves in one
 * hour on a 100 mm cell. One CLK_0016 wake (65536 ms) then moves
 * a quarter of that difference: the largest explicit step that
 * stays stable with four neighbours. A larger cell moves less,
 * by (100 / cell_mm)^2, because the stored value is a concentration.
 * At full sun the decay length is about 1.2 m.
 */
#define HUMIDITY_DIFFUSION_CELL_MM 100.0
#define HUMIDITY_DIFFUSION_PER_HOUR (0.25 * 3600000.0 / 65536.0)

/* Largest fraction of one difference an explicit step may move. */
#define HUMIDITY_DIFFUSION_STEP_LIMIT 0.25

/*
 * Fraction of the humidity still in the cell removed per hour
 * at full irradiance. Not a fraction of saturation.
 */
#define HUMIDITY_EVAPORATION_PER_HOUR 0.1

static bool grid_cell_at(
    uint32_t world_width,
    uint32_t world_depth,
    uint32_t cell_size,
    uint32_t east_mm,
    uint32_t north_mm,
    uint32_t *index)
{
    uint32_t columns;
    uint32_t rows;
    uint32_t column;
    uint32_t row;

    if (cell_size == 0 ||
        east_mm >= world_width ||
        north_mm >= world_depth ||
        world_width % cell_size != 0 ||
        world_depth % cell_size != 0) {
        return false;
    }

    columns = world_width / cell_size;
    rows = world_depth / cell_size;
    column = east_mm / cell_size;
    row = north_mm / cell_size;

    if (column >= columns || row >= rows) {
        return false;
    }

    *index = row * columns + column;
    return true;
}

/*
 * Orthogonal neighbour. MODULAR wraps to the opposite edge.
 * CLOSED has no neighbour past the border.
 */
static bool humidity_neighbor(
    const world_state_t *world,
    uint32_t columns,
    uint32_t rows,
    uint32_t column,
    uint32_t row,
    int delta_column,
    int delta_row,
    uint32_t *index)
{
    int64_t next_column;
    int64_t next_row;

    next_column = (int64_t)column + delta_column;
    next_row = (int64_t)row + delta_row;

    if (world->modularity == WORLD_MODULARITY_MODULAR) {
        next_column %= (int64_t)columns;

        if (next_column < 0) {
            next_column += (int64_t)columns;
        }

        next_row %= (int64_t)rows;

        if (next_row < 0) {
            next_row += (int64_t)rows;
        }
    } else if (next_column < 0 ||
               next_row < 0 ||
               (uint32_t)next_column >= columns ||
               (uint32_t)next_row >= rows) {
        return false;
    }

    *index = (uint32_t)next_row * columns + (uint32_t)next_column;
    return true;
}

/*
 * Cells with standing water become saturated. Depth 0 is dry.
 * The water layer is not reduced.
 */
static void humidity_saturate_standing_water(
    const world_state_t *world,
    world_u16_payload_t *humidity)
{
    const world_layer_t *layer;
    const world_staticwater_payload_t *water;
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;

    layer = world_find_layer(world, WORLD_LAYER_STATICWATER);

    if (layer == NULL || layer->payload == NULL) {
        return;
    }

    water = layer->payload;

    if (water->published == NULL || water->cell_size == 0) {
        return;
    }

    columns = world->width / humidity->cell_size;
    rows = world->depth / humidity->cell_size;

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t east_mm;
            uint32_t north_mm;
            uint32_t water_index;
            uint64_t depth;

            east_mm =
                column * humidity->cell_size +
                humidity->cell_size / 2;
            north_mm =
                row * humidity->cell_size +
                humidity->cell_size / 2;

            if (!grid_cell_at(
                    world->width,
                    world->depth,
                    water->cell_size,
                    east_mm,
                    north_mm,
                    &water_index)) {
                continue;
            }

            depth =
                (uint64_t)water->min_depth +
                water->published[water_index];

            if (depth > 0) {
                humidity->pending[row * columns + column] =
                    HUMIDITY_SATURATED;
            }
        }
    }
}

/* Depth > 0 at the centre of this humidity cell. */
static bool humidity_covers_water(
    const world_staticwater_payload_t *water,
    const world_state_t *world,
    const world_u16_payload_t *humidity,
    uint32_t column,
    uint32_t row)
{
    uint32_t east_mm;
    uint32_t north_mm;
    uint32_t water_index;
    uint64_t depth;

    if (water == NULL ||
        water->published == NULL ||
        water->cell_size == 0) {
        return false;
    }

    east_mm =
        column * humidity->cell_size +
        humidity->cell_size / 2;
    north_mm =
        row * humidity->cell_size +
        humidity->cell_size / 2;

    if (!grid_cell_at(
            world->width,
            world->depth,
            water->cell_size,
            east_mm,
            north_mm,
            &water_index)) {
        return false;
    }

    depth =
        (uint64_t)water->min_depth +
        water->published[water_index];

    return depth > 0;
}

/*
 * One explicit step. Flow across an edge is coefficient times the
 * difference. Soil exchange sums to zero. Standing water stays at
 * saturation and does not lose what it gives. A missing CLOSED
 * edge carries no flow. MODULAR wraps inside humidity_neighbor.
 */
static void humidity_diffuse_step(
    const world_state_t *world,
    world_u16_payload_t *humidity,
    const uint8_t *wet,
    uint16_t *source,
    double *delta,
    uint32_t columns,
    uint32_t rows,
    double coefficient)
{
    static const int deltas[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    uint64_t cell_count;
    uint32_t row;
    uint32_t column;

    cell_count = (uint64_t)columns * (uint64_t)rows;
    memcpy(
        source,
        humidity->pending,
        (size_t)cell_count * sizeof(*source));

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;
            double sum;
            int neighbor;

            index = row * columns + column;

            if (wet[index]) {
                continue;
            }

            sum = 0.0;

            for (neighbor = 0; neighbor < 4; neighbor++) {
                uint32_t neighbor_index;
                uint16_t neighbor_value;

                if (!humidity_neighbor(
                        world,
                        columns,
                        rows,
                        column,
                        row,
                        deltas[neighbor][0],
                        deltas[neighbor][1],
                        &neighbor_index)) {
                    continue;
                }

                if (wet[neighbor_index]) {
                    neighbor_value = HUMIDITY_SATURATED;
                } else {
                    neighbor_value = source[neighbor_index];
                }

                sum +=
                    (double)neighbor_value - (double)source[index];
            }

            delta[index] = coefficient * sum;
        }
    }

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;
            long long value;

            index = row * columns + column;

            if (wet[index]) {
                humidity->pending[index] = HUMIDITY_SATURATED;
                continue;
            }

            value = llround((double)source[index] + delta[index]);

            if (value < 0) {
                value = 0;
            } else if (value > (long long)HUMIDITY_SATURATED) {
                value = HUMIDITY_SATURATED;
            }

            humidity->pending[index] = (uint16_t)value;
        }
    }
}

/*
 * Published irradiance at this point, as a fraction of the layer
 * maximum. Missing light, or a maximum of 0, evaporates nothing.
 */
static double humidity_radiation_fraction(
    const world_state_t *world,
    const world_difflight_payload_t *light,
    uint32_t east_mm,
    uint32_t north_mm)
{
    uint32_t index;
    uint32_t irradiance;

    if (light == NULL ||
        light->published == NULL ||
        light->max_irradiance == 0 ||
        light->cell_size == 0) {
        return 0.0;
    }

    if (!grid_cell_at(
            world->width,
            world->depth,
            light->cell_size,
            east_mm,
            north_mm,
            &index)) {
        return 0.0;
    }

    irradiance = light->published[index];

    if (irradiance >= light->max_irradiance) {
        return 1.0;
    }

    return (double)irradiance / (double)light->max_irradiance;
}

/*
 * Provisional. Subtract 10 % of the humidity still in the cell
 * per hour at full sun. hours is world time since the previous
 * humidity tick, so the clock divisor only chooses how often this
 * runs. The loss is not a fraction of the cell area.
 *
 * Pending: the grass divisor. When grass has a simulation, its
 * height will scale this loss. It is not applied yet, and this
 * function does not read the grass layer.
 */
static void humidity_evaporate(
    const world_state_t *world,
    world_u16_payload_t *humidity,
    const world_difflight_payload_t *light,
    double hours)
{
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;

    if (hours <= 0.0 ||
        light == NULL ||
        light->max_irradiance == 0) {
        return;
    }

    columns = world->width / humidity->cell_size;
    rows = world->depth / humidity->cell_size;

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t east_mm;
            uint32_t north_mm;
            uint32_t index;
            double fraction;
            double loss;
            long long rounded;
            uint32_t value;

            east_mm =
                column * humidity->cell_size +
                humidity->cell_size / 2;
            north_mm =
                row * humidity->cell_size +
                humidity->cell_size / 2;
            fraction = humidity_radiation_fraction(
                world,
                light,
                east_mm,
                north_mm);
            index = row * columns + column;
            value = humidity->pending[index];
            loss =
                HUMIDITY_EVAPORATION_PER_HOUR *
                (double)value *
                fraction *
                hours;

            if (loss <= 0.0) {
                continue;
            }

            rounded = llround(loss);

            if (rounded <= 0) {
                continue;
            }

            if ((uint64_t)rounded >= value) {
                humidity->pending[index] = 0;
            } else {
                humidity->pending[index] =
                    (uint16_t)(value - (uint32_t)rounded);
            }
        }
    }
}

/*
 * Provisional humidity step. It reads standing water and published
 * daylight, and writes no other layer. See doc/layers/humidity.md.
 *
 * Pending: the grass divisor. When grass has a simulation, shade
 * will scale evaporation. This function does not read grass yet.
 */
static void simulate_humidity(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_u16_payload_t *humidity;
    const world_layer_t *light_layer;
    const world_layer_t *water_layer;
    const world_difflight_payload_t *light;
    const world_staticwater_payload_t *water;
    uint16_t *source;
    double *delta;
    uint8_t *wet;
    uint32_t columns;
    uint32_t rows;
    uint64_t cell_count;
    uint32_t row;
    uint32_t column;
    double hours;
    double rate;
    double left;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    humidity = layer->payload;

    if (humidity->pending == NULL ||
        humidity->cell_size == 0 ||
        world->width % humidity->cell_size != 0 ||
        world->depth % humidity->cell_size != 0) {
        return;
    }

    /*
     * No other layer deposits into humidity, so nothing is folded
     * before these steps. pending already matches published.
     * hours is computed first so diffusion and evaporation share it.
     */
    if (tick > layer->last_simulation_tick) {
        hours =
            (double)(tick - layer->last_simulation_tick) /
            3600000.0;
    } else {
        hours = 0.0;
    }

    humidity_saturate_standing_water(world, humidity);

    if (hours <= 0.0) {
        return;
    }

    columns = world->width / humidity->cell_size;
    rows = world->depth / humidity->cell_size;
    cell_count = (uint64_t)columns * (uint64_t)rows;

    if (cell_count > SIZE_MAX / sizeof(*source) ||
        cell_count > SIZE_MAX / sizeof(*delta) ||
        cell_count > SIZE_MAX / sizeof(*wet)) {
        return;
    }

    source = malloc((size_t)cell_count * sizeof(*source));
    delta = malloc((size_t)cell_count * sizeof(*delta));
    wet = malloc((size_t)cell_count * sizeof(*wet));

    if (source == NULL || delta == NULL || wet == NULL) {
        free(source);
        free(delta);
        free(wet);
        return;
    }

    water = NULL;
    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);

    if (water_layer != NULL) {
        water = water_layer->payload;
    }

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;

            index = row * columns + column;
            wet[index] = humidity_covers_water(
                water,
                world,
                humidity,
                column,
                row);
        }
    }

    light = NULL;
    light_layer = world_find_layer(world, WORLD_LAYER_DIFFLIGHT);

    if (light_layer != NULL) {
        light = light_layer->payload;
    }

    rate =
        HUMIDITY_DIFFUSION_PER_HOUR *
        (HUMIDITY_DIFFUSION_CELL_MM / (double)humidity->cell_size) *
        (HUMIDITY_DIFFUSION_CELL_MM / (double)humidity->cell_size);
    left = hours;

    /*
     * A wake longer than one stable step is split. Each piece
     * diffuses, then evaporates, then puts standing water back.
     */
    while (left > 0.0) {
        double step_hours;
        double coefficient;

        coefficient = rate * left;
        step_hours = left;

        if (coefficient > HUMIDITY_DIFFUSION_STEP_LIMIT) {
            step_hours = HUMIDITY_DIFFUSION_STEP_LIMIT / rate;
            coefficient = HUMIDITY_DIFFUSION_STEP_LIMIT;
        }

        humidity_diffuse_step(
            world,
            humidity,
            wet,
            source,
            delta,
            columns,
            rows,
            coefficient);
        humidity_evaporate(world, humidity, light, step_hours);
        humidity_saturate_standing_water(world, humidity);

        left -= step_hours;

        if (left < 1e-12) {
            break;
        }
    }

    free(source);
    free(delta);
    free(wet);
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
        case WORLD_LAYER_GRASS: {
            world_u8_payload_t *grid = layer->payload;

            *published = grid->published;
            *pending = grid->pending;
            *cell_size = grid->cell_size;
            *value_size = sizeof(uint8_t);
            break;
        }
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
static void publish_layer(world_layer_t *layer)
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
        case WORLD_LAYER_GRASS: {
            world_u8_payload_t *grid = layer->payload;

            grid->published = grid->pending;
            grid->pending = previous;
            break;
        }
    }
}

/*
 * Fertility and grass will read one another from the published
 * grids. This prototype does not change any cell.
 */
static void simulate_coupled_u8(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    (void)world;
    (void)layer;
    (void)tick;
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
        case WORLD_LAYER_GRASS:
            simulate_coupled_u8(world, layer, tick);
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