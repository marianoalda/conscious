#include <stdio.h>
#include <string.h>

#include "world.h"

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
 * Serialize version 0 of the world file format.
 */
static int serialize_v0(
    FILE *file,
    const world_state_t *world)
{
    if (write_uint32_be(file, world->format_version) != 0) {
        return -1;
    }

    return 0;
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
    world_error_t error;

    error = read_uint32_be(file, &world->width);
    if (error != WORLD_OK) {
        return error;
    }

    error = read_uint32_be(file, &world->depth);
    if (error != WORLD_OK) {
        return error;
    }

    return WORLD_OK;
}

/******************************
 * Serialize version 1 of the world file format.
 *
 * Version 1 contains the width and depth of the world.
 */
static int serialize_v1(
    FILE *file,
    const world_state_t *world)
{
    if (write_uint32_be(file, world->width) != 0) {
        return -1;
    }

    if (write_uint32_be(file, world->depth) != 0) {
        return -1;
    }

    return 0;
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
 * Dispatches to the appropriate serialization function
 * based on the format version stored in the world state.
 */
world_error_t world_serialize(
    const char *filename,
    const world_state_t *world)
{
    FILE *file;
    int result;

    file = fopen(filename, "wb");
    if (file == NULL) {
        return -1;
    }

    if (fwrite(WORLD_MAGIC, 1, sizeof(WORLD_MAGIC) - 1, file) !=
        sizeof(WORLD_MAGIC) - 1) {
        fclose(file);
        return -1;
    }

    switch (world->format_version) {
        case 0:
            result = serialize_v0(file, world);
            break;

        case 1:
            result = serialize_v1(file, world);
            break;
            
        default:
            result = -1;
            break;
    }

    fclose(file);

    return result;
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