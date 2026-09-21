#include "world_io.h"

world_error_t world_io_read_uint32_be(
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

world_error_t world_io_read_int32_be(
    FILE *file,
    int32_t *value)
{
    uint32_t raw;
    world_error_t error;

    error = world_io_read_uint32_be(file, &raw);

    if (error != WORLD_OK) {
        return error;
    }

    *value = (int32_t)raw;

    return WORLD_OK;
}

world_error_t world_io_write_uint32_be(
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

world_error_t world_io_write_int32_be(
    FILE *file,
    int32_t value)
{
    return world_io_write_uint32_be(
        file,
        (uint32_t)value);
}

world_error_t world_io_read_fixed_string(
    FILE *file,
    char *buffer,
    size_t size)
{
    if (fread(buffer, 1, size, file) != size) {
        return WORLD_ERROR_TRUNCATED;
    }

    return WORLD_OK;
}

