#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#define WORLD_MAGIC "CWLD"
#define WORLD_CURRENT_VERSION 3

#define WORLD_LAYER_MAGIC "_LYR"
#define WORLD_LAYER_TYPE_HEIGHTMAP "TYPE_HEIGHTMAP"
#define WORLD_LAYER_NAME_HEIGHTMAP "LYR_HEIGHTMAP"
#define WORLD_LAYER_TYPE_STATICWATER "TYPE_STATICWATER"
#define WORLD_LAYER_NAME_STATICWATER "LYR_STATICWATER"
#define WORLD_LAYER_EVOLUTION_NONE "EV_N"
#define WORLD_LAYER_STORAGE_DENSE "ST_D"

#define WORLD_MODULARITY_CLOSED_VALUE "CLOSED"
#define WORLD_MODULARITY_MODULAR_VALUE "MODULAR"

#define WORLD_LAYER_CLOCK_NOEV "CLK_NOEV"
#define WORLD_LAYER_CLOCK_PREFIX "CLK_"

/* Distances are millimetres. One world tick is one millisecond. */
#define WORLD_DISTANCE_UNIT "mm"
#define WORLD_TICK_UNIT "ms"

typedef uint64_t world_tick_t;

typedef enum {
    WORLD_OK = 0,
    WORLD_ERROR_FILE,
    WORLD_ERROR_INVALID_MAGIC,
    WORLD_ERROR_TRUNCATED,
    WORLD_ERROR_UNSUPPORTED_VERSION,
    WORLD_ERROR_INVALID_FORMAT
} world_error_t;

typedef enum {
    WORLD_CLOCK_NOEV,
    WORLD_CLOCK_DIVISOR
} world_clock_mode_t;

typedef struct {
    world_clock_mode_t mode;
    uint16_t exponent;
} world_clock_t;

typedef enum {
    WORLD_MODULARITY_CLOSED,
    WORLD_MODULARITY_MODULAR
} world_modularity_t;

typedef enum {
    WORLD_LAYER_HEIGHTMAP,
    WORLD_LAYER_STATICWATER
} world_layer_type_t;

typedef struct {
    uint32_t cell_size;
    int32_t min_height;
    uint32_t *values;
} world_heightmap_payload_t;

typedef struct {
    uint32_t cell_size;
    int32_t min_depth;
    uint32_t *values;
} world_staticwater_payload_t;

typedef struct {
    world_layer_type_t type;
    world_clock_t clock;
    world_tick_t last_simulation_tick;
    void *payload;
} world_layer_t;

typedef struct {
    uint32_t format_version;
    uint32_t width;
    uint32_t depth;
    world_modularity_t modularity;
    world_tick_t age;
    uint32_t layer_count;
    world_layer_t **layers;
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