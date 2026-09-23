#include "simulation_layer_difflight.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Fraction of diffuse daylight at an absolute world tick.
 * Tick 0 is 00:00. The curve is 0 at night, and a half sine
 * from 06:00 through 12:00 to 18:00.
 */
static double day_12h_night_12h(world_tick_t tick)
{
    const world_tick_t milliseconds_per_day = 86400000ULL;
    const world_tick_t dawn = 21600000ULL;
    const world_tick_t dusk = 64800000ULL;
    const world_tick_t daylight = 43200000ULL;
    world_tick_t time_of_day;
    double angle;

    time_of_day = tick % milliseconds_per_day;

    if (time_of_day <= dawn || time_of_day >= dusk) {
        return 0.0;
    }

    angle = 3.14159265358979323846 *
        (double)(time_of_day - dawn) /
        (double)daylight;

    return sin(angle);
}

void simulate_difflight(
    world_state_t *world,
    world_layer_t *layer,
    world_tick_t tick)
{
    world_difflight_payload_t *light;
    double fraction;
    double scaled;
    uint32_t irradiance;
    uint64_t cell_count;
    uint64_t i;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL) {
        return;
    }

    light = layer->payload;

    if (light->published == NULL || light->cell_size == 0) {
        return;
    }

    if (world->width % light->cell_size != 0 ||
        world->depth % light->cell_size != 0) {
        return;
    }

    cell_count =
        (uint64_t)(world->width / light->cell_size) *
        (uint64_t)(world->depth / light->cell_size);

    fraction = day_12h_night_12h(tick);
    scaled = fraction * (double)light->max_irradiance;

    if (scaled <= 0.0) {
        irradiance = 0;
    } else if (scaled >= (double)UINT32_MAX) {
        irradiance = UINT32_MAX;
    } else {
        irradiance = (uint32_t)llround(scaled);
    }

    if (light->pending == NULL) {
        return;
    }

    /* Fill pending. published stays as it was at the start of the tick. */
    for (i = 0; i < cell_count; i++) {
        light->pending[i] = irradiance;
    }
}
