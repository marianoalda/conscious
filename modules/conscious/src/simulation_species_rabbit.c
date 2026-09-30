#include "simulation_species_rabbit.h"

#include "world_species_rabbit.h"

#include <math.h>
#include <stdint.h>

#define RABBIT_MS_PER_DAY 86400000ULL
#define RABBIT_STEP_MM 1000

/*
 * Calendar days crossed from last_simulation_tick to tick, counting
 * each midnight strictly after last. Same day index → 0.
 */
static uint64_t rabbit_calendar_days(
    world_tick_t last,
    world_tick_t tick)
{
    if (tick <= last) {
        return 0;
    }

    return (tick / RABBIT_MS_PER_DAY) - (last / RABBIT_MS_PER_DAY);
}

static void rabbit_wrap_mm(
    const world_state_t *world,
    int32_t *x_mm,
    int32_t *y_mm)
{
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t depth;

    if (world == NULL || x_mm == NULL || y_mm == NULL) {
        return;
    }

    if (world->modularity != WORLD_MODULARITY_MODULAR) {
        return;
    }

    width = (int64_t)world->width;
    depth = (int64_t)world->depth;

    if (width <= 0 || depth <= 0) {
        return;
    }

    x = *x_mm;
    y = *y_mm;

    x %= width;
    if (x < 0) {
        x += width;
    }

    y %= depth;
    if (y < 0) {
        y += depth;
    }

    *x_mm = (int32_t)x;
    *y_mm = (int32_t)y;
}

void simulate_species_rabbit(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_individual_payload_t *payload;
    uint64_t days;
    uint32_t i;
    int64_t step_mm;

    if (world == NULL || layer == NULL || layer->payload == NULL) {
        return;
    }

    payload = layer->payload;

    if (!world_species_rabbit_is(payload->species) ||
        payload->depth != WORLD_STORAGE_DEPTH_FUNCTIONAL ||
        payload->species_version !=
            WORLD_SPECIES_VERSION_RABBIT_FUNCTIONAL_V1) {
        return;
    }

    days = rabbit_calendar_days(layer->last_simulation_tick, tick);
    if (days == 0 || payload->count == 0 || payload->individuals == NULL) {
        return;
    }

    if (days > (uint64_t)(INT64_MAX / RABBIT_STEP_MM)) {
        return;
    }

    step_mm = (int64_t)days * (int64_t)RABBIT_STEP_MM;

    for (i = 0; i < payload->count; i++) {
        world_individual_t *ind = &payload->individuals[i];
        double radians;
        double dx;
        double dy;

        radians = (double)ind->orientation_mrad / 1000.0;
        dx = (double)step_mm * cos(radians);
        dy = (double)step_mm * sin(radians);

        ind->x_mm = (int32_t)lround((double)ind->x_mm + dx);
        ind->y_mm = (int32_t)lround((double)ind->y_mm + dy);
        rabbit_wrap_mm(world, &ind->x_mm, &ind->y_mm);
    }
}
