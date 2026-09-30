#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include <stdint.h>

#define WORLD_MAGIC "CWLD"
#define WORLD_CURRENT_VERSION 4

#define WORLD_LAYER_MAGIC "_LYR"
#define WORLD_INDIVIDUAL_MAGIC "_IND"
#define WORLD_LAYER_TYPE_HEIGHTMAP "TYPE_HEIGHTMAP"
#define WORLD_LAYER_NAME_HEIGHTMAP "LYR_HEIGHTMAP"
#define WORLD_LAYER_TYPE_STATICWATER "TYPE_STATICWATER"
#define WORLD_LAYER_NAME_STATICWATER "LYR_STATICWATER"
#define WORLD_LAYER_TYPE_DIFFLIGHT "TYPE_DIFFLIGHT"
#define WORLD_LAYER_NAME_DIFFLIGHT "LYR_DIFFLIGHT"
#define WORLD_LAYER_TYPE_HUMIDITY "TYPE_HUMIDITY"
#define WORLD_LAYER_NAME_HUMIDITY "LYR_HUMIDITY"
#define WORLD_LAYER_TYPE_FERTILITY "TYPE_FERTILITY"
#define WORLD_LAYER_NAME_FERTILITY "LYR_FERTILITY"
#define WORLD_LAYER_TYPE_GRASS "TYPE_GRASS"
#define WORLD_LAYER_NAME_GRASS "LYR_GRASS"
#define WORLD_LAYER_TYPE_GRASSAGE "TYPE_GRASSAGE"
#define WORLD_LAYER_NAME_GRASSAGE "LYR_GRASSAGE"
#define WORLD_LAYER_TYPE_INDIVIDUAL "TYPE_INDIVIDUAL"
#define WORLD_LAYER_NAME_INDIVIDUAL "LYR_INDIVIDUAL"
#define WORLD_STORAGE_DEPTH_FUNCTIONAL_VALUE "FUNCTIONAL"
#define WORLD_STORAGE_DEPTH_DEEP_VALUE "DEEP"
#define WORLD_LAYER_EVOLUTION_NONE "EV_N"
#define WORLD_LAYER_STORAGE_DENSE "ST_D"
#define WORLD_LAYER_STORAGE_U8 "ST_8"
#define WORLD_LAYER_STORAGE_U16 "ST16"

/* Species string field on disk (TYPE_INDIVIDUAL). */
#define WORLD_SPECIES_FIELD_BYTES 32
/* Storage-depth string field on disk. */
#define WORLD_STORAGE_DEPTH_FIELD_BYTES 16

/* Fertility, grass, and humidity use a 10 cm cell. */
#define WORLD_U8_CELL_MM 100
/* Inclusive stored ranges. The lower bound is 0. */
#define WORLD_U8_MAX 255
#define WORLD_U16_MAX 65535

/* Orthogonal slope buffer: east, west, north, south per cell. */
#define WORLD_SLOPE_DIRS 4

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
    WORLD_MODULARITY_MODULAR    /* opposite edges meet when a layer asks for a neighbour */
} world_modularity_t;

typedef enum {
    WORLD_LAYER_HEIGHTMAP,      /* terrain elevation */
    WORLD_LAYER_STATICWATER,   /* water depth that does not move */
    WORLD_LAYER_DIFFLIGHT,      /* diffuse daylight irradiance */
    WORLD_LAYER_HUMIDITY,       /* 0 dry, 65535 saturated */
    WORLD_LAYER_FERTILITY,      /* 0 sterile, 255 maximum fertility */
    WORLD_LAYER_GRASS,          /* millimetres of height, 0 bare, 255 maximum */
    WORLD_LAYER_GRASSAGE,       /* age in days, 0 dead, 255 dies of old age */
    WORLD_LAYER_INDIVIDUAL      /* static species; one record per being */
} world_layer_type_t;

typedef enum {
    WORLD_STORAGE_DEPTH_FUNCTIONAL, /* pose + basic behaviour params */
    WORLD_STORAGE_DEPTH_DEEP        /* FUNCTIONAL plus mind; not used yet */
} world_storage_depth_t;

/*
 * One individual in a TYPE_INDIVIDUAL layer (FUNCTIONAL pose record).
 * Orientation is milliradians: 0 = east (+X), positive CCW about +Z.
 * Species-specific layouts live in world_species_*.h.
 */
typedef struct {
    uint32_t id;                /* serial; nonzero */
    int32_t x_mm;
    int32_t y_mm;
    int32_t z_mm;
    int32_t orientation_mrad;
} world_individual_t;

typedef struct {
    char species[WORLD_SPECIES_FIELD_BYTES];
    world_storage_depth_t depth;
    uint32_t species_version;
    uint32_t count;
    world_individual_t *individuals;
} world_individual_payload_t;

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
    /*
     * Orthogonal rise of published, four values per cell (E,W,N,S).
     * Not in the file. Rebuilt when the published grid is filled
     * or swapped. A missing neighbour is 0.
     */
    int64_t *slope;
} world_heightmap_payload_t;

typedef struct {
    uint32_t cell_size;     /* millimetres; same grid as the heightmap */
    int32_t min_depth;      /* millimetres; depth = min_depth + cell */
    uint32_t *published;    /* 0 is dry ground */
    uint32_t *pending;
    int64_t *slope;         /* same layout as the heightmap slope buffer */
} world_staticwater_payload_t;

typedef struct {
    uint32_t cell_size;         /* millimetres; need not match the terrain */
    uint32_t max_irradiance;    /* W/m²; a cell value of 0 is night */
    uint32_t *published;        /* current irradiance, W/m², not an offset */
    uint32_t *pending;
} world_difflight_payload_t;

/*
 * Fertility, grass height, and grass age. Each cell is one uint8.
 * Fertility: 0 is sterile, 255 is the maximum.
 * Grass: the value is millimetres of height, from 0 to 255.
 * Grass age: the value is days of this layer's clock, from 0 to 255.
 * cell_size is WORLD_U8_CELL_MM.
 */
typedef struct {
    uint32_t cell_size;
    uint8_t *published;
    uint8_t *pending;
} world_u8_payload_t;

/*
 * Humidity. 0 is dry, 65535 is saturated.
 * cell_size is WORLD_U8_CELL_MM. The minimum field on disk is 0
 * and is not added to the cell.
 */
typedef struct {
    uint32_t cell_size;
    uint16_t *published;
    uint16_t *pending;
} world_u16_payload_t;

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
    world_layer_t **layers;     /* at most one of each dense type; many TYPE_INDIVIDUAL */
} world_state_t;

/* Read any supported version. Older files gain the defaults of later ones. */
world_error_t world_load(
    const char *filename,
    world_state_t *world);

/* Write version 4. The file stores published grids and individual lists. */
world_error_t world_serialize(
    const char *filename,
    const world_state_t *world);

const char *world_error_string(world_error_t error);

/* Free every layer, including both cell buffers. */
void world_destroy(world_state_t *world);

/* Inclusive bounds. Unsigned stored cells have lower bound 0. */
uint8_t world_clamp_u8(int64_t value);
uint16_t world_clamp_u16(int64_t value);
uint32_t world_clamp_u32(int64_t value, uint32_t hi);

/*
 * Index of the cell that contains this point, on a grid of cell_size
 * that covers the world. Row-major, row times the column count plus
 * column. The point is in world millimetres from the south-west corner.
 * Modularity does not apply: a point outside the map has no cell.
 * index is left unchanged when the function returns false.
 */
bool world_cell_at(
    uint32_t world_width,
    uint32_t world_depth,
    uint32_t cell_size,
    uint32_t east_mm,
    uint32_t north_mm,
    uint32_t *index);

/*
 * Published cell of this layer that contains (east_mm, north_mm).
 * The integer is the stored cell, widened: heightmap and static water
 * offsets, daylight irradiance, humidity 0..65535, fertility and grass
 * 0..255. It does not add min_height or min_depth. Callers interpret.
 * Reads published only. value is left unchanged when the function
 * returns false.
 */
bool world_layer_value(
    const world_state_t *world,
    const world_layer_t *layer,
    uint32_t east_mm,
    uint32_t north_mm,
    int64_t *value);

typedef enum {
    WORLD_DIR_EAST,
    WORLD_DIR_WEST,
    WORLD_DIR_NORTH,
    WORLD_DIR_SOUTH
} world_direction_t;

/*
 * Stored-cell rise from the cell that contains this point to its
 * orthogonal neighbour on this layer, and the millimetres between
 * those cell centres (the layer's cell_size). Same integer as
 * world_layer_value; min_height is not added. CLOSED has no neighbour
 * past the border. rise and run_mm are left unchanged when the
 * function returns false.
 */
bool world_layer_gradient(
    const world_state_t *world,
    const world_layer_t *layer,
    uint32_t east_mm,
    uint32_t north_mm,
    world_direction_t direction,
    int64_t *rise,
    uint32_t *run_mm);

/*
 * Orthogonal neighbour on one layer's grid. columns and rows are
 * that layer's own counts, because cell sizes differ. The step is
 * in cells, usually +1 or −1 on a single axis.
 *
 * MODULAR folds the step onto the opposite edge, as many times as
 * the step requires. CLOSED has no cell past the border and returns
 * false. index is left unchanged when the function returns false.
 */
bool world_neighbor(
    const world_state_t *world,
    uint32_t columns,
    uint32_t rows,
    uint32_t column,
    uint32_t row,
    int delta_column,
    int delta_row,
    uint32_t *index);

/*
 * Future helpers (declared for the format; behaviour not wired yet).
 *
 * world_individual_alloc_id — unused serial for a species layer
 *   (e.g. max(existing)+1).
 * world_individual_surface_z_mm — heightmap elevation at (x,y) for a
 *   being that walks on the surface.
 */
uint32_t world_individual_alloc_id(
    const world_state_t *world,
    const char *species);

bool world_individual_surface_z_mm(
    const world_state_t *world,
    int32_t x_mm,
    int32_t y_mm,
    int32_t *z_mm);

#endif