#include "simulation_layer_humidity.h"
#include "world_internal.h"
#include "debug.h"

#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Fraction of the humidity still in the cell removed per hour
 * at full irradiance. Not a fraction of saturation.
 */
#define HUMIDITY_EVAPORATION_PER_HOUR 0.1

static const world_direction_t humidity_directions[4] = {
    WORLD_DIR_EAST,
    WORLD_DIR_WEST,
    WORLD_DIR_NORTH,
    WORLD_DIR_SOUTH
};

static const int64_t *humidity_layer_slope(
    const world_layer_t *layer,
    uint32_t *cell_mm)
{
    if (cell_mm != NULL) {
        *cell_mm = 0;
    }

    if (layer == NULL || layer->payload == NULL) {
        return NULL;
    }

    if (layer->type == WORLD_LAYER_HEIGHTMAP) {
        const world_heightmap_payload_t *grid = layer->payload;

        if (cell_mm != NULL) {
            *cell_mm = grid->cell_size;
        }

        return grid->slope;
    }

    if (layer->type == WORLD_LAYER_STATICWATER) {
        const world_staticwater_payload_t *grid = layer->payload;

        if (cell_mm != NULL) {
            *cell_mm = grid->cell_size;
        }

        return grid->slope;
    }

    return NULL;
}

void humidity_slope_cache_bind(
    const world_state_t *world,
    uint32_t cell_mm,
    humidity_slope_cache_t *cache)
{
    const world_layer_t *heightmap_layer;
    const world_layer_t *water_layer;
    bool height_ok;
    bool water_ok;

    if (cache == NULL) {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    cache->cell_mm = cell_mm;

    if (world == NULL || cell_mm == 0) {
        return;
    }

    heightmap_layer = world_find_layer(world, WORLD_LAYER_HEIGHTMAP);
    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);
    cache->height_slope =
        humidity_layer_slope(heightmap_layer, &cache->height_cell_mm);
    cache->water_slope =
        humidity_layer_slope(water_layer, &cache->water_cell_mm);

    height_ok =
        heightmap_layer == NULL ||
        (cache->height_slope != NULL && cache->height_cell_mm == cell_mm);
    water_ok =
        water_layer == NULL ||
        (cache->water_slope != NULL && cache->water_cell_mm == cell_mm);
    cache->by_index = height_ok && water_ok;
}

int64_t humidity_slope_cache_dz(
    const humidity_slope_cache_t *cache,
    uint32_t index,
    world_direction_t direction)
{
    uint32_t dir;
    uint64_t slot;
    int64_t dz;

    if (cache == NULL || cache->cell_mm == 0) {
        return 0;
    }

    dir = (uint32_t)direction;

    if (dir >= WORLD_SLOPE_DIRS) {
        return 0;
    }

    slot = (uint64_t)index * (uint64_t)WORLD_SLOPE_DIRS + (uint64_t)dir;
    dz = 0;

    if (cache->height_slope != NULL && cache->height_cell_mm != 0) {
        dz +=
            (cache->height_slope[slot] * (int64_t)cache->cell_mm) /
            (int64_t)cache->height_cell_mm;
    }

    if (cache->water_slope != NULL && cache->water_cell_mm != 0) {
        dz +=
            (cache->water_slope[slot] * (int64_t)cache->cell_mm) /
            (int64_t)cache->water_cell_mm;
    }

    return dz;
}

void humidity_field_cache_bind(
    const world_state_t *world,
    uint32_t cell_mm,
    humidity_field_cache_t *cache)
{
    const world_layer_t *humidity_layer;
    const world_layer_t *water_layer;
    bool humidity_ok;
    bool water_ok;

    if (cache == NULL) {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    cache->cell_mm = cell_mm;

    if (world == NULL || cell_mm == 0) {
        return;
    }

    humidity_layer = world_find_layer(world, WORLD_LAYER_HUMIDITY);
    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);

    if (humidity_layer != NULL && humidity_layer->payload != NULL) {
        const world_u16_payload_t *humidity = humidity_layer->payload;

        if (humidity->cell_size == cell_mm) {
            cache->humidity = humidity->published;
        }
    }

    if (water_layer != NULL && water_layer->payload != NULL) {
        const world_staticwater_payload_t *water = water_layer->payload;

        if (water->cell_size == cell_mm) {
            cache->water = water->published;
            cache->water_min_depth = water->min_depth;
        }
    }

    humidity_ok = humidity_layer == NULL || cache->humidity != NULL;
    water_ok = water_layer == NULL || cache->water != NULL;
    cache->by_index = humidity_ok && water_ok;
}

uint16_t humidity_field_cache_at(
    const humidity_field_cache_t *cache,
    uint32_t index)
{
    uint16_t cell;

    if (cache == NULL) {
        return 0;
    }

    if (cache->water != NULL &&
        (uint64_t)cache->water_min_depth +
            (uint64_t)cache->water[index] > 0) {
        return HUMIDITY_SATURATED;
    }

    if (cache->humidity == NULL) {
        return 0;
    }

    cell = cache->humidity[index];

    if (cell == 0) {
        return 0;
    }

    return cell;
}

uint16_t humidity_field_cache_published(
    const humidity_field_cache_t *cache,
    uint32_t index)
{
    uint16_t cell;

    if (cache == NULL || cache->humidity == NULL) {
        return 0;
    }

    cell = cache->humidity[index];

    if (cell == 0) {
        return 0;
    }

    return cell;
}

/*
 * Stored rise of this layer, scaled to one humidity cell step.
 * Missing layer, missing neighbour, or run 0: no slope.
 */
static int64_t humidity_scaled_rise(
    const world_state_t *world,
    const world_layer_t *layer,
    uint32_t cell_mm,
    uint32_t east_mm,
    uint32_t north_mm,
    world_direction_t direction)
{
    const int64_t *slope;
    uint32_t layer_cell_mm;
    uint32_t index;
    uint32_t dir;
    int64_t rise;
    uint32_t run_mm;

    if (layer == NULL || cell_mm == 0) {
        return 0;
    }

    slope = humidity_layer_slope(layer, &layer_cell_mm);
    dir = (uint32_t)direction;

    if (slope != NULL &&
        layer_cell_mm != 0 &&
        world != NULL &&
        dir < WORLD_SLOPE_DIRS &&
        world_cell_at(
            world->width,
            world->depth,
            layer_cell_mm,
            east_mm,
            north_mm,
            &index)) {
        rise = slope[(uint64_t)index * (uint64_t)WORLD_SLOPE_DIRS +
                     (uint64_t)dir];
        return (rise * (int64_t)cell_mm) / (int64_t)layer_cell_mm;
    }

    if (!world_layer_gradient(
            world,
            layer,
            east_mm,
            north_mm,
            direction,
            &rise,
            &run_mm) ||
        run_mm == 0) {
        return 0;
    }

    return (rise * (int64_t)cell_mm) / (int64_t)run_mm;
}

double humidity_diffusion_rate(uint32_t cell_mm)
{
    if (cell_mm == 0) {
        return 0.0;
    }

    return HUMIDITY_DIFFUSION_PER_HOUR *
        (HUMIDITY_DIFFUSION_CELL_MM / (double)cell_mm) *
        (HUMIDITY_DIFFUSION_CELL_MM / (double)cell_mm);
}

int64_t humidity_free_surface_dz(
    const world_state_t *world,
    uint32_t humidity_cell_mm,
    uint32_t east_mm,
    uint32_t north_mm,
    world_direction_t direction)
{
    const world_layer_t *heightmap_layer;
    const world_layer_t *water_layer;

    if (world == NULL) {
        return 0;
    }

    heightmap_layer = world_find_layer(world, WORLD_LAYER_HEIGHTMAP);
    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);

    return humidity_scaled_rise(
               world,
               heightmap_layer,
               humidity_cell_mm,
               east_mm,
               north_mm,
               direction) +
           humidity_scaled_rise(
               world,
               water_layer,
               humidity_cell_mm,
               east_mm,
               north_mm,
               direction);
}

double humidity_edge_flow(
    int64_t humidity_here,
    int64_t humidity_neighbor,
    int64_t dz_mm,
    double coefficient)
{
    double sum;

    sum = (double)humidity_neighbor - (double)humidity_here;

    if (HUMIDITY_CAPILLARY_RISE_MM > 0) {
        sum +=
            (double)((int64_t)HUMIDITY_SATURATED * dz_mm) /
            (double)HUMIDITY_CAPILLARY_RISE_MM;
    }

    return coefficient * sum;
}

/*
 * Cells with standing water become saturated. Depth 0 is dry.
 * The water layer is not reduced.
 */
static void humidity_saturate_standing_water(
    const world_state_t *world,
    world_u16_payload_t *humidity,
    const humidity_field_cache_t *field)
{
    const world_layer_t *layer;
    const world_staticwater_payload_t *water;
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;
    uint64_t i;
    uint64_t cell_count;

    columns = world->width / humidity->cell_size;
    rows = world->depth / humidity->cell_size;
    cell_count = (uint64_t)columns * (uint64_t)rows;

    if (field != NULL && field->water != NULL) {
        for (i = 0; i < cell_count; i++) {
            if ((uint64_t)field->water_min_depth +
                    (uint64_t)field->water[i] > 0) {
                humidity->pending[i] = HUMIDITY_SATURATED;
            }
        }

        return;
    }

    layer = world_find_layer(world, WORLD_LAYER_STATICWATER);

    if (layer == NULL || layer->payload == NULL) {
        return;
    }

    water = layer->payload;

    if (water->published == NULL || water->cell_size == 0) {
        return;
    }

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t east_mm;
            uint32_t north_mm;
            int64_t cell;
            uint64_t depth;

            east_mm =
                column * humidity->cell_size +
                humidity->cell_size / 2;
            north_mm =
                row * humidity->cell_size +
                humidity->cell_size / 2;

            if (!world_layer_value(
                    world,
                    layer,
                    east_mm,
                    north_mm,
                    &cell)) {
                continue;
            }

            depth = (uint64_t)water->min_depth + (uint64_t)cell;

            if (depth > 0) {
                humidity->pending[row * columns + column] =
                    HUMIDITY_SATURATED;
            }
        }
    }
}

/* Depth > 0 at the centre of this humidity cell. */
static bool humidity_covers_water(
    const world_layer_t *water_layer,
    const world_state_t *world,
    const world_u16_payload_t *humidity,
    uint32_t column,
    uint32_t row)
{
    const world_staticwater_payload_t *water;
    uint32_t east_mm;
    uint32_t north_mm;
    int64_t cell;
    uint64_t depth;

    if (water_layer == NULL || water_layer->payload == NULL) {
        return false;
    }

    water = water_layer->payload;

    east_mm =
        column * humidity->cell_size +
        humidity->cell_size / 2;
    north_mm =
        row * humidity->cell_size +
        humidity->cell_size / 2;

    if (!world_layer_value(
            world,
            water_layer,
            east_mm,
            north_mm,
            &cell)) {
        return false;
    }

    depth = (uint64_t)water->min_depth + (uint64_t)cell;

    return depth > 0;
}

/*
 * One explicit step. Flow across an edge is coefficient times the
 * humidity difference plus a gravity term from the free-surface
 * slope (heightmap plus standing water). Soil exchange sums to
 * zero. Standing water stays at saturation and does not lose what
 * it gives.
 *
 * Each soil cell sums the neighbours world_neighbor returns.
 * MODULAR therefore exchanges with the opposite edge. CLOSED
 * skips a missing side, which is a wall: nothing crosses it.
 * Slope is sampled at the cell centre, toward that neighbour.
 */
static void humidity_diffuse_step(
    const world_state_t *world,
    world_u16_payload_t *humidity,
    const uint8_t *wet,
    uint16_t *source,
    double *delta,
    uint32_t columns,
    uint32_t rows,
    double coefficient,
    const humidity_slope_cache_t *slopes)
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
            uint32_t east_mm;
            uint32_t north_mm;
            double sum;
            int neighbor;
            bool need_mm;

            index = row * columns + column;

            if (wet[index]) {
                continue;
            }

            need_mm = slopes == NULL || !slopes->by_index;
            east_mm = 0;
            north_mm = 0;

            if (need_mm) {
                east_mm =
                    column * humidity->cell_size +
                    humidity->cell_size / 2;
                north_mm =
                    row * humidity->cell_size +
                    humidity->cell_size / 2;
            }

            sum = 0.0;

            for (neighbor = 0; neighbor < 4; neighbor++) {
                uint32_t neighbor_index;
                uint16_t neighbor_value;
                int64_t dz;
                world_direction_t direction;

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

                direction = humidity_directions[neighbor];

                if (slopes != NULL && slopes->by_index) {
                    dz = humidity_slope_cache_dz(
                        slopes,
                        index,
                        direction);
                } else {
                    dz = humidity_free_surface_dz(
                        world,
                        humidity->cell_size,
                        east_mm,
                        north_mm,
                        direction);
                }

                sum += humidity_edge_flow(
                    (int64_t)source[index],
                    (int64_t)neighbor_value,
                    dz,
                    1.0);
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
            humidity->pending[index] = world_clamp_u16(value);
        }
    }
}

/*
 * Published irradiance at this point, as a fraction of the layer
 * maximum. Missing light, or a maximum of 0, evaporates nothing.
 */
static double humidity_radiation_fraction(
    const world_state_t *world,
    const world_layer_t *light_layer,
    uint32_t east_mm,
    uint32_t north_mm)
{
    const world_difflight_payload_t *light;
    int64_t irradiance;

    if (light_layer == NULL || light_layer->payload == NULL) {
        return 0.0;
    }

    light = light_layer->payload;

    if (light->max_irradiance == 0) {
        return 0.0;
    }

    if (!world_layer_value(
            world,
            light_layer,
            east_mm,
            north_mm,
            &irradiance)) {
        return 0.0;
    }

    if (irradiance <= 0) {
        return 0.0;
    }

    if ((uint64_t)irradiance >= light->max_irradiance) {
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
    const world_layer_t *grass_layer,
    uint32_t east_mm,
    uint32_t north_mm)
{
    int64_t height;

    if (grass_layer == NULL) {
        return 1.0;
    }

    if (!world_layer_value(
            world,
            grass_layer,
            east_mm,
            north_mm,
            &height) ||
        height <= 0) {
        return 1.0;
    }

    return 1.0 - 0.5 * ((double)world_clamp_u8(height) / (double)WORLD_U8_MAX);
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
    const world_layer_t *light_layer,
    const world_layer_t *grass_layer,
    const uint8_t *grass_cells,
    double hours)
{
    const world_difflight_payload_t *light;
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;

    if (hours <= 0.0 || light_layer == NULL || light_layer->payload == NULL) {
        return;
    }

    light = light_layer->payload;

    if (light->max_irradiance == 0) {
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
                light_layer,
                east_mm,
                north_mm);
            index = row * columns + column;

            if (grass_cells != NULL) {
                divisor =
                    1.0 - 0.5 * ((double)grass_cells[index] /
                    (double)WORLD_U8_MAX);
            } else {
                divisor = humidity_grass_divisor(
                    world,
                    grass_layer,
                    east_mm,
                    north_mm);
            }
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
 * daylight, published grass, and the slope of the heightmap and
 * standing water. It writes no other layer.
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
    humidity_slope_cache_t slopes;
    humidity_field_cache_t field;
    const uint8_t *grass_cells;
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
    struct timespec t0;
    struct timespec t1;
    double wet_ms;
    double diffuse_ms;
    double evaporate_ms;
    double saturate_ms;
    unsigned int substeps;

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

    humidity_field_cache_bind(world, humidity->cell_size, &field);
    humidity_saturate_standing_water(world, humidity, &field);

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

    water_layer = world_find_layer(world, WORLD_LAYER_STATICWATER);
    wet_ms = 0.0;
    diffuse_ms = 0.0;
    evaporate_ms = 0.0;
    saturate_ms = 0.0;
    substeps = 0;

    debug_clock(&t0);
    if (field.water != NULL) {
        uint64_t i;

        for (i = 0; i < cell_count; i++) {
            wet[i] =
                (uint64_t)field.water_min_depth +
                    (uint64_t)field.water[i] >
                0;
        }
    } else {
        for (row = 0; row < rows; row++) {
            for (column = 0; column < columns; column++) {
                uint32_t index;

                index = row * columns + column;
                wet[index] = humidity_covers_water(
                    water_layer,
                    world,
                    humidity,
                    column,
                    row);
            }
        }
    }
    debug_clock(&t1);
    wet_ms = debug_ms(&t0, &t1);

    light_layer = world_find_layer(world, WORLD_LAYER_DIFFLIGHT);
    grass_layer = world_find_layer(world, WORLD_LAYER_GRASS);
    grass_cells = NULL;

    if (grass_layer != NULL && grass_layer->payload != NULL) {
        const world_u8_payload_t *grass = grass_layer->payload;

        if (grass->cell_size == humidity->cell_size) {
            grass_cells = grass->published;
        }
    }

    humidity_slope_cache_bind(world, humidity->cell_size, &slopes);

    rate = humidity_diffusion_rate(humidity->cell_size);
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

        debug_clock(&t0);
        humidity_diffuse_step(
            world,
            humidity,
            wet,
            source,
            delta,
            columns,
            rows,
            coefficient,
            &slopes);
        debug_clock(&t1);
        diffuse_ms += debug_ms(&t0, &t1);

        debug_clock(&t0);
        humidity_evaporate(
            world,
            humidity,
            light_layer,
            grass_layer,
            grass_cells,
            step_hours);
        debug_clock(&t1);
        evaporate_ms += debug_ms(&t0, &t1);

        debug_clock(&t0);
        humidity_saturate_standing_water(world, humidity, &field);
        debug_clock(&t1);
        saturate_ms += debug_ms(&t0, &t1);

        substeps++;
        left -= step_hours;

        if (left < 1e-12) {
            break;
        }
    }

    if (debug_on()) {
        debug_log(
            "prof humidity cells=%" PRIu64 " substeps=%u wet=%.1fms "
            "diffuse=%.1fms evaporate=%.1fms saturate=%.1fms",
            cell_count,
            substeps,
            wet_ms,
            diffuse_ms,
            evaporate_ms,
            saturate_ms);
    }

    free(source);
    free(delta);
    free(wet);
}
