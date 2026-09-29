#include "world.h"
#include "world_internal.h"
#include "simulation_layer_humidity.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void sample(
    const char *label,
    const world_state_t *world,
    const world_layer_t *humidity_layer,
    const world_layer_t *grass_layer,
    const world_layer_t *water_layer)
{
    static const struct {
        const char *name;
        uint32_t east_mm;
        uint32_t north_mm;
    } points[] = {
        {"pool centre", 13600, 10000},
        {"first land east", 14650, 10000},
        {"1 m past shore", 15650, 10000},
        {"5 m grass edge", 18600, 10000},
        {"6 m dry plain", 19650, 10000},
    };
    size_t i;

    printf("%s\n", label);

    for (i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        int64_t humidity;
        int64_t grass;
        int64_t water;

        humidity = 0;
        grass = 0;
        water = 0;
        world_layer_value(
            world,
            humidity_layer,
            points[i].east_mm,
            points[i].north_mm,
            &humidity);
        world_layer_value(
            world,
            grass_layer,
            points[i].east_mm,
            points[i].north_mm,
            &grass);
        world_layer_value(
            world,
            water_layer,
            points[i].east_mm,
            points[i].north_mm,
            &water);
        printf(
            "  %-16s  H=%6" PRId64 "  grass=%3" PRId64 "  water=%4" PRId64 "\n",
            points[i].name,
            humidity,
            grass,
            water);
    }
}

static void count_grass_humidity(
    const world_u16_payload_t *humidity,
    const world_u8_payload_t *grass,
    uint64_t cells)
{
    uint64_t live;
    uint64_t live_wet;
    uint64_t i;

    live = 0;
    live_wet = 0;

    for (i = 0; i < cells; i++) {
        if (grass->published[i] == 0) {
            continue;
        }

        live++;

        if (humidity->published[i] > 0) {
            live_wet++;
        }
    }

    printf(
        "  live grass %" PRIu64 "  with H>0 %" PRIu64 "  with H=0 %" PRIu64 "\n",
        live,
        live_wet,
        live - live_wet);
}

int main(int argc, char **argv)
{
    world_state_t world;
    world_error_t error;
    world_layer_t *humidity_layer;
    const world_layer_t *grass_layer;
    const world_layer_t *water_layer;
    world_u16_payload_t *humidity;
    const world_u8_payload_t *grass;
    uint64_t cells;
    const char *path;
    world_tick_t period;

    path = argc > 1
        ? argv[1]
        : "../data/world-po-mo-fer-hu-gr-v3.bin";

    error = world_load(path, &world);

    if (error != WORLD_OK) {
        fprintf(stderr, "load failed: %s\n", world_error_string(error));
        return EXIT_FAILURE;
    }

    humidity_layer = (world_layer_t *)world_find_layer(
        &world,
        WORLD_LAYER_HUMIDITY);
    grass_layer = world_find_layer(&world, WORLD_LAYER_GRASS);
    water_layer = world_find_layer(&world, WORLD_LAYER_STATICWATER);

    if (humidity_layer == NULL ||
        grass_layer == NULL ||
        water_layer == NULL ||
        humidity_layer->payload == NULL ||
        grass_layer->payload == NULL) {
        fprintf(stderr, "missing layers\n");
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    humidity = humidity_layer->payload;
    grass = grass_layer->payload;
    cells =
        (uint64_t)(world.width / humidity->cell_size) *
        (uint64_t)(world.depth / humidity->cell_size);
    period = (world_tick_t)1 << 16;

    sample("file (no humidity wake)", &world, humidity_layer, grass_layer, water_layer);
    count_grass_humidity(humidity, grass, cells);

    mirror_u16(humidity, cells);
    simulate_humidity(&world, humidity_layer, period);
    publish_u16(humidity);
    humidity_layer->last_simulation_tick = period;

    sample("after 1 CLK_0016 wake", &world, humidity_layer, grass_layer, water_layer);
    count_grass_humidity(humidity, grass, cells);

    mirror_u16(humidity, cells);
    simulate_humidity(&world, humidity_layer, (world_tick_t)86400000);
    publish_u16(humidity);
    humidity_layer->last_simulation_tick = (world_tick_t)86400000;

    sample("after 1 calendar day", &world, humidity_layer, grass_layer, water_layer);
    count_grass_humidity(humidity, grass, cells);

    world_destroy(&world);
    return EXIT_SUCCESS;
}
