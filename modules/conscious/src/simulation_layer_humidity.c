#include "simulation_layer_humidity.h"
#include "world_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

            if (!world_cell_at(
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

    if (!world_cell_at(
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
 * saturation and does not lose what it gives.
 *
 * Each soil cell sums the neighbours world_neighbor returns.
 * MODULAR therefore exchanges with the opposite edge. CLOSED
 * skips a missing side, which is a wall: nothing crosses it.
 * Grass and water are not looked up here; they are sampled at
 * the cell centre, which is already on the map.
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

                if (!world_neighbor(
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

    if (!world_cell_at(
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
 * 1 with no grass, or height 0. 0.5 when grass is at 255 mm.
 * The shade is linear between those heights. A missing layer
 * leaves the divisor at 1. Grass is only read: its own function
 * does not change the stored height.
 *
 * The sample is the published cell under this centre. Modularity
 * does not move it. Wrapping is only for a neighbour, through
 * world_neighbor.
 * The shipped ring was written with straight distance from the
 * pool, so the shade stops inside the map and does not cross a seam.
 */
static double humidity_grass_divisor(
    const world_state_t *world,
    const world_u8_payload_t *grass,
    uint32_t east_mm,
    uint32_t north_mm)
{
    uint32_t index;

    if (grass == NULL ||
        grass->published == NULL ||
        grass->cell_size == 0) {
        return 1.0;
    }

    if (!world_cell_at(
            world->width,
            world->depth,
            grass->cell_size,
            east_mm,
            north_mm,
            &index)) {
        return 1.0;
    }

    return 1.0 - 0.5 * ((double)grass->published[index] / 255.0);
}

/*
 * Subtract 10 % of the humidity still in the cell per hour at
 * full sun, then scale by the grass divisor. hours is world time
 * since the previous humidity tick, so the clock divisor only
 * chooses how often this runs. The loss is not a fraction of the
 * cell area.
 */
static void humidity_evaporate(
    const world_state_t *world,
    world_u16_payload_t *humidity,
    const world_difflight_payload_t *light,
    const world_u8_payload_t *grass,
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
            double divisor;
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
            divisor = humidity_grass_divisor(
                world,
                grass,
                east_mm,
                north_mm);
            index = row * columns + column;
            value = humidity->pending[index];
            loss =
                HUMIDITY_EVAPORATION_PER_HOUR *
                (double)value *
                fraction *
                divisor *
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
 * Humidity owns its changes. It reads standing water, published
 * daylight, and published grass. It writes no other layer.
 * See doc/layers/humidity.md.
 */
void simulate_humidity(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_u16_payload_t *humidity;
    const world_layer_t *light_layer;
    const world_layer_t *water_layer;
    const world_layer_t *grass_layer;
    const world_difflight_payload_t *light;
    const world_staticwater_payload_t *water;
    const world_u8_payload_t *grass;
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
    grass = NULL;
    light_layer = world_find_layer(world, WORLD_LAYER_DIFFLIGHT);
    grass_layer = world_find_layer(world, WORLD_LAYER_GRASS);

    if (light_layer != NULL) {
        light = light_layer->payload;
    }

    if (grass_layer != NULL) {
        grass = grass_layer->payload;
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
        humidity_evaporate(world, humidity, light, grass, step_hours);
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
