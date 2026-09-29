#include "world.h"
#include "world_internal.h"
#include "simulation_layer_fertility.h"
#include "simulation_layer_grass.h"
#include "simulation_layer_grassage.h"
#include "simulation_layer_humidity.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mirror_u8(world_u8_payload_t *grid, uint64_t cells)
{
    if (grid == NULL ||
        grid->published == NULL ||
        grid->pending == NULL) {
        return;
    }

    memcpy(grid->pending, grid->published, (size_t)cells);
}

static void publish_u8(world_u8_payload_t *grid)
{
    void *previous;

    if (grid == NULL ||
        grid->published == NULL ||
        grid->pending == NULL) {
        return;
    }

    previous = grid->published;
    grid->published = grid->pending;
    grid->pending = previous;
}

static uint64_t u8_live(const world_u8_payload_t *grid, uint64_t cells)
{
    uint64_t n;
    uint64_t i;

    n = 0;

    if (grid == NULL || grid->published == NULL) {
        return 0;
    }

    for (i = 0; i < cells; i++) {
        if (grid->published[i] > 0) {
            n++;
        }
    }

    return n;
}

static uint64_t u8_sum(const world_u8_payload_t *grid, uint64_t cells)
{
    uint64_t sum;
    uint64_t i;

    sum = 0;

    if (grid == NULL || grid->published == NULL) {
        return 0;
    }

    for (i = 0; i < cells; i++) {
        sum += grid->published[i];
    }

    return sum;
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
    world_tick_t period_grass;
    world_tick_t tick_grow;
    world_tick_t tick_age;
    world_tick_t tick_die;
    uint64_t cells;
    uint64_t i;
    uint64_t live_file;
    uint64_t live_after_grow;
    uint64_t live_after_die;
    uint64_t height_before;
    uint64_t height_after_grow;
    uint64_t grown_cells;
    uint64_t births;
    uint64_t deaths;
    int32_t available;
    const char *path;
    int fail;

    path = argc > 1
        ? argv[1]
        : "../data/world-po-mo-fer-hu-gr-v3.bin";
    fail = 0;

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
        age_layer->payload == NULL ||
        grass_layer->payload == NULL ||
        fertility_layer->payload == NULL ||
        humidity_layer->payload == NULL) {
        fprintf(stderr, "missing grass, age, fertility, or humidity\n");
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    age = age_layer->payload;
    grass = grass_layer->payload;
    fertility = fertility_layer->payload;
    humidity = humidity_layer->payload;
    cells =
        (uint64_t)(world.width / grass->cell_size) *
        (uint64_t)(world.depth / grass->cell_size);
    live_file = u8_live(grass, cells);
    height_before = u8_sum(grass, cells);

    printf(
        "file: live grass %" PRIu64 "  height sum %" PRIu64
        "  fertility sum %" PRIu64 "\n",
        live_file,
        height_before,
        u8_sum(fertility, cells));

    /*
     * Shipped po-mo: fertility 0, pool grass already at 255 mm,
     * bank humidity 0. Seed the three things growth needs, and
     * leave 5 mm of room on plants that were already maxed.
     */
    for (i = 0; i < cells; i++) {
        fertility->published[i] = (uint8_t)FERTILITY_MAX;
        humidity->published[i] = (uint16_t)HUMIDITY_SATURATED;

        if (grass->published[i] >= (uint8_t)GRASS_HEIGHT_MAX) {
            grass->published[i] = (uint8_t)(GRASS_HEIGHT_MAX - 5);
        }
    }

    if (light_layer != NULL && light_layer->payload != NULL) {
        light = light_layer->payload;

        if (light->published != NULL &&
            light->max_irradiance > 0) {
            uint64_t light_cells;

            light_cells =
                (uint64_t)(world.width / light->cell_size) *
                (uint64_t)(world.depth / light->cell_size);

            for (i = 0; i < light_cells; i++) {
                light->published[i] = light->max_irradiance;
            }
        }
    }

    height_before = u8_sum(grass, cells);
    period_age = (world_tick_t)1 << 26;
    period_grass = (world_tick_t)1 << 22;
    tick_grow = (world_tick_t)GRASS_MS_PER_DAY;
    tick_age = 255 * period_age;
    tick_die = tick_age + period_grass;

    mirror_u8(grass, cells);
    simulate_grass(&world, grass_layer, tick_grow);
    publish_u8(grass);
    grass_layer->last_simulation_tick = tick_grow;

    live_after_grow = u8_live(grass, cells);
    height_after_grow = u8_sum(grass, cells);
    grown_cells = 0;

    for (i = 0; i < cells; i++) {
        if (grass->published[i] > 0) {
            grown_cells++;
        }
    }

    grass_total_stats(&births, &deaths);
    printf(
        "after 1 calendar day: live %" PRIu64
        "  height sum %" PRIu64 " -> %" PRIu64
        "  born=%" PRIu64 " died=%" PRIu64 "\n",
        live_after_grow,
        height_before,
        height_after_grow,
        births,
        deaths);

    if (height_after_grow <= height_before) {
        fprintf(stderr, "plants did not grow\n");
        fail = 1;
    }

    mirror_u8(age, cells);
    simulate_grassage(&world, age_layer, tick_age);
    publish_u8(age);
    age_layer->last_simulation_tick = tick_age;

    mirror_u8(grass, cells);
    simulate_grass(&world, grass_layer, tick_die);
    publish_u8(grass);
    grass_layer->last_simulation_tick = tick_die;

    live_after_die = u8_live(grass, cells);
    grass_total_stats(&births, &deaths);
    available = fertility_available(&world, 13600, 10000);

    printf(
        "after age 255: live %" PRIu64
        "  total born=%" PRIu64 " died=%" PRIu64
        "  fertility inbox at pool centre %d\n",
        live_after_die,
        births,
        deaths,
        available);

    world_destroy(&world);

    if (deaths == 0 || live_after_die >= live_after_grow) {
        fprintf(stderr, "plants did not die\n");
        fail = 1;
    }

    (void)grown_cells;

    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
