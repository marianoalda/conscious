#include "simulation_layer_grass.h"
#include "simulation_layer_humidity.h"
#include "world_internal.h"
#include "debug.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const int grass_deltas[4][2] = {
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1}
};

static double *grass_carry;
static uint64_t grass_carry_count;
static uint64_t grass_births_cycle;
static uint64_t grass_deaths_cycle;
static uint64_t grass_births_total;
static uint64_t grass_deaths_total;

void grass_cycle_stats(
    uint64_t *births,
    uint64_t *deaths)
{
    if (births != NULL) {
        *births = grass_births_cycle;
    }

    if (deaths != NULL) {
        *deaths = grass_deaths_cycle;
    }
}

void grass_total_stats(
    uint64_t *births,
    uint64_t *deaths)
{
    if (births != NULL) {
        *births = grass_births_total;
    }

    if (deaths != NULL) {
        *deaths = grass_deaths_total;
    }
}

static void grass_clear_carry(void)
{
    free(grass_carry);
    grass_carry = NULL;
    grass_carry_count = 0;
}

static bool grass_resize_carry(uint64_t cell_count)
{
    double *grown;

    if (cell_count == grass_carry_count &&
        (cell_count == 0 || grass_carry != NULL)) {
        return true;
    }

    grass_clear_carry();

    if (cell_count == 0) {
        return true;
    }

    if (cell_count > SIZE_MAX / sizeof(*grown)) {
        return false;
    }

    grown = calloc((size_t)cell_count, sizeof(*grown));

    if (grown == NULL) {
        return false;
    }

    grass_carry = grown;
    grass_carry_count = cell_count;
    return true;
}

/*
 * 1 at or above half of maximum. Linear from 0 at value 0 to 1
 * at the half. Missing or empty layers are 0.
 */
static double grass_limit_factor(int64_t value, int64_t maximum)
{
    if (maximum <= 0 || value <= 0) {
        return 0.0;
    }

    if (value >= maximum - value) {
        return 1.0;
    }

    return (double)value / (GRASS_LIMIT_FRACTION * (double)maximum);
}

static double grass_radiation_fraction(
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

static uint8_t grass_age_at(
    const world_state_t *world,
    const world_layer_t *age_layer,
    const uint8_t *age_cells,
    uint32_t index,
    uint32_t east_mm,
    uint32_t north_mm)
{
    int64_t value;

    if (age_cells != NULL) {
        return age_cells[index];
    }

    if (age_layer == NULL) {
        return 0;
    }

    if (!world_layer_value(
            world,
            age_layer,
            east_mm,
            north_mm,
            &value) ||
        value <= 0) {
        return 0;
    }

    return world_clamp_u8(value);
}

static uint8_t grass_height_at(
    const world_u8_payload_t *grass,
    uint32_t index)
{
    if (grass == NULL || grass->published == NULL) {
        return 0;
    }

    return grass->published[index];
}

static bool grass_has_mature_parent(
    const world_state_t *world,
    const world_u8_payload_t *grass,
    const world_layer_t *age_layer,
    const uint8_t *age_cells,
    uint32_t columns,
    uint32_t rows,
    uint32_t column,
    uint32_t row)
{
    uint32_t d;

    if (world == NULL || grass == NULL || grass->published == NULL) {
        return false;
    }

    for (d = 0; d < 4; d++) {
        uint32_t neighbor;
        uint8_t neighbor_height;
        uint8_t neighbor_age;
        uint32_t east_mm;
        uint32_t north_mm;

        if (!world_neighbor(
                world,
                columns,
                rows,
                column,
                row,
                grass_deltas[d][0],
                grass_deltas[d][1],
                &neighbor)) {
            continue;
        }

        neighbor_height = grass_height_at(grass, neighbor);

        if (neighbor_height == 0) {
            continue;
        }

        east_mm =
            (neighbor % columns) * grass->cell_size +
            grass->cell_size / 2;
        north_mm =
            (neighbor / columns) * grass->cell_size +
            grass->cell_size / 2;
        neighbor_age = grass_age_at(
            world,
            age_layer,
            age_cells,
            neighbor,
            east_mm,
            north_mm);

        if ((uint32_t)neighbor_age * 4u > (uint32_t)GRASS_AGE_MAX) {
            return true;
        }
    }

    return false;
}

static void grass_zero_age(
    world_u8_payload_t *age,
    uint32_t index)
{
    if (age == NULL) {
        return;
    }

    if (age->pending != NULL) {
        age->pending[index] = 0;
    }

    if (age->published != NULL) {
        age->published[index] = 0;
    }
}

static void grass_zero_height(
    world_u8_payload_t *grass,
    uint32_t index)
{
    if (grass == NULL) {
        return;
    }

    if (grass->pending != NULL) {
        grass->pending[index] = 0;
    }

    if (grass->published != NULL) {
        grass->published[index] = 0;
    }
}

static void grass_die(
    const world_state_t *world,
    world_u8_payload_t *grass,
    world_u8_payload_t *age,
    uint32_t index,
    uint8_t height)
{
    grass_zero_height(grass, index);

    if (index < grass_carry_count) {
        grass_carry[index] = 0.0;
    }

    grass_zero_age(age, index);
    fertility_add_grass_delta_at(
        world,
        index,
        grass_death_fertility_return(height));
}

static bool grass_can_birth(
    uint16_t humidity,
    int32_t fertility)
{
    if ((uint32_t)humidity * 10u <= HUMIDITY_SATURATED) {
        return false;
    }

    if (fertility <= 0) {
        return false;
    }

    if ((int64_t)fertility * 10 <= (int64_t)FERTILITY_MAX) {
        return false;
    }

    return true;
}

void simulate_grass(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_u8_payload_t *grass;
    const world_layer_t *age_layer;
    world_u8_payload_t *age;
    const uint8_t *age_cells;
    const world_layer_t *light_layer;
    const world_layer_t *fertility_layer;
    const world_u8_payload_t *fertility;
    const uint8_t *fertility_cells;
    const world_layer_t *humidity_layer;
    humidity_field_cache_t humidity_field;
    double days;
    double radiation_world;
    bool radiation_uniform;
    uint32_t columns;
    uint32_t rows;
    uint64_t cell_count;
    uint64_t i;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    grass = layer->payload;

    if (grass->pending == NULL ||
        grass->published == NULL ||
        grass->cell_size == 0 ||
        world->width % grass->cell_size != 0 ||
        world->depth % grass->cell_size != 0) {
        return;
    }

    columns = world->width / grass->cell_size;
    rows = world->depth / grass->cell_size;
    cell_count = (uint64_t)columns * (uint64_t)rows;

    if (!grass_resize_carry(cell_count)) {
        return;
    }

    grass_births_cycle = 0;
    grass_deaths_cycle = 0;

    if (tick > layer->last_simulation_tick) {
        days =
            (double)(tick - layer->last_simulation_tick) /
            GRASS_MS_PER_DAY;
    } else {
        days = 0.0;
    }

    age_layer = world_find_layer(world, WORLD_LAYER_GRASSAGE);
    age = NULL;
    age_cells = NULL;

    if (age_layer != NULL && age_layer->payload != NULL) {
        age = age_layer->payload;

        if (age->cell_size == grass->cell_size) {
            age_cells = age->published;
        }
    }

    light_layer = world_find_layer(world, WORLD_LAYER_DIFFLIGHT);
    radiation_uniform = false;
    radiation_world = 0.0;

    if (light_layer != NULL && light_layer->payload != NULL) {
        const world_difflight_payload_t *light = light_layer->payload;

        if (light->cell_size == world->width &&
            light->cell_size == world->depth) {
            radiation_world = grass_radiation_fraction(
                world,
                light_layer,
                world->width / 2,
                world->depth / 2);
            radiation_uniform = true;
        }
    }

    fertility_layer = world_find_layer(world, WORLD_LAYER_FERTILITY);
    fertility = NULL;
    fertility_cells = NULL;

    if (fertility_layer != NULL && fertility_layer->payload != NULL) {
        fertility = fertility_layer->payload;

        if (fertility->cell_size == grass->cell_size) {
            fertility_cells = fertility->published;
        }
    }

    humidity_layer = world_find_layer(world, WORLD_LAYER_HUMIDITY);
    humidity_field_cache_bind(world, grass->cell_size, &humidity_field);

    for (i = 0; i < cell_count; i++) {
        uint32_t column;
        uint32_t row;
        uint32_t east_mm;
        uint32_t north_mm;
        uint8_t height;
        uint8_t cell_age;
        uint16_t humidity;
        int64_t fertility_cell;
        int32_t available;
        double radiation;
        double fert_factor;
        double hum_factor;

        column = (uint32_t)(i % columns);
        row = (uint32_t)(i / columns);
        east_mm = column * grass->cell_size + grass->cell_size / 2;
        north_mm = row * grass->cell_size + grass->cell_size / 2;
        height = grass->published[i];
        cell_age = grass_age_at(
            world,
            age_layer,
            age_cells,
            (uint32_t)i,
            east_mm,
            north_mm);

        if (humidity_field.by_index) {
            humidity = humidity_field_cache_published(
                &humidity_field,
                (uint32_t)i);
        } else {
            int64_t value;

            humidity = 0;

            if (world_layer_value(
                    world,
                    humidity_layer,
                    east_mm,
                    north_mm,
                    &value) &&
                value > 0) {
                humidity = world_clamp_u16(value);
            }
        }

        if (fertility_cells != NULL) {
            fertility_cell = fertility_cells[i];
            available = fertility_available_at(world, (uint32_t)i);
        } else {
            fertility_cell = 0;
            available = fertility_available(world, east_mm, north_mm);

            if (fertility_layer != NULL) {
                world_layer_value(
                    world,
                    fertility_layer,
                    east_mm,
                    north_mm,
                    &fertility_cell);
            }
        }

        if (height == 0) {
            if (grass_carry[i] != 0.0) {
                grass_carry[i] = 0.0;
            }

            if (!grass_can_birth(humidity, (int32_t)fertility_cell)) {
                grass->pending[i] = 0;
                continue;
            }

            if (available < (int32_t)FERTILITY_UPTAKE_PER_MM *
                    (int32_t)GRASS_BIRTH_HEIGHT_MM) {
                grass->pending[i] = 0;
                continue;
            }

            if (!grass_has_mature_parent(
                    world,
                    grass,
                    age_layer,
                    age_cells,
                    columns,
                    rows,
                    column,
                    row)) {
                grass->pending[i] = 0;
                continue;
            }

            grass->pending[i] = (uint8_t)GRASS_BIRTH_HEIGHT_MM;
            grass_births_cycle++;
            grass_births_total++;
            fertility_add_grass_delta_at(
                world,
                (uint32_t)i,
                -(int32_t)FERTILITY_UPTAKE_PER_MM *
                    (int32_t)GRASS_BIRTH_HEIGHT_MM);
            continue;
        }

        if (cell_age >= (uint8_t)GRASS_AGE_MAX) {
            grass_die(world, grass, age, (uint32_t)i, height);
            grass_deaths_cycle++;
            grass_deaths_total++;
            continue;
        }

        if (radiation_uniform) {
            radiation = radiation_world;
        } else {
            radiation = grass_radiation_fraction(
                world,
                light_layer,
                east_mm,
                north_mm);
        }

        fert_factor = grass_limit_factor(
            fertility_cell,
            (int64_t)FERTILITY_MAX);
        hum_factor = grass_limit_factor(
            (int64_t)humidity,
            (int64_t)HUMIDITY_SATURATED);

        if (days <= 0.0 ||
            radiation <= 0.0 ||
            fert_factor <= 0.0 ||
            hum_factor <= 0.0) {
            grass->pending[i] = height;
            continue;
        }

        {
            double gain;
            double total;
            double fraction;
            long long whole;
            long long room;
            long long payable;
            uint8_t grown;

            gain =
                (double)GRASS_GROWTH_MM_PER_DAY *
                days *
                radiation *
                fert_factor *
                hum_factor;
            total = gain + grass_carry[i];

            if (total <= 0.0) {
                grass->pending[i] = height;
                grass_carry[i] = 0.0;
                continue;
            }

            whole = (long long)total;
            fraction = total - (double)whole;
            room = (long long)GRASS_HEIGHT_MAX - (long long)height;

            if (whole > room) {
                whole = room;
            }

            if (FERTILITY_UPTAKE_PER_MM > 0) {
                payable =
                    (long long)available /
                    (long long)FERTILITY_UPTAKE_PER_MM;

                if (payable < 0) {
                    payable = 0;
                }

                if (whole > payable) {
                    whole = payable;
                }
            }

            if (whole < 0) {
                whole = 0;
            }

            grown = world_clamp_u8((int64_t)height + whole);
            grass->pending[i] = grown;

            if (grown >= (uint8_t)GRASS_HEIGHT_MAX) {
                grass_carry[i] = 0.0;
            } else {
                grass_carry[i] = fraction;
            }

            if (whole > 0) {
                fertility_add_grass_delta_at(
                    world,
                    (uint32_t)i,
                    -(int32_t)FERTILITY_UPTAKE_PER_MM * (int32_t)whole);
            }
        }
    }

    debug_log(
        "grass cycle born=%" PRIu64 " died=%" PRIu64
        " total born=%" PRIu64 " died=%" PRIu64,
        grass_births_cycle,
        grass_deaths_cycle,
        grass_births_total,
        grass_deaths_total);
}
