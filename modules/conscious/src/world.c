#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "world.h"

static world_error_t read_fixed_string(
    FILE *file,
    char *buffer,
    size_t size)
{
    if (fread(buffer, 1, size, file) != size) {
        return WORLD_ERROR_TRUNCATED;
    }

    return WORLD_OK;
}

static world_error_t read_uint32_be(
    FILE *file,
    uint32_t *value)
{
    unsigned char buffer[4];

    if (fread(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_TRUNCATED;
    }

    *value =
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8) |
        ((uint32_t)buffer[3]);

    return WORLD_OK;
}

static world_error_t read_int32_be(
    FILE *file,
    int32_t *value)
{
    uint32_t raw;
    world_error_t error;

    error = read_uint32_be(file, &raw);

    if (error != WORLD_OK) {
        return error;
    }

    *value = (int32_t)raw;

    return WORLD_OK;
}

static world_error_t write_uint32_be(
    FILE *file,
    uint32_t value)
{
    unsigned char buffer[4];

    buffer[0] = (unsigned char)(value >> 24);
    buffer[1] = (unsigned char)(value >> 16);
    buffer[2] = (unsigned char)(value >> 8);
    buffer[3] = (unsigned char)value;

    if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

static world_error_t write_int32_be(
    FILE *file,
    int32_t value)
{
    return write_uint32_be(
        file,
        (uint32_t)value);
}

/******************************
 * Deserialize version 0 of the world file format.
 *
 * Version 0 contains no world state beyond the header.
 */
static int deserialize_v0(FILE *file)
{
    (void)file;

    return 0;
}

/******************************
 * Deserialize the heightmap layer of the world file.
 */
static world_error_t deserialize_heightmap(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];
    char evolution[4];
    char storage_type[4];

    uint32_t cell_size;
    int32_t min_height;

    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;

    world_error_t error;

    error = read_fixed_string(
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

    error = read_fixed_string(
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

    error = read_fixed_string(
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

    error = read_uint32_be(file, &cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    if (cell_size == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = read_int32_be(file, &min_height);

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
        error = read_uint32_be(
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

/******************************
 * Deserialize version 1 of the world file format.
 *
 * Version 1 contains the width and depth of the world.
 */
static world_error_t deserialize_v1(
    FILE *file,
    world_state_t *world)
{
    uint32_t width;
    uint32_t depth;

    char layer_magic[4];
    char layer_type[16];

    world_error_t error;

    error = read_uint32_be(file, &width);

    if (error != WORLD_OK) {
        return error;
    }

    error = read_uint32_be(file, &depth);

    if (error != WORLD_OK) {
        return error;
    }

    if (width == 0 || depth == 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    world->width = width;
    world->depth = depth;

    error = read_fixed_string(
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

    error = read_fixed_string(
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

    return deserialize_heightmap(file, world);
}

/******************************
 * Serialize version 1 of the world file format.
 *
 * Version 1 contains the width and depth of the world .
 */
static world_error_t serialize_v1(
    FILE *file,
    const world_state_t *world)
{
    uint32_t cell_width;
    uint32_t cell_depth;
    uint64_t cell_count;
    uint64_t i;
    world_error_t error;

    if (world->width == 0 || world->depth == 0) {
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

    cell_count = (uint64_t)cell_width * cell_depth;

    error = write_uint32_be(file, world->width);

    if (error != WORLD_OK) {
        return error;
    }

    error = write_uint32_be(file, world->depth);

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

    error = write_uint32_be(
        file,
        world->heightmap.cell_size);

    if (error != WORLD_OK) {
        return error;
    }

    error = write_int32_be(
        file,
        world->heightmap.min_height);

    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < cell_count; i++) {
        error = write_uint32_be(
            file,
            world->heightmap.values[i]);

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}

/******************************
 * Load the world from a file.
 *
 * Dispatches to the appropriate deserialization function
 * based on the version stored in the file.
 */
world_error_t world_load(
    const char *filename,
    world_state_t *world)
{
    FILE *file;
    char magic[4];
    uint32_t version;
    world_error_t error;

    /* initialize the world state to zero */
    *world = (world_state_t){0};

    file = fopen(filename, "rb");
    if (file == NULL) {
        return WORLD_ERROR_FILE;
    }

    if (fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        fclose(file);
        return WORLD_ERROR_TRUNCATED;
    }

    if (memcmp(magic, WORLD_MAGIC, sizeof(magic)) != 0) {
        fclose(file);
        return WORLD_ERROR_INVALID_MAGIC;
    }

    error = read_uint32_be(file, &version);
    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    world->format_version = version;

    switch (version) {
        case 0:
            error = deserialize_v0(file);
            break;

        case 1:
            error = deserialize_v1(file, world);
            break;

        default:
            error = WORLD_ERROR_UNSUPPORTED_VERSION;
            break;
    }

    fclose(file);

    return error;
}

/******************************
 * Serialize the world to a file.
 *
 * Always writes the current format version to the file 
 * whatever the version of the input file
 */
world_error_t world_serialize(
    const char *filename,
    const world_state_t *world)
{
    FILE *file;
    world_error_t error;

    file = fopen(filename, "wb");

    if (file == NULL) {
        return WORLD_ERROR_FILE;
    }

    if (fwrite(
            WORLD_MAGIC,
            1,
            sizeof(WORLD_MAGIC) - 1,
            file) != sizeof(WORLD_MAGIC) - 1) {
        fclose(file);
        return WORLD_ERROR_FILE;
    }

    error = write_uint32_be(
        file,
        1);

    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    error = serialize_v1(
        file,
        world);

    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    if (fclose(file) != 0) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

/* ******************************
 * Get a string representation of a world error code.
 */
const char *world_error_string(world_error_t error)
{
    switch (error) {
        case WORLD_OK:
            return "success";

        case WORLD_ERROR_FILE:
            return "file error";

        case WORLD_ERROR_INVALID_MAGIC:
            return "invalid world magic";

        case WORLD_ERROR_TRUNCATED:
            return "truncated world data";

        case WORLD_ERROR_UNSUPPORTED_VERSION:
            return "unsupported world format version";

        case WORLD_ERROR_INVALID_FORMAT:
            return "invalid world format";

        default:
            return "unknown world error";
    }
}

/*******************************
 * Destroy the world state and free any allocated memory.
 */
void world_destroy(world_state_t *world)
{
    free(world->heightmap.values);

    world->heightmap.values = NULL;
    world->heightmap.cell_size = 0;
    world->heightmap.min_height = 0;
}