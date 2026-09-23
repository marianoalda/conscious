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
#define WORLD_LAYER_TYPE_DIFFLIGHT "TYPE_DIFFLIGHT"
#define WORLD_LAYER_NAME_DIFFLIGHT "LYR_DIFFLIGHT"
#define WORLD_LAYER_EVOLUTION_NONE "EV_N"
#define WORLD_LAYER_STORAGE_DENSE "ST_D"

#define WORLD_MODULARITY_CLOSED_VALUE "CLOSED"
#define WORLD_MODULARITY_MODULAR_VALUE "MODULAR"

#define WORLD_LAYER_CLOCK_NOEV "CLK_NOEV"
#define WORLD_LAYER_CLOCK_PREFIX "CLK_"

/* Distances are millimetres. One world tick is one millisecond. */
#define WORLD_DISTANCE_UNIT "mm"
#define WORLD_TICK_UNIT "ms"
#define WORLD_IRRADIANCE_UNIT "W/m2"

/* Absolute world time. One tick is one millisecond. */
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
    WORLD_CLOCK_NOEV,       /* the layer is never simulated */
    WORLD_CLOCK_DIVISOR     /* simulated every 2^exponent ticks */
} world_clock_mode_t;

typedef struct {
    world_clock_mode_t mode;
    uint16_t exponent;      /* period = 2^exponent milliseconds */
} world_clock_t;

typedef enum {
    WORLD_MODULARITY_CLOSED,    /* edges are borders */
    WORLD_MODULARITY_MODULAR    /* opposite edges meet; not applied yet */
} world_modularity_t;

typedef enum {
    WORLD_LAYER_HEIGHTMAP,      /* terrain elevation */
    WORLD_LAYER_STATICWATER,   /* water depth that does not move */
    WORLD_LAYER_DIFFLIGHT       /* diffuse daylight irradiance */
} world_layer_type_t;

/*
 * A dense grid keeps two cell buffers of the same size.
 * published is what other layers, the viewer and the file see.
 * pending is reserved for the tick being calculated. Readers keep
 * using published until the tick publishes pending in its place.
 * On disk only published is stored.
 */
typedef struct {
    uint32_t cell_size;     /* millimetres; divides width and depth */
    int32_t min_height;     /* millimetres; elevation = min_height + cell */
    uint32_t *published;
    uint32_t *pending;
} world_heightmap_payload_t;

typedef struct {
    uint32_t cell_size;     /* millimetres; same grid as the heightmap */
    int32_t min_depth;      /* millimetres; depth = min_depth + cell */
    uint32_t *published;    /* 0 is dry ground */
    uint32_t *pending;
} world_staticwater_payload_t;

typedef struct {
    uint32_t cell_size;         /* millimetres; need not match the terrain */
    uint32_t max_irradiance;    /* W/m²; a cell value of 0 is night */
    uint32_t *published;        /* current irradiance, W/m², not an offset */
    uint32_t *pending;
} world_difflight_payload_t;

typedef struct {
    world_layer_type_t type;
    world_clock_t clock;
    world_tick_t last_simulation_tick;  /* tick of the last published update */
    void *payload;                      /* type-specific grid */
} world_layer_t;

typedef struct {
    uint32_t format_version;    /* version read from the file */
    uint32_t width;             /* east-west extent, millimetres */
    uint32_t depth;             /* north-south extent, millimetres */
    world_modularity_t modularity;
    world_tick_t age;           /* milliseconds since tick 0, which is 00:00 */
    uint32_t layer_count;
    world_layer_t **layers;     /* one layer of each type at most */
} world_state_t;

/* Read any supported version. Older files gain the defaults of later ones. */
world_error_t world_load(
    const char *filename,
    world_state_t *world);

/* Write version 3. The file stores published grids only. */
world_error_t world_serialize(
    const char *filename,
    const world_state_t *world);

const char *world_error_string(world_error_t error);

/* Free every layer, including both cell buffers. */
void world_destroy(world_state_t *world);

#endif