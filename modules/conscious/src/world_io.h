#ifndef WORLD_IO_H
#define WORLD_IO_H

#include <stdint.h>
#include <stdio.h>

#include "world.h"

/* Multi-byte integers in a world file are big-endian. Strings are fixed width and are not terminated. */

world_error_t world_io_read_uint16_be(
    FILE *file,
    uint16_t *value);

world_error_t world_io_read_uint32_be(
    FILE *file,
    uint32_t *value);

world_error_t world_io_read_uint64_be(
    FILE *file,
    uint64_t *value);

world_error_t world_io_read_int32_be(
    FILE *file,
    int32_t *value);

world_error_t world_io_write_uint16_be(
    FILE *file,
    uint16_t value);

world_error_t world_io_write_uint32_be(
    FILE *file,
    uint32_t value);

world_error_t world_io_write_uint64_be(
    FILE *file,
    uint64_t value);

world_error_t world_io_write_int32_be(
    FILE *file,
    int32_t value);

world_error_t world_io_read_fixed_string(
    FILE *file,
    char *buffer,
    size_t size);

#endif