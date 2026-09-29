#include "world.h"
#include "world_internal.h"
#include "simulation_layer_difflight.h"
#include "simulation_layer_fertility.h"
#include "simulation_layer_grass.h"
#include "simulation_layer_grassage.h"
#include "simulation_layer_humidity.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MS_PER_DAY ((world_tick_t)86400000)
#define NOON_MS ((world_tick_t)43200000)
#define GENERATIONS 6
#define STEPS_PER_LIFE 255
#define REPORT_EVERY 85

static void mirror_u8(world_u8_payload_t *grid, uint64_t cells)
{
    memcpy(grid->pending, grid->published, (size_t)cells);
}

static void publish_u8(world_u8_payload_t *grid)
{
    void *previous;

    previous = grid->published;
    grid->published = grid->pending;
    grid->pending = previous;
}

static void mirror_u16(world_u16_payload_t *grid, uint64_t cells)
{
    memcpy(grid->pending, grid->published, (size_t)cells * sizeof(uint16_t));
}

static void publish_u16(world_u16_payload_t *grid)
{
    void *previous;

    previous = grid->published;
    grid->published = grid->pending;
    grid->pending = previous;
}

static uint64_t u8_sum(const uint8_t *cells, uint64_t count)
{
    uint64_t sum;
    uint64_t i;

    sum = 0;

    for (i = 0; i < count; i++) {
        sum += cells[i];
    }

    return sum;
}

static uint64_t u8_nonzero(const uint8_t *cells, uint64_t count)
{
    uint64_t n;
    uint64_t i;

    n = 0;

    for (i = 0; i < count; i++) {
        if (cells[i] > 0) {
            n++;
        }
    }

    return n;
}

static uint64_t living_age_sum(
    const uint8_t *grass,
    const uint8_t *age,
    uint64_t cells)
{
    uint64_t sum;
    uint64_t i;

    sum = 0;

    for (i = 0; i < cells; i++) {
        if (grass[i] > 0) {
            sum += age[i];
        }
    }

    return sum;
}

static void sample_pool(
    const world_state_t *world,
    int64_t *grass,
    int64_t *age,
    int64_t *fertility)
{
    *grass = 0;
    *age = 0;
    *fertility = 0;
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_GRASS),
        13600,
        10000,
        grass);
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_GRASSAGE),
        13600,
        10000,
        age);
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_FERTILITY),
        13600,
        10000,
        fertility);
}

static void report(
    unsigned int step,
    const world_state_t *world,
    const world_u8_payload_t *grass,
    const world_u8_payload_t *age,
    const world_u8_payload_t *fertility,
    uint64_t cells,
    uint64_t born_window,
    uint64_t died_window)
{
    uint64_t live;
    uint64_t height;
    uint64_t ages;
    int64_t pool_g;
    int64_t pool_age;
    int64_t pool_f;

    live = u8_nonzero(grass->published, cells);
    height = u8_sum(grass->published, cells);
    ages = living_age_sum(grass->published, age->published, cells);
    sample_pool(world, &pool_g, &pool_age, &pool_f);
    printf(
        "%4u  live %5" PRIu64 "  h_mean %5.1f  age_mean %5.1f"
        "  F_sum %7" PRIu64 "  F_nz %5" PRIu64
        "  pool g=%3" PRId64 " a=%3" PRId64 " F=%3" PRId64
        "  Δborn %5" PRIu64 " Δdied %5" PRIu64 "\n",
        step,
        live,
        live == 0 ? 0.0 : (double)height / (double)live,
        live == 0 ? 0.0 : (double)ages / (double)live,
        u8_sum(fertility->published, cells),
        u8_nonzero(fertility->published, cells),
        pool_g,
        pool_age,
        pool_f,
        born_window,
        died_window);
}

int main(int argc, char **argv)
{
    world_state_t world;
    world_error_t error;
    world_layer_t *age_layer;
    world_layer_t *grass_layer;
    world_layer_t *fertility_layer;
    world_layer_t *humidity_layer;
    world_layer_t *light_layer;
    world_u8_payload_t *age;
    world_u8_payload_t *grass;
    world_u8_payload_t *fertility;
    world_u16_payload_t *humidity;
    world_difflight_payload_t *light;
    world_tick_t period_age;
    world_tick_t tick;
    uint64_t cells;
    uint64_t light_cells;
    uint64_t i;
    uint64_t born;
    uint64_t died;
    uint64_t born_prev;
    uint64_t died_prev;
    unsigned int step;
    unsigned int steps;
    const char *path;

    path = argc > 1
        ? argv[1]
        : "../data/world-po-mo-fer-hu-gr-v3.bin";

    error = world_load(path, &world);

    if (error != WORLD_OK) {
        fprintf(stderr, "load failed: %s\n", world_error_string(error));
        return EXIT_FAILURE;
    }

    age_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_GRASSAGE);
    grass_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_GRASS);
    fertility_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_FERTILITY);
    humidity_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_HUMIDITY);
    light_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_DIFFLIGHT);

    if (age_layer == NULL ||
        grass_layer == NULL ||
        fertility_layer == NULL ||
        humidity_layer == NULL ||
        light_layer == NULL) {
        fprintf(stderr, "missing layers\n");
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    age = age_layer->payload;
    grass = grass_layer->payload;
    fertility = fertility_layer->payload;
    humidity = humidity_layer->payload;
    light = light_layer->payload;
    cells =
        (uint64_t)(world.width / grass->cell_size) *
        (uint64_t)(world.depth / grass->cell_size);
    light_cells =
        (uint64_t)(world.width / light->cell_size) *
        (uint64_t)(world.depth / light->cell_size);
    period_age = (world_tick_t)1 << 26;

    mirror_u16(humidity, cells);
    simulate_humidity(&world, humidity_layer, MS_PER_DAY);
    publish_u16(humidity);
    humidity_layer->last_simulation_tick = MS_PER_DAY;

    memcpy(
        light->pending,
        light->published,
        (size_t)light_cells * sizeof(uint32_t));
    simulate_difflight(&world, light_layer, NOON_MS);
    {
        void *previous = light->published;

        light->published = light->pending;
        light->pending = previous;
    }
    light_layer->last_simulation_tick = NOON_MS;

    /*
     * Three age classes so a death does not wipe the stand.
     * Even-aged (all 0) goes extinct on the first 255 and stays
     * bare: empty cells need a living parent older than 63.
     */
    for (i = 0; i < cells; i++) {
        if (grass->published[i] > 0) {
            age->published[i] = (uint8_t)((i % 3) * 85);
        } else {
            age->published[i] = 0;
        }
    }

    tick = 0;
    grass_layer->last_simulation_tick = 0;
    age_layer->last_simulation_tick = 0;
    fertility_layer->last_simulation_tick = 0;
    born_prev = 0;
    died_prev = 0;
    steps = (unsigned int)GENERATIONS * (unsigned int)STEPS_PER_LIFE;

    printf(
        "cohorts 0/85/170, %u age-days, noon, humidity of day 1 held\n",
        steps);
    report(
        0,
        &world,
        grass,
        age,
        fertility,
        cells,
        0,
        0);

    for (step = 1; step <= steps; step++) {
        tick += period_age;

        mirror_u8(age, cells);
        simulate_grassage(&world, age_layer, tick);
        publish_u8(age);
        age_layer->last_simulation_tick = tick;

        mirror_u8(grass, cells);
        simulate_grass(&world, grass_layer, tick);
        publish_u8(grass);
        grass_layer->last_simulation_tick = tick;

        mirror_u8(fertility, cells);
        fertility_layer->last_simulation_tick = tick;
        simulate_fertility(&world, fertility_layer, tick);
        publish_u8(fertility);

        if (step % REPORT_EVERY == 0 || step == steps) {
            grass_total_stats(&born, &died);
            report(
                step,
                &world,
                grass,
                age,
                fertility,
                cells,
                born - born_prev,
                died - died_prev);
            born_prev = born;
            died_prev = died;
        }
    }

    world_destroy(&world);
    return EXIT_SUCCESS;
}
