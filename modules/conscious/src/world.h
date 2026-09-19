#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#define WORLD_MAGIC "CWLD"
#define WORLD_LAYER_MAGIC "_LYR"
#define WORLD_LAYER_TYPE_HEIGHTMAP "TYPE_HEIGHTMAP"
#define WORLD_LAYER_NAME_HEIGHTMAP "LYR_HEIGHTMAP"
#define WORLD_LAYER_EVOLUTION_NONE "EV_N"
#define WORLD_LAYER_STORAGE_DENSE "ST_D"

typedef enum {
    WORLD_OK = 0,
    WORLD_ERROR_FILE,
    WORLD_ERROR_INVALID_MAGIC,
    WORLD_ERROR_TRUNCATED,
    WORLD_ERROR_UNSUPPORTED_VERSION,
    WORLD_ERROR_INVALID_FORMAT
} world_error_t;

typedef struct {
    uint32_t cell_size;
    int32_t min_height;
    uint32_t *values;
} world_heightmap_t;

typedef struct {
    uint32_t format_version;
    uint32_t width;
    uint32_t depth;
    world_heightmap_t heightmap;
} world_state_t;

world_error_t world_load(
    const char *filename,
    world_state_t *world);

world_error_t world_serialize(
    const char *filename,
    const world_state_t *world);

const char *world_error_string(world_error_t error);

void world_destroy(world_state_t *world);

#endif