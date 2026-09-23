#include "world.h"
#include "world_internal.h"
#include "world_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world_error_t read_modularity(
    FILE *file,
    world_state_t *world)
{
    char modularity[8];
    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        modularity,
        sizeof(modularity));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            modularity,
            WORLD_MODULARITY_CLOSED_VALUE,
            strlen(WORLD_MODULARITY_CLOSED_VALUE)) == 0 &&
        modularity[6] == '\0' &&
        modularity[7] == '\0') {
        world->modularity = WORLD_MODULARITY_CLOSED;
        return WORLD_OK;
    }

    if (memcmp(
            modularity,
            WORLD_MODULARITY_MODULAR_VALUE,
            strlen(WORLD_MODULARITY_MODULAR_VALUE)) == 0 &&
        modularity[7] == '\0') {
        world->modularity = WORLD_MODULARITY_MODULAR;
        return WORLD_OK;
    }

    return WORLD_ERROR_INVALID_FORMAT;
}

static world_error_t write_modularity(
    FILE *file,
    world_modularity_t modularity)
{
    char buffer[8] = {0};

    switch (modularity) {
        case WORLD_MODULARITY_CLOSED:
            memcpy(
                buffer,
                WORLD_MODULARITY_CLOSED_VALUE,
                strlen(WORLD_MODULARITY_CLOSED_VALUE));
            break;

        case WORLD_MODULARITY_MODULAR:
            memcpy(
                buffer,
                WORLD_MODULARITY_MODULAR_VALUE,
                strlen(WORLD_MODULARITY_MODULAR_VALUE));
            break;

        default:
            return WORLD_ERROR_INVALID_FORMAT;
    }

    if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

static world_error_t read_clock(
    FILE *file,
    world_clock_t *clock)
{
    char buffer[8];
    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        buffer,
        sizeof(buffer));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            buffer,
            WORLD_LAYER_CLOCK_NOEV,
            sizeof(buffer)) == 0) {
        clock->mode = WORLD_CLOCK_NOEV;
        clock->exponent = 0;

        return WORLD_OK;
    }

    if (memcmp(
            buffer,
            WORLD_LAYER_CLOCK_PREFIX,
            strlen(WORLD_LAYER_CLOCK_PREFIX)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (buffer[4] < '0' || buffer[4] > '9' ||
        buffer[5] < '0' || buffer[5] > '9' ||
        buffer[6] < '0' || buffer[6] > '9' ||
        buffer[7] < '0' || buffer[7] > '9') {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    clock->mode = WORLD_CLOCK_DIVISOR;

    clock->exponent =
        (uint16_t)(
            (buffer[4] - '0') * 1000 +
            (buffer[5] - '0') * 100 +
            (buffer[6] - '0') * 10 +
            (buffer[7] - '0'));

    return WORLD_OK;
}

static world_error_t write_clock(
    FILE *file,
    const world_clock_t *clock)
{
    char buffer[8];

    if (clock->mode == WORLD_CLOCK_NOEV) {
        memcpy(
            buffer,
            WORLD_LAYER_CLOCK_NOEV,
            sizeof(buffer));

    } else if (clock->mode == WORLD_CLOCK_DIVISOR) {
        if (clock->exponent > 9999) {
            return WORLD_ERROR_INVALID_FORMAT;
        }

        buffer[0] = 'C';
        buffer[1] = 'L';
        buffer[2] = 'K';
        buffer[3] = '_';
        buffer[4] = (char)('0' + (clock->exponent / 1000) % 10);
        buffer[5] = (char)('0' + (clock->exponent / 100) % 10);
        buffer[6] = (char)('0' + (clock->exponent / 10) % 10);
        buffer[7] = (char)('0' + clock->exponent % 10);

    } else {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

static world_error_t read_dense_values(
    FILE *file,
    const world_state_t *world,
    uint32_t *cell_size,
    int32_t *minimum,
    uint32_t **values)
{
    char storage_type[4];

    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;

    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        storage_type,
        sizeof(storage_type));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            storage_type,
            WORLD_LAYER_STORAGE_DENSE,
            sizeof(storage_type)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_io_read_uint32_be(
        file,
        cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    if (*cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_io_read_int32_be(
        file,
        minimum);

    if (error != WORLD_OK) {
        return error;
    }

    if (world->width % *cell_size != 0 ||
        world->depth % *cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_width = world->width / *cell_size;
    cell_depth = world->depth / *cell_size;

    cell_count = (uint64_t)cell_width * cell_depth;

    if (cell_count > SIZE_MAX / sizeof(uint32_t)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *values = malloc((size_t)cell_count * sizeof(uint32_t));

    if (*values == NULL) {
        return WORLD_ERROR_FILE;
    }

    for (i = 0; i < cell_count; i++) {
        error = world_io_read_uint32_be(
            file,
            &(*values)[i]);

        if (error != WORLD_OK) {
            free(*values);
            *values = NULL;
            return error;
        }
    }

    return WORLD_OK;
}

static world_error_t deserialize_heightmap_v3(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];

    uint32_t cell_size;
    int32_t min_height;
    uint32_t *values;

    world_clock_t clock;
    world_tick_t last_simulation_tick;
    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        layer_name,
        sizeof(layer_name));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_name,
            WORLD_LAYER_NAME_HEIGHTMAP,
            strlen(WORLD_LAYER_NAME_HEIGHTMAP)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = read_clock(
        file,
        &clock);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint64_be(
        file,
        &last_simulation_tick);

    if (error != WORLD_OK) {
        return error;
    }

    error = read_dense_values(
        file,
        world,
        &cell_size,
        &min_height,
        &values);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_append_heightmap_layer(
        world,
        clock,
        last_simulation_tick,
        cell_size,
        min_height,
        values);

    if (error != WORLD_OK) {
        free(values);
        return error;
    }

    return WORLD_OK;
}

static world_error_t deserialize_staticwater_v3(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];

    uint32_t cell_size;
    int32_t min_depth;
    uint32_t *values;

    world_clock_t clock;
    world_tick_t last_simulation_tick;
    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        layer_name,
        sizeof(layer_name));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_name,
            WORLD_LAYER_NAME_STATICWATER,
            strlen(WORLD_LAYER_NAME_STATICWATER)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = read_clock(
        file,
        &clock);

    if (error != WORLD_OK) {
        return error;
    }

    if (clock.mode != WORLD_CLOCK_NOEV) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_io_read_uint64_be(
        file,
        &last_simulation_tick);

    if (error != WORLD_OK) {
        return error;
    }

    error = read_dense_values(
        file,
        world,
        &cell_size,
        &min_depth,
        &values);

    if (error != WORLD_OK) {
        return error;
    }

    if (min_depth < 0) {
        free(values);
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_append_staticwater_layer(
        world,
        clock,
        last_simulation_tick,
        cell_size,
        min_depth,
        values);

    if (error != WORLD_OK) {
        free(values);
        return error;
    }

    return WORLD_OK;
}

static world_error_t read_next_layer_marker(
    FILE *file,
    int *present)
{
    char marker[4];
    size_t read_count;

    read_count = fread(marker, 1, sizeof(marker), file);

    if (read_count == 0) {
        if (ferror(file)) {
            return WORLD_ERROR_FILE;
        }

        *present = 0;
        return WORLD_OK;
    }

    if (read_count != sizeof(marker)) {
        return WORLD_ERROR_TRUNCATED;
    }

    if (memcmp(
            marker,
            WORLD_LAYER_MAGIC,
            sizeof(marker)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *present = 1;
    return WORLD_OK;
}

static world_error_t load_one_layer_v3(
    FILE *file,
    world_state_t *world)
{
    char layer_type[16];
    world_error_t error;

    error = world_io_read_fixed_string(
        file,
        layer_type,
        sizeof(layer_type));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_type,
            WORLD_LAYER_TYPE_HEIGHTMAP,
            strlen(WORLD_LAYER_TYPE_HEIGHTMAP)) == 0) {
        return deserialize_heightmap_v3(
            file,
            world);
    }

    if (memcmp(
            layer_type,
            WORLD_LAYER_TYPE_STATICWATER,
            sizeof(layer_type)) == 0) {
        return deserialize_staticwater_v3(
            file,
            world);
    }

    return WORLD_ERROR_INVALID_FORMAT;
}

static world_error_t check_staticwater_grid(
    const world_state_t *world)
{
    const world_layer_t *terrain;
    const world_layer_t *water;
    const world_heightmap_payload_t *heightmap;
    const world_staticwater_payload_t *staticwater;

    water = world_find_layer(world, WORLD_LAYER_STATICWATER);

    if (water == NULL) {
        return WORLD_OK;
    }

    terrain = world_find_layer(world, WORLD_LAYER_HEIGHTMAP);

    if (terrain == NULL ||
        terrain->payload == NULL ||
        water->payload == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    heightmap = terrain->payload;
    staticwater = water->payload;

    if (staticwater->cell_size != heightmap->cell_size) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    return WORLD_OK;
}

world_error_t world_v3_load(
    FILE *file,
    world_state_t *world)
{
    uint32_t width;
    uint32_t depth;
    uint64_t age;
    int layer_present;
    world_error_t error;

    error = world_io_read_uint32_be(
        file,
        &width);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint32_be(
        file,
        &depth);

    if (error != WORLD_OK) {
        return error;
    }

    if (width == 0 || depth == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    world->width = width;
    world->depth = depth;

    error = read_modularity(
        file,
        world);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint64_be(
        file,
        &age);

    if (error != WORLD_OK) {
        return error;
    }

    world->age = age;

    for (;;) {
        error = read_next_layer_marker(
            file,
            &layer_present);

        if (error != WORLD_OK) {
            return error;
        }

        if (!layer_present) {
            break;
        }

        error = load_one_layer_v3(
            file,
            world);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return check_staticwater_grid(world);
}

static world_error_t write_padded_string(
    FILE *file,
    const char *text,
    size_t field_size)
{
    char buffer[16];
    size_t length;

    if (field_size > sizeof(buffer)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    memset(buffer, 0, field_size);
    length = strlen(text);

    if (length > field_size) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    memcpy(buffer, text, length);

    if (fwrite(buffer, 1, field_size, file) != field_size) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

static world_error_t dense_cell_count(
    const world_state_t *world,
    uint32_t cell_size,
    uint64_t *cell_count)
{
    if (world->width == 0 ||
        world->depth == 0 ||
        cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *cell_count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);

    return WORLD_OK;
}

static world_error_t write_dense_layer(
    FILE *file,
    const char *type,
    const char *name,
    const world_clock_t *clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t minimum,
    const uint32_t *values,
    uint64_t cell_count)
{
    uint64_t i;
    world_error_t error;

    if (values == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (fwrite(
            WORLD_LAYER_MAGIC,
            1,
            sizeof(WORLD_LAYER_MAGIC) - 1,
            file) != sizeof(WORLD_LAYER_MAGIC) - 1) {
        return WORLD_ERROR_FILE;
    }

    error = write_padded_string(
        file,
        type,
        16);

    if (error != WORLD_OK) {
        return error;
    }

    error = write_padded_string(
        file,
        name,
        16);

    if (error != WORLD_OK) {
        return error;
    }

    error = write_clock(
        file,
        clock);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint64_be(
        file,
        last_simulation_tick);

    if (error != WORLD_OK) {
        return error;
    }

    if (fwrite(
            WORLD_LAYER_STORAGE_DENSE,
            1,
            sizeof(WORLD_LAYER_STORAGE_DENSE) - 1,
            file) != sizeof(WORLD_LAYER_STORAGE_DENSE) - 1) {
        return WORLD_ERROR_FILE;
    }

    error = world_io_write_uint32_be(
        file,
        cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_int32_be(
        file,
        minimum);

    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < cell_count; i++) {
        error = world_io_write_uint32_be(
            file,
            values[i]);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}

static world_error_t write_layer_v3(
    FILE *file,
    const world_state_t *world,
    const world_layer_t *layer)
{
    uint64_t cell_count;
    world_error_t error;

    if (layer == NULL || layer->payload == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            const world_heightmap_payload_t *heightmap = layer->payload;

            error = dense_cell_count(
                world,
                heightmap->cell_size,
                &cell_count);

            if (error != WORLD_OK) {
                return error;
            }

            return write_dense_layer(
                file,
                WORLD_LAYER_TYPE_HEIGHTMAP,
                WORLD_LAYER_NAME_HEIGHTMAP,
                &layer->clock,
                layer->last_simulation_tick,
                heightmap->cell_size,
                heightmap->min_height,
                heightmap->values,
                cell_count);
        }

        case WORLD_LAYER_STATICWATER: {
            const world_staticwater_payload_t *water = layer->payload;

            if (layer->clock.mode != WORLD_CLOCK_NOEV ||
                water->min_depth < 0) {
                return WORLD_ERROR_INVALID_FORMAT;
            }

            error = dense_cell_count(
                world,
                water->cell_size,
                &cell_count);

            if (error != WORLD_OK) {
                return error;
            }

            return write_dense_layer(
                file,
                WORLD_LAYER_TYPE_STATICWATER,
                WORLD_LAYER_NAME_STATICWATER,
                &layer->clock,
                layer->last_simulation_tick,
                water->cell_size,
                water->min_depth,
                water->values,
                cell_count);
        }
    }

    return WORLD_ERROR_INVALID_FORMAT;
}

world_error_t world_v3_serialize(
    FILE *file,
    const world_state_t *world)
{
    uint32_t i;
    world_error_t error;

    if (world_find_layer(world, WORLD_LAYER_HEIGHTMAP) == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = check_staticwater_grid(world);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint32_be(
        file,
        world->width);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint32_be(
        file,
        world->depth);

    if (error != WORLD_OK) {
        return error;
    }

    error = write_modularity(
        file,
        world->modularity);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint64_be(
        file,
        world->age);

    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < world->layer_count; i++) {
        error = write_layer_v3(
            file,
            world,
            world->layers[i]);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}

