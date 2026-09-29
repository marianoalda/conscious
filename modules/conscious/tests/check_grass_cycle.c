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
#define GROW_DAYS 20

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

static void sample(
    const char *label,
    const world_state_t *world)
{
    int64_t grass;
    int64_t fertility;
    int64_t humidity;
    int64_t age;

    grass = 0;
    fertility = 0;
    humidity = 0;
    age = 0;
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_GRASS),
        13600,
        10000,
        &grass);
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_FERTILITY),
        13600,
        10000,
        &fertility);
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_HUMIDITY),
        13600,
        10000,
        &humidity);
    world_layer_value(
        world,
        world_find_layer(world, WORLD_LAYER_GRASSAGE),
        13600,
        10000,
        &age);
    printf(
        "%s  pool grass=%" PRId64 "  age=%" PRId64
        "  F=%" PRId64 "  H=%" PRId64 "\n",
        label,
        grass,
        age,
        fertility,
        humidity);
}

static void report_grids(
    const char *label,
    const world_u8_payload_t *grass,
    const world_u8_payload_t *fertility,
    uint64_t cells)
{
    printf(
        "%s  live %" PRIu64 "  height sum %" PRIu64
        "  F sum %" PRIu64 "  F nz %" PRIu64 "\n",
        label,
        u8_nonzero(grass->published, cells),
        u8_sum(grass->published, cells),
        u8_sum(fertility->published, cells),
        u8_nonzero(fertility->published, cells));
}

static void fold_fertility(
    world_state_t *world,
    world_layer_t *fertility_layer,
    world_u8_payload_t *fertility,
    uint64_t cells,
    world_tick_t tick)
{
    mirror_u8(fertility, cells);
    fertility_layer->last_simulation_tick = tick;
    simulate_fertility(world, fertility_layer, tick);
    publish_u8(fertility);
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
    world_tick_t tick_humid;
    world_tick_t tick_grow;
    world_tick_t tick_age;
    world_tick_t tick_die;
    world_tick_t tick_restart;
    uint64_t cells;
    uint64_t light_cells;
    uint64_t births;
    uint64_t deaths;
    uint64_t fert_after_grow;
    uint64_t fert_after_death;
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
    tick_humid = MS_PER_DAY;
    tick_grow = (world_tick_t)GROW_DAYS * MS_PER_DAY;
    tick_age = 255 * period_age;
    tick_die = tick_age + ((world_tick_t)1 << 22);
    tick_restart = tick_die + MS_PER_DAY;

    report_grids("file", grass, fertility, cells);
    sample("file", &world);

    /*
     * Night light: humidity only diffuses, so the bank wets.
     * Noon is published afterwards for grass.
     */
    mirror_u16(humidity, cells);
    simulate_humidity(&world, humidity_layer, tick_humid);
    publish_u16(humidity);
    humidity_layer->last_simulation_tick = tick_humid;

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

    mirror_u8(grass, cells);
    simulate_grass(&world, grass_layer, tick_grow);
    publish_u8(grass);
    grass_layer->last_simulation_tick = tick_grow;
    grass_total_stats(&births, &deaths);
    printf(
        "after %d d growth  born=%" PRIu64 " died=%" PRIu64 "\n",
        GROW_DAYS,
        births,
        deaths);
    report_grids("grown", grass, fertility, cells);
    sample("grown, F still published", &world);

    fold_fertility(
        &world,
        fertility_layer,
        fertility,
        cells,
        tick_grow);
    fert_after_grow = u8_sum(fertility->published, cells);
    report_grids("after uptake fold", grass, fertility, cells);
    sample("after uptake fold", &world);

    mirror_u8(age, cells);
    simulate_grassage(&world, age_layer, tick_age);
    publish_u8(age);
    age_layer->last_simulation_tick = tick_age;

    mirror_u8(grass, cells);
    simulate_grass(&world, grass_layer, tick_die);
    publish_u8(grass);
    grass_layer->last_simulation_tick = tick_die;
    grass_total_stats(&births, &deaths);
    printf(
        "after old-age death  born=%" PRIu64 " died=%" PRIu64 "\n",
        births,
        deaths);
    report_grids("dead, F still inbox", grass, fertility, cells);
    sample("dead, inbox not folded", &world);
    printf(
        "  inbox at pool %d\n",
        fertility_available(&world, 13600, 10000));

    fold_fertility(
        &world,
        fertility_layer,
        fertility,
        cells,
        tick_die);
    fert_after_death = u8_sum(fertility->published, cells);
    report_grids("after death fold", grass, fertility, cells);
    sample("after death fold", &world);

    mirror_u8(grass, cells);
    simulate_grass(&world, grass_layer, tick_restart);
    publish_u8(grass);
    grass_layer->last_simulation_tick = tick_restart;
    grass_total_stats(&births, &deaths);
    printf(
        "after restart day  born=%" PRIu64 " died=%" PRIu64 "\n",
        births,
        deaths);
    report_grids("restart", grass, fertility, cells);
    sample("restart", &world);

    world_destroy(&world);

    if (fert_after_death <= fert_after_grow) {
        fprintf(stderr, "death did not raise published fertility\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
