#include "simulation_layer_fertility.h"
#include "simulation_layer_humidity.h"
#include "world_internal.h"
#include "debug.h"

#include <math.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Fraction of saturation above which anaerobic loss begins.
 * At full saturation the cell loses 2 % of its store per day.
 * Drought does not change the store: biome death becomes
 * nutrient in the same cell.
 */
#define FERTILITY_FLOOD_START 0.85
#define FERTILITY_DENITRIFICATION_PER_DAY 0.02

static const world_direction_t fertility_directions[4] = {
    WORLD_DIR_EAST,
    WORLD_DIR_WEST,
    WORLD_DIR_NORTH,
    WORLD_DIR_SOUTH
};

static const int fertility_deltas[4][2] = {
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1}
};

static int32_t *grass_delta;
static uint64_t grass_delta_count;
static double *fertility_carry;
static uint64_t fertility_carry_count;

static void fertility_clear_buffers(void)
{
    free(grass_delta);
    grass_delta = NULL;
    grass_delta_count = 0;
    free(fertility_carry);
    fertility_carry = NULL;
    fertility_carry_count = 0;
}

static bool fertility_resize(
    uint64_t cell_count)
{
    int32_t *new_delta;
    double *new_carry;

    if (cell_count == grass_delta_count &&
        cell_count == fertility_carry_count &&
        (cell_count == 0 ||
         (grass_delta != NULL && fertility_carry != NULL))) {
        return true;
    }

    fertility_clear_buffers();

    if (cell_count == 0) {
        return true;
    }

    if (cell_count > SIZE_MAX / sizeof(*new_delta) ||
        cell_count > SIZE_MAX / sizeof(*new_carry)) {
        return false;
    }

    new_delta = calloc((size_t)cell_count, sizeof(*new_delta));
    new_carry = calloc((size_t)cell_count, sizeof(*new_carry));

    if (new_delta == NULL || new_carry == NULL) {
        free(new_delta);
        free(new_carry);
        return false;
    }

    grass_delta = new_delta;
    grass_delta_count = cell_count;
    fertility_carry = new_carry;
    fertility_carry_count = cell_count;
    return true;
}

static bool fertility_index_at(
    const world_state_t *world,
    const world_u8_payload_t *fertility,
    uint32_t east_mm,
    uint32_t north_mm,
    uint32_t *index)
{
    if (world == NULL ||
        fertility == NULL ||
        fertility->cell_size == 0 ||
        index == NULL) {
        return false;
    }

    return world_cell_at(
        world->width,
        world->depth,
        fertility->cell_size,
        east_mm,
        north_mm,
        index);
}

static const world_u8_payload_t *fertility_payload(
    const world_state_t *world)
{
    const world_layer_t *layer;

    layer = world_find_layer(world, WORLD_LAYER_FERTILITY);

    if (layer == NULL || layer->payload == NULL) {
        return NULL;
    }

    return layer->payload;
}

static uint64_t fertility_cell_count(
    const world_state_t *world,
    const world_u8_payload_t *fertility)
{
    uint32_t columns;
    uint32_t rows;

    if (world == NULL ||
        fertility == NULL ||
        fertility->cell_size == 0 ||
        world->width % fertility->cell_size != 0 ||
        world->depth % fertility->cell_size != 0) {
        return 0;
    }

    columns = world->width / fertility->cell_size;
    rows = world->depth / fertility->cell_size;
    return (uint64_t)columns * (uint64_t)rows;
}

static bool fertility_prepare(
    const world_state_t *world,
    const world_u8_payload_t *fertility)
{
    uint64_t cell_count;

    cell_count = fertility_cell_count(world, fertility);

    if (cell_count == 0) {
        return false;
    }

    return fertility_resize(cell_count);
}

void fertility_add_grass_delta(
    const world_state_t *world,
    uint32_t east_mm,
    uint32_t north_mm,
    int32_t delta)
{
    const world_u8_payload_t *fertility;
    uint32_t index;

    if (delta == 0) {
        return;
    }

    fertility = fertility_payload(world);

    if (fertility == NULL ||
        !fertility_prepare(world, fertility) ||
        !fertility_index_at(
            world,
            fertility,
            east_mm,
            north_mm,
            &index)) {
        return;
    }

    grass_delta[index] += delta;
}

int32_t fertility_available(
    const world_state_t *world,
    uint32_t east_mm,
    uint32_t north_mm)
{
    const world_layer_t *layer;
    const world_u8_payload_t *fertility;
    int64_t published;
    uint32_t index;
    int32_t unposted;

    layer = world_find_layer(world, WORLD_LAYER_FERTILITY);

    if (layer == NULL) {
        return 0;
    }

    fertility = fertility_payload(world);

    if (fertility == NULL) {
        return 0;
    }

    if (!world_layer_value(
            world,
            layer,
            east_mm,
            north_mm,
            &published)) {
        return 0;
    }

    unposted = 0;

    if (fertility_prepare(world, fertility) &&
        fertility_index_at(
            world,
            fertility,
            east_mm,
            north_mm,
            &index)) {
        unposted = grass_delta[index];
    }

    if (published > (int64_t)INT32_MAX - unposted) {
        return INT32_MAX;
    }

    if (published < (int64_t)INT32_MIN - unposted) {
        return INT32_MIN;
    }

    return (int32_t)published + unposted;
}

static uint16_t fertility_humidity_at(
    const world_state_t *world,
    const world_layer_t *humidity_layer,
    const world_layer_t *water_layer,
    uint32_t east_mm,
    uint32_t north_mm)
{
    int64_t cell;

    if (water_layer != NULL && water_layer->payload != NULL) {
        const world_staticwater_payload_t *water = water_layer->payload;

        if (world_layer_value(
                world,
                water_layer,
                east_mm,
                north_mm,
                &cell)) {
            if ((uint64_t)water->min_depth + (uint64_t)cell > 0) {
                return HUMIDITY_SATURATED;
            }
        }
    }

    if (humidity_layer != NULL &&
        world_layer_value(
            world,
            humidity_layer,
            east_mm,
            north_mm,
            &cell)) {
        if (cell <= 0) {
            return 0;
        }

        if (cell >= (int64_t)HUMIDITY_SATURATED) {
            return HUMIDITY_SATURATED;
        }

        return (uint16_t)cell;
    }

    return 0;
}

static uint16_t fertility_humidity_published(
    const world_state_t *world,
    const world_layer_t *humidity_layer,
    uint32_t east_mm,
    uint32_t north_mm)
{
    int64_t cell;

    if (humidity_layer == NULL ||
        !world_layer_value(
            world,
            humidity_layer,
            east_mm,
            north_mm,
            &cell)) {
        return 0;
    }

    if (cell <= 0) {
        return 0;
    }

    if (cell >= (int64_t)HUMIDITY_SATURATED) {
        return HUMIDITY_SATURATED;
    }

    return (uint16_t)cell;
}

static void fertility_fold_deltas(
    double *work,
    uint64_t cell_count)
{
    uint64_t i;

    for (i = 0; i < cell_count; i++) {
        work[i] += (double)grass_delta[i] + fertility_carry[i];
        grass_delta[i] = 0;
        fertility_carry[i] = 0.0;
    }
}

/*
 * Standing flood destroys dissolved nitrogen as gas. Drought
 * does not change the number: the biome stops, and its mass
 * stays as nutrient in the same cell.
 */
static void fertility_moisture_loss(
    const world_state_t *world,
    const world_u8_payload_t *fertility,
    const world_layer_t *humidity_layer,
    const humidity_field_cache_t *field,
    double *work,
    uint32_t columns,
    uint32_t rows,
    double hours)
{
    uint32_t row;
    uint32_t column;
    double days;
    bool by_index;

    if (hours <= 0.0) {
        return;
    }

    days = hours / 24.0;
    by_index = field != NULL && field->humidity != NULL;

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;
            uint16_t humidity;
            double humidity_fraction;
            double flood;
            double loss;

            index = row * columns + column;

            if (by_index) {
                humidity = humidity_field_cache_published(field, index);
            } else {
                uint32_t east_mm;
                uint32_t north_mm;

                east_mm =
                    column * fertility->cell_size +
                    fertility->cell_size / 2;
                north_mm =
                    row * fertility->cell_size +
                    fertility->cell_size / 2;
                humidity = fertility_humidity_published(
                    world,
                    humidity_layer,
                    east_mm,
                    north_mm);
            }

            humidity_fraction =
                (double)humidity / (double)HUMIDITY_SATURATED;

            if (humidity_fraction <= FERTILITY_FLOOD_START) {
                continue;
            }

            flood =
                (humidity_fraction - FERTILITY_FLOOD_START) /
                (1.0 - FERTILITY_FLOOD_START);

            if (work[index] <= 0.0) {
                continue;
            }

            loss =
                work[index] *
                FERTILITY_DENITRIFICATION_PER_DAY *
                flood *
                days;

            if (loss > work[index]) {
                work[index] = 0.0;
            } else {
                work[index] -= loss;
            }
        }
    }
}

static void fertility_advect_step(
    const world_state_t *world,
    const world_u8_payload_t *fertility,
    const world_layer_t *humidity_layer,
    const world_layer_t *water_layer,
    double *work,
    uint32_t columns,
    uint32_t rows,
    double coefficient,
    const humidity_slope_cache_t *slopes,
    const humidity_field_cache_t *field)
{
    uint32_t row;
    uint32_t column;
    bool field_by_index;
    bool slope_by_index;

    field_by_index = field != NULL && field->by_index;
    slope_by_index = slopes != NULL && slopes->by_index;

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;
            uint32_t east_mm;
            uint32_t north_mm;
            uint16_t humidity_here;
            int neighbor;

            index = row * columns + column;
            east_mm = 0;
            north_mm = 0;

            if (!field_by_index || !slope_by_index) {
                east_mm =
                    column * fertility->cell_size +
                    fertility->cell_size / 2;
                north_mm =
                    row * fertility->cell_size +
                    fertility->cell_size / 2;
            }

            if (field_by_index) {
                humidity_here = humidity_field_cache_at(field, index);
            } else {
                humidity_here = fertility_humidity_at(
                    world,
                    humidity_layer,
                    water_layer,
                    east_mm,
                    north_mm);
            }

            for (neighbor = 0; neighbor < 4; neighbor++) {
                uint32_t neighbor_index;
                uint16_t humidity_neighbor;
                int64_t dz;
                double flow;
                uint32_t source;
                uint32_t dest;
                uint16_t humidity_source;
                double amount;
                double carried;
                double mobile;

                if (!world_neighbor(
                        world,
                        columns,
                        rows,
                        column,
                        row,
                        fertility_deltas[neighbor][0],
                        fertility_deltas[neighbor][1],
                        &neighbor_index)) {
                    continue;
                }

                if (neighbor_index <= index) {
                    continue;
                }

                if (field_by_index) {
                    humidity_neighbor =
                        humidity_field_cache_at(field, neighbor_index);
                } else {
                    uint32_t neighbor_column;
                    uint32_t neighbor_row;
                    uint32_t neighbor_east_mm;
                    uint32_t neighbor_north_mm;

                    neighbor_column = neighbor_index % columns;
                    neighbor_row = neighbor_index / columns;
                    neighbor_east_mm =
                        neighbor_column * fertility->cell_size +
                        fertility->cell_size / 2;
                    neighbor_north_mm =
                        neighbor_row * fertility->cell_size +
                        fertility->cell_size / 2;
                    humidity_neighbor = fertility_humidity_at(
                        world,
                        humidity_layer,
                        water_layer,
                        neighbor_east_mm,
                        neighbor_north_mm);
                }

                if (slope_by_index) {
                    dz = humidity_slope_cache_dz(
                        slopes,
                        index,
                        fertility_directions[neighbor]);
                } else {
                    dz = humidity_free_surface_dz(
                        world,
                        fertility->cell_size,
                        east_mm,
                        north_mm,
                        fertility_directions[neighbor]);
                }

                flow = humidity_edge_flow(
                    (int64_t)humidity_here,
                    (int64_t)humidity_neighbor,
                    dz,
                    coefficient);

                if (flow > 0.0) {
                    source = neighbor_index;
                    dest = index;
                    humidity_source = humidity_neighbor;
                    amount = flow;
                } else if (flow < 0.0) {
                    source = index;
                    dest = neighbor_index;
                    humidity_source = humidity_here;
                    amount = -flow;
                } else {
                    continue;
                }

                if (humidity_source == 0 || work[source] <= 0.0) {
                    continue;
                }

                mobile = FERTILITY_HUMIDITY_DRAG * work[source];

                if (amount >= (double)humidity_source) {
                    carried = mobile;
                } else {
                    carried =
                        mobile *
                        (amount / (double)humidity_source);
                }

                if (carried > work[source]) {
                    carried = work[source];
                }

                if (carried <= 0.0) {
                    continue;
                }

                work[source] -= carried;
                work[dest] += carried;
            }
        }
    }
}

static void fertility_store(
    world_u8_payload_t *fertility,
    double *work,
    uint64_t cell_count)
{
    uint64_t i;

    for (i = 0; i < cell_count; i++) {
        long long rounded;

        if (work[i] <= 0.0) {
            fertility->pending[i] = 0;
            fertility_carry[i] = 0.0;
            continue;
        }

        if (work[i] >= (double)FERTILITY_MAX) {
            fertility->pending[i] = (uint8_t)FERTILITY_MAX;
            fertility_carry[i] = 0.0;
            continue;
        }

        rounded = llround(work[i]);

        if (rounded < 0) {
            rounded = 0;
        } else if (rounded > (long long)FERTILITY_MAX) {
            rounded = FERTILITY_MAX;
        }

        fertility->pending[i] = (uint8_t)rounded;
        fertility_carry[i] = work[i] - (double)rounded;
    }
}

void simulate_fertility(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_u8_payload_t *fertility;
    const world_layer_t *humidity_layer;
    const world_layer_t *water_layer;
    humidity_slope_cache_t slopes;
    humidity_field_cache_t field;
    double *work;
    uint32_t columns;
    uint32_t rows;
    uint64_t cell_count;
    uint64_t i;
    double hours;
    double rate;
    double left;
    struct timespec t0;
    struct timespec t1;
    double moisture_ms;
    double advect_ms;
    unsigned int substeps;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    fertility = layer->payload;

    if (fertility->pending == NULL ||
        fertility->published == NULL ||
        fertility->cell_size == 0 ||
        world->width % fertility->cell_size != 0 ||
        world->depth % fertility->cell_size != 0) {
        return;
    }

    if (!fertility_prepare(world, fertility)) {
        return;
    }

    columns = world->width / fertility->cell_size;
    rows = world->depth / fertility->cell_size;
    cell_count = (uint64_t)columns * (uint64_t)rows;

    if (cell_count > SIZE_MAX / sizeof(*work)) {
        return;
    }

    work = malloc((size_t)cell_count * sizeof(*work));

    if (work == NULL) {
        return;
    }

    for (i = 0; i < cell_count; i++) {
        work[i] = (double)fertility->pending[i];
    }

    fertility_fold_deltas(work, cell_count);

    if (tick > layer->last_simulation_tick) {
        hours =
            (double)(tick - layer->last_simulation_tick) /
            3600000.0;
    } else {
        hours = 0.0;
    }

    humidity_layer = world_find_layer(world, WORLD_LAYER_HUMIDITY);
    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);
    humidity_slope_cache_bind(world, fertility->cell_size, &slopes);
    humidity_field_cache_bind(world, fertility->cell_size, &field);
    moisture_ms = 0.0;
    advect_ms = 0.0;
    substeps = 0;

    debug_clock(&t0);
    fertility_moisture_loss(
        world,
        fertility,
        humidity_layer,
        &field,
        work,
        columns,
        rows,
        hours);
    debug_clock(&t1);
    moisture_ms = debug_ms(&t0, &t1);

    if (hours > 0.0 && humidity_layer != NULL) {
        rate = humidity_diffusion_rate(fertility->cell_size);
        left = hours;

        while (left > 0.0 && rate > 0.0) {
            double step_hours;
            double coefficient;

            coefficient = rate * left;
            step_hours = left;

            if (coefficient > HUMIDITY_DIFFUSION_STEP_LIMIT) {
                step_hours = HUMIDITY_DIFFUSION_STEP_LIMIT / rate;
                coefficient = HUMIDITY_DIFFUSION_STEP_LIMIT;
            }

            debug_clock(&t0);
            fertility_advect_step(
                world,
                fertility,
                humidity_layer,
                water_layer,
                work,
                columns,
                rows,
                coefficient,
                &slopes,
                &field);
            debug_clock(&t1);
            advect_ms += debug_ms(&t0, &t1);
            substeps++;

            left -= step_hours;

            if (left < 1e-12) {
                break;
            }
        }
    }

    fertility_store(fertility, work, cell_count);

    if (debug_on()) {
        debug_log(
            "prof fertility cells=%" PRIu64 " substeps=%u moisture=%.1fms "
            "advect=%.1fms",
            cell_count,
            substeps,
            moisture_ms,
            advect_ms);
    }

    free(work);
}
