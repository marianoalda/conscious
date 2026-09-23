#include "world.h"
#include "world_internal.h"
#include "world_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world_error_t deserialize_heightmap_v1(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];
    char evolution[4];
    char storage_type[4];

    uint32_t cell_size;
    int32_t min_height;
    uint32_t *values;

    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;

    world_clock_t clock;
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

    error = world_io_read_fixed_string(
        file,
        evolution,
        sizeof(evolution));

    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            evolution,
            WORLD_LAYER_EVOLUTION_NONE,
            sizeof(evolution)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    /* in v1, we didn't even have a clock divisor,
     * but the flag for heightmap layer was "no evolution"
     */
    clock.mode = WORLD_CLOCK_NOEV;
    clock.exponent = 0;

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

    error = world_io_read_uint32_be(file, &cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    if (cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_io_read_int32_be(file, &min_height);

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

    values = malloc((size_t)cell_count * sizeof(uint32_t));

    if (values == NULL) {
        return WORLD_ERROR_FILE;
    }

    for (i = 0; i < cell_count; i++) {
        error = world_io_read_uint32_be(
            file,
            &values[i]);

        if (error != WORLD_OK) {
            free(values);
            return error;
        }
    }

    error = world_append_heightmap_layer(
        world,
        clock,
        0,
        cell_size,
        min_height,
        values);

    if (error != WORLD_OK) {
        free(values);
        return error;
    }

    return WORLD_OK;
}

world_error_t world_v1_load(
    FILE *file,
    world_state_t *world)
{
    uint32_t width;
    uint32_t depth;

    char layer_magic[4];
    char layer_type[16];

    world_error_t error;

    error = world_io_read_uint32_be(file, &width);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint32_be(file, &depth);

    if (error != WORLD_OK) {
        return error;
    }

    if (width == 0 || depth == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    world->width = width;
    world->depth = depth;

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

    return deserialize_heightmap_v1(file, world);
}

world_error_t world_v1_serialize(
    FILE *file,
    const world_state_t *world)
{
    const world_layer_t *layer;
    const world_heightmap_payload_t *heightmap;
    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;
    world_error_t error;

    layer = world_find_layer(world, WORLD_LAYER_HEIGHTMAP);

    if (layer == NULL || layer->payload == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    heightmap = layer->payload;

    if (world->width == 0 || world->depth == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (heightmap->cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world->width % heightmap->cell_size != 0 ||
        world->depth % heightmap->cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (heightmap->values == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_width = world->width / heightmap->cell_size;

    cell_depth = world->depth / heightmap->cell_size;

    cell_count = (uint64_t)cell_width * cell_depth;

    error = world_io_write_uint32_be(file, world->width);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint32_be(file, world->depth);

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

        if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
            return WORLD_ERROR_FILE;
        }
    }

    {
        char buffer[16] = {0};

        memcpy(
            buffer,
            WORLD_LAYER_NAME_HEIGHTMAP,
            strlen(WORLD_LAYER_NAME_HEIGHTMAP));

        if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
            return WORLD_ERROR_FILE;
        }
    }

    if (fwrite(
            WORLD_LAYER_EVOLUTION_NONE,
            1,
            sizeof(WORLD_LAYER_EVOLUTION_NONE) - 1,
            file) != sizeof(WORLD_LAYER_EVOLUTION_NONE) - 1) {
        return WORLD_ERROR_FILE;
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
        heightmap->cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_int32_be(
        file,
        heightmap->min_height);

    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < cell_count; i++) {
        error = world_io_write_uint32_be(
            file,
            heightmap->values[i]);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}
