#ifndef WORLD_IO_H
#define WORLD_IO_H

#include <stdint.h>
#include <stdio.h>

#include "world.h"

world_error_t world_io_read_uint32_be(
    FILE *file,
    uint32_t *value);

world_error_t world_io_read_int32_be(
    FILE *file,
    int32_t *value);

world_error_t world_io_write_uint32_be(
    FILE *file,
    uint32_t value);

world_error_t world_io_write_int32_be(
    FILE *file,
    int32_t value);

#endif