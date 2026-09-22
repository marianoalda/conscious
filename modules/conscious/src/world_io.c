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

world_error_t world_io_read_uint64_be(
    FILE *file,
    uint64_t *value)
{
    unsigned char buffer[8];

    if (fread(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_TRUNCATED;
    }

    *value =
        ((uint64_t)buffer[0] << 56) |
        ((uint64_t)buffer[1] << 48) |
        ((uint64_t)buffer[2] << 40) |
        ((uint64_t)buffer[3] << 32) |
        ((uint64_t)buffer[4] << 24) |
        ((uint64_t)buffer[5] << 16) |
        ((uint64_t)buffer[6] << 8) |
        (uint64_t)buffer[7];

    return WORLD_OK;
}

world_error_t world_io_write_uint64_be(
    FILE *file,
    uint64_t value)
{
    unsigned char buffer[8];

    buffer[0] = (unsigned char)(value >> 56);
    buffer[1] = (unsigned char)(value >> 48);
    buffer[2] = (unsigned char)(value >> 40);
    buffer[3] = (unsigned char)(value >> 32);
    buffer[4] = (unsigned char)(value >> 24);
    buffer[5] = (unsigned char)(value >> 16);
    buffer[6] = (unsigned char)(value >> 8);
    buffer[7] = (unsigned char)value;

    if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
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

