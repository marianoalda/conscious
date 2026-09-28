#include "simulation_layer_grassage.h"
#include "world_internal.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * Days of this layer's clock since the previous wake. A skipped
 * period adds more than one. Tick 0, with last_simulation_tick 0,
 * adds nothing.
 */
static uint64_t grassage_days(
    const world_layer_t *layer,
    world_tick_t tick)
{
    world_tick_t period;

    if (layer == NULL ||
        layer->clock.mode != WORLD_CLOCK_DIVISOR ||
        layer->clock.exponent >= 64 ||
        tick <= layer->last_simulation_tick) {
        return 0;
    }

    period = (world_tick_t)1 << layer->clock.exponent;

    if (period == 0) {
        return 0;
    }

    return (tick - layer->last_simulation_tick) / period;
}

void simulate_grassage(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_u8_payload_t *age;
    const world_layer_t *grass_layer;
    const world_u8_payload_t *grass;
    const uint8_t *height;
    uint64_t days;
    uint32_t columns;
    uint32_t rows;
    uint64_t cell_count;
    uint64_t i;
    bool same_grid;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    age = layer->payload;

    if (age->pending == NULL ||
        age->published == NULL ||
        age->cell_size == 0 ||
        world->width % age->cell_size != 0 ||
        world->depth % age->cell_size != 0) {
        return;
    }

    days = grassage_days(layer, tick);

    columns = world->width / age->cell_size;
    rows = world->depth / age->cell_size;
    cell_count = (uint64_t)columns * (uint64_t)rows;

    grass_layer = world_find_layer(world, WORLD_LAYER_GRASS);
    grass = NULL;
    height = NULL;
    same_grid = false;

    if (grass_layer != NULL && grass_layer->payload != NULL) {
        grass = grass_layer->payload;

        if (grass->cell_size == age->cell_size) {
            height = grass->published;
            same_grid = height != NULL;
        }
    }

    if (days == 0) {
        return;
    }

    for (i = 0; i < cell_count; i++) {
        uint8_t live_height;
        uint32_t new_age;
        uint32_t east_mm;
        uint32_t north_mm;

        live_height = 0;

        if (same_grid) {
            live_height = height[i];
        } else if (grass_layer != NULL) {
            int64_t value;

            east_mm =
                (uint32_t)(i % columns) * age->cell_size +
                age->cell_size / 2;
            north_mm =
                (uint32_t)(i / columns) * age->cell_size +
                age->cell_size / 2;

            if (world_layer_value(
                    world,
                    grass_layer,
                    east_mm,
                    north_mm,
                    &value) &&
                value > 0) {
                live_height = world_clamp_u8(value);
            }
        }

        if (live_height == 0) {
            age->pending[i] = 0;
            continue;
        }

        new_age = (uint32_t)age->published[i] + (uint32_t)days;

        if (new_age >= (uint32_t)GRASS_AGE_MAX) {
            age->pending[i] = (uint8_t)GRASS_AGE_MAX;
            continue;
        }

        age->pending[i] = world_clamp_u8((int64_t)new_age);
    }
}
