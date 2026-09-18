#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#define WORLD_MAGIC "CWLD"

typedef enum {
    WORLD_OK = 0,
    WORLD_ERROR_FILE,
    WORLD_ERROR_INVALID_MAGIC,
    WORLD_ERROR_TRUNCATED,
    WORLD_ERROR_UNSUPPORTED_VERSION,
    WORLD_ERROR_INVALID_FORMAT
} world_error_t;

typedef struct {
    uint32_t format_version;
    uint32_t width;
    uint32_t depth;
} world_state_t;

world_error_t world_load(
    const char *filename,
    world_state_t *world);

world_error_t world_serialize(
    const char *filename,
    const world_state_t *world);

const char *world_error_string(world_error_t error);
#endif