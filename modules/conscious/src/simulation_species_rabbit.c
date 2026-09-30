#include "simulation_species_rabbit.h"

#include "world_species_rabbit.h"

#include <math.h>
#include <stdint.h>

/*
 * Calendar days crossed from last to tick, counting each midnight
 * strictly after last. Same day index → 0.
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

sim_rabbit_result_t simulate_species_rabbit_one(
    const world_state_t *world,
    world_individual_t *individual,
    world_tick_t birth_tick,
    world_tick_t *last_tick,
    world_tick_t tick)
{
    uint64_t life_days;
    uint64_t days;
    int64_t step_mm;
    double radians;
    double dx;
    double dy;

    if (world == NULL ||
        individual == NULL ||
        individual->id == 0 ||
        last_tick == NULL) {
        return SIM_RABBIT_OK;
    }

    if (tick >= birth_tick) {
        life_days =
            (tick / RABBIT_MS_PER_DAY) - (birth_tick / RABBIT_MS_PER_DAY);
    } else {
        life_days = 0;
    }

    if (life_days >= (uint64_t)RABBIT_LIFESPAN_DAYS) {
        return SIM_RABBIT_DIED;
    }

    days = rabbit_calendar_days(*last_tick, tick);
    if (days == 0) {
        return SIM_RABBIT_OK;
    }

    if (days > (uint64_t)(INT64_MAX / RABBIT_STEP_MM)) {
        return SIM_RABBIT_OK;
    }

    step_mm = (int64_t)days * (int64_t)RABBIT_STEP_MM;
    radians = (double)individual->orientation_mrad / 1000.0;
    dx = (double)step_mm * cos(radians);
    dy = (double)step_mm * sin(radians);

    individual->x_mm =
        (int32_t)lround((double)individual->x_mm + dx);
    individual->y_mm =
        (int32_t)lround((double)individual->y_mm + dy);
    rabbit_wrap_mm(world, &individual->x_mm, &individual->y_mm);
    *last_tick = tick;

    return SIM_RABBIT_OK;
}
