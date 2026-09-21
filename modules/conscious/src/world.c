#include "world.h"
#include "world_io.h"
#include "world_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    /* initialize the world state to backwards compatibility defaults */
    world_initialize_defaults(world);

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

    error = world_io_read_uint32_be(file, &version);
    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    world->format_version = version;

    switch (version) {
        case 0:
            error = world_v0_load(file, world);
            break;

        case 1:
            error = world_v1_load(file, world);
            break;

        case 2:
            error = world_v2_load(file, world);
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

    error = world_io_write_uint32_be(
        file,
        WORLD_CURRENT_VERSION);

    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    error = world_v2_serialize(
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

void world_initialize_defaults(
    world_state_t *world)
{
    *world = (world_state_t){0};

    world->modularity = WORLD_MODULARITY_CLOSED;

    world->heightmap.clock.mode =
        WORLD_CLOCK_DIVISOR;

    world->heightmap.clock.exponent = 0;
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

