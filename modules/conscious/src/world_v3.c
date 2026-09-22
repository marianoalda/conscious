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

static world_error_t deserialize_heightmap_v3(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];
    char storage_type[4];

    uint32_t cell_size;
    int32_t min_height;

    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;

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
        &world->heightmap.clock);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint64_be(
        file,
        &world->heightmap.last_simulation_tick);

    if (error != WORLD_OK) {
        return error;
    }

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
        &cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    if (cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_io_read_int32_be(
        file,
        &min_height);

    if (error != WORLD_OK) {
        return error;
    }

    if (world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_width = world->width / cell_size;
    cell_depth = world->depth / cell_size;

    cell_count = (uint64_t)cell_width * cell_depth;

    if (cell_count > SIZE_MAX / sizeof(uint32_t)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    world->heightmap.values =
        malloc((size_t)cell_count * sizeof(uint32_t));

    if (world->heightmap.values == NULL) {
        return WORLD_ERROR_FILE;
    }

    world->heightmap.cell_size = cell_size;
    world->heightmap.min_height = min_height;

    for (i = 0; i < cell_count; i++) {
        error = world_io_read_uint32_be(
            file,
            &world->heightmap.values[i]);

        if (error != WORLD_OK) {
            free(world->heightmap.values);
            world->heightmap.values = NULL;
            return error;
        }
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

    char layer_magic[4];
    char layer_type[16];

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

    error = world_io_read_fixed_string(
        file,
        layer_magic,
        sizeof(layer_magic));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_magic,
            WORLD_LAYER_MAGIC,
            sizeof(layer_magic)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

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
            strlen(WORLD_LAYER_TYPE_HEIGHTMAP)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    return deserialize_heightmap_v3(
        file,
        world);
}

world_error_t world_v3_serialize(
    FILE *file,
    const world_state_t *world)
{
    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;
    world_error_t error;

    if (world->width == 0 ||
        world->depth == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world->heightmap.cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world->width % world->heightmap.cell_size != 0 ||
        world->depth % world->heightmap.cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world->heightmap.values == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_width =
        world->width / world->heightmap.cell_size;

    cell_depth =
        world->depth / world->heightmap.cell_size;

    cell_count =
        (uint64_t)cell_width * cell_depth;

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

    if (fwrite(
            WORLD_LAYER_MAGIC,
            1,
            sizeof(WORLD_LAYER_MAGIC) - 1,
            file) != sizeof(WORLD_LAYER_MAGIC) - 1) {
        return WORLD_ERROR_FILE;
    }

    {
        char buffer[16] = {0};

        memcpy(
            buffer,
            WORLD_LAYER_TYPE_HEIGHTMAP,
            strlen(WORLD_LAYER_TYPE_HEIGHTMAP));

        if (fwrite(
                buffer,
                1,
                sizeof(buffer),
                file) != sizeof(buffer)) {
            return WORLD_ERROR_FILE;
        }
    }

    {
        char buffer[16] = {0};

        memcpy(
            buffer,
            WORLD_LAYER_NAME_HEIGHTMAP,
            strlen(WORLD_LAYER_NAME_HEIGHTMAP));

        if (fwrite(
                buffer,
                1,
                sizeof(buffer),
                file) != sizeof(buffer)) {
            return WORLD_ERROR_FILE;
        }
    }

    error = write_clock(
        file,
        &world->heightmap.clock);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint64_be(
        file,
        world->heightmap.last_simulation_tick);

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
        world->heightmap.cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_int32_be(
        file,
        world->heightmap.min_height);

    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < cell_count; i++) {
        error = world_io_write_uint32_be(
            file,
            world->heightmap.values[i]);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}

