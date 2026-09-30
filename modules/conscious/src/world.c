#include "world.h"
#include "world_io.h"
#include "world_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    /* initialize the world state to backwards compatibility defaults */
    world_initialize_defaults(world);

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

    error = world_io_read_uint32_be(file, &version);
    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    world->format_version = version;

    switch (version) {
        case 0:
            error = world_v0_load(file, world);
            break;

        case 1:
            error = world_v1_load(file, world);
            break;

        case 2:
            error = world_v2_load(file, world);
            break;

        case 3:
            error = world_v3_load(file, world);
            break;

        case 4:
            error = world_v4_load(file, world);
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
 * Always writes the current format version to the file 
 * whatever the version of the input file
 */
world_error_t world_serialize(
    const char *filename,
    const world_state_t *world)
{
    FILE *file;
    world_error_t error;

    file = fopen(filename, "wb");

    if (file == NULL) {
        return WORLD_ERROR_FILE;
    }

    if (fwrite(
            WORLD_MAGIC,
            1,
            sizeof(WORLD_MAGIC) - 1,
            file) != sizeof(WORLD_MAGIC) - 1) {
        fclose(file);
        return WORLD_ERROR_FILE;
    }

    error = world_io_write_uint32_be(
        file,
        WORLD_CURRENT_VERSION);

    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    error = world_v4_serialize(
        file,
        world);

    if (error != WORLD_OK) {
        fclose(file);
        return error;
    }

    if (fclose(file) != 0) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
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

void world_initialize_defaults(
    world_state_t *world)
{
    *world = (world_state_t){0};

    world->modularity = WORLD_MODULARITY_CLOSED;
}

static void world_free_layer(world_layer_t *layer)
{
    if (layer == NULL) {
        return;
    }

    if (layer->payload != NULL) {
        if (layer->type == WORLD_LAYER_HEIGHTMAP) {
            world_heightmap_payload_t *heightmap = layer->payload;

            free(heightmap->published);
            free(heightmap->pending);
            free(heightmap->slope);
        } else if (layer->type == WORLD_LAYER_STATICWATER) {
            world_staticwater_payload_t *water = layer->payload;

            free(water->published);
            free(water->pending);
            free(water->slope);
        } else if (layer->type == WORLD_LAYER_DIFFLIGHT) {
            world_difflight_payload_t *light = layer->payload;

            free(light->published);
            free(light->pending);
        } else if (layer->type == WORLD_LAYER_HUMIDITY) {
            world_u16_payload_t *grid = layer->payload;

            free(grid->published);
            free(grid->pending);
        } else if (layer->type == WORLD_LAYER_FERTILITY ||
                   layer->type == WORLD_LAYER_GRASS ||
                   layer->type == WORLD_LAYER_GRASSAGE) {
            world_u8_payload_t *grid = layer->payload;

            free(grid->published);
            free(grid->pending);
        } else if (layer->type == WORLD_LAYER_INDIVIDUAL) {
            world_individual_payload_t *payload = layer->payload;

            free(payload->individuals);
        }

        free(layer->payload);
    }

    free(layer);
}

static world_error_t world_append_layer(
    world_state_t *world,
    world_layer_type_t type,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    void *payload)
{
    world_layer_t *layer;
    world_layer_t **grown;

    if (type != WORLD_LAYER_INDIVIDUAL &&
        world_find_layer(world, type) != NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    layer = calloc(1, sizeof(*layer));

    if (layer == NULL) {
        return WORLD_ERROR_FILE;
    }

    grown = realloc(
        world->layers,
        (world->layer_count + 1) * sizeof(*grown));

    if (grown == NULL) {
        free(layer);
        return WORLD_ERROR_FILE;
    }

    layer->type = type;
    layer->clock = clock;
    layer->last_simulation_tick = last_simulation_tick;
    layer->payload = payload;

    world->layers = grown;
    world->layers[world->layer_count] = layer;
    world->layer_count++;
    world_clamp_layer(world, layer);

    return WORLD_OK;
}

const world_layer_t *world_find_layer(
    const world_state_t *world,
    world_layer_type_t type)
{
    uint32_t i;

    if (world == NULL || world->layers == NULL) {
        return NULL;
    }

    for (i = 0; i < world->layer_count; i++) {
        if (world->layers[i] != NULL &&
            world->layers[i]->type == type) {
            return world->layers[i];
        }
    }

    return NULL;
}

/*
 * Reserve the tick buffer. It matches published in size and content,
 * so the loaded world is intact before any tick calculates into it.
 * On failure the caller still owns published.
 */
static world_error_t allocate_pending_grid(
    const world_state_t *world,
    uint32_t cell_size,
    const void *published,
    size_t value_size,
    void **pending)
{
    uint64_t cell_count;
    size_t bytes;

    if (published == NULL ||
        value_size == 0 ||
        cell_size == 0 ||
        world->width == 0 ||
        world->depth == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);

    if (cell_count > SIZE_MAX / value_size) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    bytes = (size_t)cell_count * value_size;
    *pending = malloc(bytes);

    if (*pending == NULL) {
        return WORLD_ERROR_FILE;
    }

    memcpy(*pending, published, bytes);
    return WORLD_OK;
}

static bool world_direction_delta(
    world_direction_t direction,
    int *delta_column,
    int *delta_row);

static const world_direction_t world_slope_dirs[WORLD_SLOPE_DIRS] = {
    WORLD_DIR_EAST,
    WORLD_DIR_WEST,
    WORLD_DIR_NORTH,
    WORLD_DIR_SOUTH
};

static world_error_t allocate_slope_buffer(
    const world_state_t *world,
    uint32_t cell_size,
    int64_t **slope)
{
    uint64_t cell_count;
    uint64_t values;

    if (slope == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *slope = NULL;

    if (cell_size == 0 ||
        world == NULL ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    cell_count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);
    values = cell_count * (uint64_t)WORLD_SLOPE_DIRS;

    if (values > SIZE_MAX / sizeof(int64_t)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *slope = calloc((size_t)values, sizeof(int64_t));

    if (*slope == NULL) {
        return WORLD_ERROR_FILE;
    }

    return WORLD_OK;
}

static void fill_u32_slopes(
    const world_state_t *world,
    uint32_t cell_size,
    const uint32_t *published,
    int64_t *slope)
{
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;
    int dir;

    if (world == NULL ||
        published == NULL ||
        slope == NULL ||
        cell_size == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return;
    }

    columns = world->width / cell_size;
    rows = world->depth / cell_size;

    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            uint32_t index;

            index = row * columns + column;

            for (dir = 0; dir < WORLD_SLOPE_DIRS; dir++) {
                int delta_column;
                int delta_row;
                uint32_t neighbor_index;
                uint64_t slope_index;

                slope_index =
                    (uint64_t)index * (uint64_t)WORLD_SLOPE_DIRS +
                    (uint64_t)dir;

                if (!world_direction_delta(
                        world_slope_dirs[dir],
                        &delta_column,
                        &delta_row) ||
                    !world_neighbor(
                        world,
                        columns,
                        rows,
                        column,
                        row,
                        delta_column,
                        delta_row,
                        &neighbor_index)) {
                    slope[slope_index] = 0;
                    continue;
                }

                slope[slope_index] =
                    (int64_t)published[neighbor_index] -
                    (int64_t)published[index];
            }
        }
    }
}

uint8_t world_clamp_u8(int64_t value)
{
    if (value < 0) {
        return 0;
    }

    if (value > (int64_t)WORLD_U8_MAX) {
        return (uint8_t)WORLD_U8_MAX;
    }

    return (uint8_t)value;
}

uint16_t world_clamp_u16(int64_t value)
{
    if (value < 0) {
        return 0;
    }

    if (value > (int64_t)WORLD_U16_MAX) {
        return (uint16_t)WORLD_U16_MAX;
    }

    return (uint16_t)value;
}

uint32_t world_clamp_u32(int64_t value, uint32_t hi)
{
    if (value < 0) {
        return 0;
    }

    if (value > (int64_t)hi) {
        return hi;
    }

    return (uint32_t)value;
}

static uint64_t world_grid_cell_count(
    const world_state_t *world,
    uint32_t cell_size)
{
    if (world == NULL ||
        cell_size == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return 0;
    }

    return
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);
}

static void clamp_u32_hi(
    uint32_t *cells,
    uint64_t count,
    uint32_t hi)
{
    uint64_t i;

    if (cells == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (cells[i] > hi) {
            cells[i] = hi;
        }
    }
}

static void clamp_u16_hi(
    uint16_t *cells,
    uint64_t count,
    uint16_t hi)
{
    uint64_t i;

    if (cells == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (cells[i] > hi) {
            cells[i] = hi;
        }
    }
}

static void clamp_u8_hi(
    uint8_t *cells,
    uint64_t count,
    uint8_t hi)
{
    uint64_t i;

    if (cells == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (cells[i] > hi) {
            cells[i] = hi;
        }
    }
}

void world_clamp_layer(
    const world_state_t *world,
    world_layer_t *layer)
{
    uint64_t count;

    if (world == NULL || layer == NULL || layer->payload == NULL) {
        return;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP:
        case WORLD_LAYER_STATICWATER:
            return;

        case WORLD_LAYER_DIFFLIGHT: {
            world_difflight_payload_t *grid = layer->payload;

            count = world_grid_cell_count(world, grid->cell_size);
            clamp_u32_hi(grid->published, count, grid->max_irradiance);
            clamp_u32_hi(grid->pending, count, grid->max_irradiance);
            return;
        }

        case WORLD_LAYER_HUMIDITY: {
            world_u16_payload_t *grid = layer->payload;

            count = world_grid_cell_count(world, grid->cell_size);
            clamp_u16_hi(grid->published, count, (uint16_t)WORLD_U16_MAX);
            clamp_u16_hi(grid->pending, count, (uint16_t)WORLD_U16_MAX);
            return;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            world_u8_payload_t *grid = layer->payload;

            count = world_grid_cell_count(world, grid->cell_size);
            clamp_u8_hi(grid->published, count, (uint8_t)WORLD_U8_MAX);
            clamp_u8_hi(grid->pending, count, (uint8_t)WORLD_U8_MAX);
            return;
        }

        case WORLD_LAYER_INDIVIDUAL:
            return;
    }
}

void world_refresh_slopes(
    const world_state_t *world,
    world_layer_t *layer)
{
    if (world == NULL || layer == NULL || layer->payload == NULL) {
        return;
    }

    if (layer->type == WORLD_LAYER_HEIGHTMAP) {
        world_heightmap_payload_t *grid = layer->payload;

        fill_u32_slopes(
            world,
            grid->cell_size,
            grid->published,
            grid->slope);
        return;
    }

    if (layer->type == WORLD_LAYER_STATICWATER) {
        world_staticwater_payload_t *grid = layer->payload;

        fill_u32_slopes(
            world,
            grid->cell_size,
            grid->published,
            grid->slope);
    }
}

/*
 * Takes ownership of published when it succeeds, and allocates pending.
 * On failure the caller still owns published.
 */
world_error_t world_append_heightmap_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t min_height,
    uint32_t *published)
{
    world_heightmap_payload_t *payload;
    world_error_t error;

    payload = calloc(1, sizeof(*payload));

    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    payload->cell_size = cell_size;
    payload->min_height = min_height;
    payload->published = published;

    error = allocate_pending_grid(
        world,
        cell_size,
        published,
        sizeof(uint32_t),
        (void **)&payload->pending);

    if (error != WORLD_OK) {
        free(payload);
        return error;
    }

    error = allocate_slope_buffer(world, cell_size, &payload->slope);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload);
        return error;
    }

    error = world_append_layer(
        world,
        WORLD_LAYER_HEIGHTMAP,
        clock,
        last_simulation_tick,
        payload);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload->slope);
        free(payload);
        return error;
    }

    world_refresh_slopes(world, world->layers[world->layer_count - 1]);
    return WORLD_OK;
}

world_error_t world_append_staticwater_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    int32_t min_depth,
    uint32_t *published)
{
    world_staticwater_payload_t *payload;
    world_error_t error;

    payload = calloc(1, sizeof(*payload));

    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    payload->cell_size = cell_size;
    payload->min_depth = min_depth;
    payload->published = published;

    error = allocate_pending_grid(
        world,
        cell_size,
        published,
        sizeof(uint32_t),
        (void **)&payload->pending);

    if (error != WORLD_OK) {
        free(payload);
        return error;
    }

    error = allocate_slope_buffer(world, cell_size, &payload->slope);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload);
        return error;
    }

    error = world_append_layer(
        world,
        WORLD_LAYER_STATICWATER,
        clock,
        last_simulation_tick,
        payload);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload->slope);
        free(payload);
        return error;
    }

    world_refresh_slopes(world, world->layers[world->layer_count - 1]);
    return WORLD_OK;
}

world_error_t world_append_difflight_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint32_t max_irradiance,
    uint32_t *published)
{
    world_difflight_payload_t *payload;
    world_error_t error;

    payload = calloc(1, sizeof(*payload));

    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    payload->cell_size = cell_size;
    payload->max_irradiance = max_irradiance;
    payload->published = published;

    error = allocate_pending_grid(
        world,
        cell_size,
        published,
        sizeof(uint32_t),
        (void **)&payload->pending);

    if (error != WORLD_OK) {
        free(payload);
        return error;
    }

    error = world_append_layer(
        world,
        WORLD_LAYER_DIFFLIGHT,
        clock,
        last_simulation_tick,
        payload);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload);
        return error;
    }

    return WORLD_OK;
}

/*
 * Humidity. cell_size is 10 cm. Each cell is uint16.
 * On success the layer owns published and a pending copy.
 * On failure the caller still owns published.
 */
world_error_t world_append_u16_layer(
    world_state_t *world,
    world_layer_type_t type,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint16_t *published)
{
    world_u16_payload_t *payload;
    world_error_t error;

    if (type != WORLD_LAYER_HUMIDITY) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (cell_size != WORLD_U8_CELL_MM) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    payload = calloc(1, sizeof(*payload));

    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    payload->cell_size = cell_size;
    payload->published = published;

    error = allocate_pending_grid(
        world,
        cell_size,
        published,
        sizeof(uint16_t),
        (void **)&payload->pending);

    if (error != WORLD_OK) {
        free(payload);
        return error;
    }

    error = world_append_layer(
        world,
        type,
        clock,
        last_simulation_tick,
        payload);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload);
        return error;
    }

    return WORLD_OK;
}

/*
 * Fertility, grass height, and grass age. cell_size is 10 cm.
 * On success the layer owns published and a pending copy.
 * On failure the caller still owns published.
 */
world_error_t world_append_u8_layer(
    world_state_t *world,
    world_layer_type_t type,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    uint32_t cell_size,
    uint8_t *published)
{
    world_u8_payload_t *payload;
    world_error_t error;

    if (type != WORLD_LAYER_FERTILITY &&
        type != WORLD_LAYER_GRASS &&
        type != WORLD_LAYER_GRASSAGE) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (cell_size != WORLD_U8_CELL_MM) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    payload = calloc(1, sizeof(*payload));

    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    payload->cell_size = cell_size;
    payload->published = published;

    error = allocate_pending_grid(
        world,
        cell_size,
        published,
        sizeof(uint8_t),
        (void **)&payload->pending);

    if (error != WORLD_OK) {
        free(payload);
        return error;
    }

    error = world_append_layer(
        world,
        type,
        clock,
        last_simulation_tick,
        payload);

    if (error != WORLD_OK) {
        free(payload->pending);
        free(payload);
        return error;
    }

    return WORLD_OK;
}

bool world_cell_at(
    uint32_t world_width,
    uint32_t world_depth,
    uint32_t cell_size,
    uint32_t east_mm,
    uint32_t north_mm,
    uint32_t *index)
{
    uint32_t columns;
    uint32_t rows;
    uint32_t column;
    uint32_t row;

    if (index == NULL ||
        cell_size == 0 ||
        east_mm >= world_width ||
        north_mm >= world_depth ||
        world_width % cell_size != 0 ||
        world_depth % cell_size != 0) {
        return false;
    }

    columns = world_width / cell_size;
    rows = world_depth / cell_size;
    column = east_mm / cell_size;
    row = north_mm / cell_size;

    if (column >= columns || row >= rows) {
        return false;
    }

    *index = row * columns + column;
    return true;
}

bool world_layer_value(
    const world_state_t *world,
    const world_layer_t *layer,
    uint32_t east_mm,
    uint32_t north_mm,
    int64_t *value)
{
    uint32_t cell_size;
    uint32_t index;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL ||
        value == NULL) {
        return false;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            const world_heightmap_payload_t *grid = layer->payload;

            if (grid->published == NULL) {
                return false;
            }

            cell_size = grid->cell_size;
            if (!world_cell_at(
                    world->width,
                    world->depth,
                    cell_size,
                    east_mm,
                    north_mm,
                    &index)) {
                return false;
            }

            *value = (int64_t)grid->published[index];
            return true;
        }

        case WORLD_LAYER_STATICWATER: {
            const world_staticwater_payload_t *grid = layer->payload;

            if (grid->published == NULL) {
                return false;
            }

            cell_size = grid->cell_size;
            if (!world_cell_at(
                    world->width,
                    world->depth,
                    cell_size,
                    east_mm,
                    north_mm,
                    &index)) {
                return false;
            }

            *value = (int64_t)grid->published[index];
            return true;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            const world_difflight_payload_t *grid = layer->payload;

            if (grid->published == NULL) {
                return false;
            }

            cell_size = grid->cell_size;
            if (!world_cell_at(
                    world->width,
                    world->depth,
                    cell_size,
                    east_mm,
                    north_mm,
                    &index)) {
                return false;
            }

            *value = (int64_t)grid->published[index];
            return true;
        }

        case WORLD_LAYER_HUMIDITY: {
            const world_u16_payload_t *grid = layer->payload;

            if (grid->published == NULL) {
                return false;
            }

            cell_size = grid->cell_size;
            if (!world_cell_at(
                    world->width,
                    world->depth,
                    cell_size,
                    east_mm,
                    north_mm,
                    &index)) {
                return false;
            }

            *value = (int64_t)grid->published[index];
            return true;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            const world_u8_payload_t *grid = layer->payload;

            if (grid->published == NULL) {
                return false;
            }

            cell_size = grid->cell_size;
            if (!world_cell_at(
                    world->width,
                    world->depth,
                    cell_size,
                    east_mm,
                    north_mm,
                    &index)) {
                return false;
            }

            *value = (int64_t)grid->published[index];
            return true;
        }

        case WORLD_LAYER_INDIVIDUAL:
            return false;
    }

    return false;
}

static bool world_layer_cell_size(
    const world_layer_t *layer,
    uint32_t *cell_size)
{
    if (layer == NULL || layer->payload == NULL || cell_size == NULL) {
        return false;
    }

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            const world_heightmap_payload_t *grid = layer->payload;

            *cell_size = grid->cell_size;
            break;
        }

        case WORLD_LAYER_STATICWATER: {
            const world_staticwater_payload_t *grid = layer->payload;

            *cell_size = grid->cell_size;
            break;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            const world_difflight_payload_t *grid = layer->payload;

            *cell_size = grid->cell_size;
            break;
        }

        case WORLD_LAYER_HUMIDITY: {
            const world_u16_payload_t *grid = layer->payload;

            *cell_size = grid->cell_size;
            break;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            const world_u8_payload_t *grid = layer->payload;

            *cell_size = grid->cell_size;
            break;
        }

        case WORLD_LAYER_INDIVIDUAL:
            return false;

        default:
            return false;
    }

    return *cell_size != 0;
}

static bool world_direction_delta(
    world_direction_t direction,
    int *delta_column,
    int *delta_row)
{
    if (delta_column == NULL || delta_row == NULL) {
        return false;
    }

    switch (direction) {
        case WORLD_DIR_EAST:
            *delta_column = 1;
            *delta_row = 0;
            return true;

        case WORLD_DIR_WEST:
            *delta_column = -1;
            *delta_row = 0;
            return true;

        case WORLD_DIR_NORTH:
            *delta_column = 0;
            *delta_row = 1;
            return true;

        case WORLD_DIR_SOUTH:
            *delta_column = 0;
            *delta_row = -1;
            return true;
    }

    return false;
}

bool world_layer_gradient(
    const world_state_t *world,
    const world_layer_t *layer,
    uint32_t east_mm,
    uint32_t north_mm,
    world_direction_t direction,
    int64_t *rise,
    uint32_t *run_mm)
{
    uint32_t cell_size;
    uint32_t index;
    uint32_t columns;
    uint32_t rows;
    uint32_t column;
    uint32_t row;
    uint32_t neighbor_index;
    uint32_t neighbor_column;
    uint32_t neighbor_row;
    uint32_t neighbor_east_mm;
    uint32_t neighbor_north_mm;
    int delta_column;
    int delta_row;
    int64_t here;
    int64_t neighbor;

    if (world == NULL ||
        rise == NULL ||
        run_mm == NULL ||
        !world_layer_cell_size(layer, &cell_size) ||
        !world_direction_delta(direction, &delta_column, &delta_row)) {
        return false;
    }

    if (world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return false;
    }

    if (!world_layer_value(world, layer, east_mm, north_mm, &here) ||
        !world_cell_at(
            world->width,
            world->depth,
            cell_size,
            east_mm,
            north_mm,
            &index)) {
        return false;
    }

    columns = world->width / cell_size;
    rows = world->depth / cell_size;
    column = index % columns;
    row = index / columns;

    if (!world_neighbor(
            world,
            columns,
            rows,
            column,
            row,
            delta_column,
            delta_row,
            &neighbor_index)) {
        return false;
    }

    neighbor_column = neighbor_index % columns;
    neighbor_row = neighbor_index / columns;
    neighbor_east_mm =
        neighbor_column * cell_size + cell_size / 2;
    neighbor_north_mm =
        neighbor_row * cell_size + cell_size / 2;

    if (!world_layer_value(
            world,
            layer,
            neighbor_east_mm,
            neighbor_north_mm,
            &neighbor)) {
        return false;
    }

    *rise = neighbor - here;
    *run_mm = cell_size;
    return true;
}

/*
 * Fold one axis onto a modular edge. count is the number of cells
 * on that axis. The C remainder keeps the sign of the dividend, so
 * a negative step is brought back into the range by adding count.
 */
static void world_wrap_axis(
    int64_t value,
    uint32_t count,
    uint32_t *wrapped)
{
    int64_t span;

    span = (int64_t)count;
    value %= span;

    if (value < 0) {
        value += span;
    }

    *wrapped = (uint32_t)value;
}

bool world_neighbor(
    const world_state_t *world,
    uint32_t columns,
    uint32_t rows,
    uint32_t column,
    uint32_t row,
    int delta_column,
    int delta_row,
    uint32_t *index)
{
    int64_t next_column;
    int64_t next_row;
    uint32_t column_index;
    uint32_t row_index;

    if (world == NULL ||
        index == NULL ||
        columns == 0 ||
        rows == 0) {
        return false;
    }

    next_column = (int64_t)column + delta_column;
    next_row = (int64_t)row + delta_row;

    if (world->modularity == WORLD_MODULARITY_MODULAR) {
        world_wrap_axis(next_column, columns, &column_index);
        world_wrap_axis(next_row, rows, &row_index);
    } else if (next_column < 0 ||
               next_row < 0 ||
               (uint64_t)next_column >= (uint64_t)columns ||
               (uint64_t)next_row >= (uint64_t)rows) {
        return false;
    } else {
        column_index = (uint32_t)next_column;
        row_index = (uint32_t)next_row;
    }

    *index = row_index * columns + column_index;
    return true;
}

/*
 * Future: allocate a new unused serial for a species layer
 * (e.g. max(existing ids) + 1). Not wired yet.
 */
uint32_t world_individual_alloc_id(
    const world_state_t *world,
    const char *species)
{
    (void)world;
    (void)species;
    return 0;
}

/*
 * Future: heightmap elevation at (x,y) for a surface-walking being.
 * Not wired yet.
 */
bool world_individual_surface_z_mm(
    const world_state_t *world,
    int32_t x_mm,
    int32_t y_mm,
    int32_t *z_mm)
{
    (void)world;
    (void)x_mm;
    (void)y_mm;
    (void)z_mm;
    return false;
}

/*******************************
 * Destroy the world state and free any allocated memory.
 */
void world_destroy(world_state_t *world)
{
    uint32_t i;

    if (world == NULL) {
        return;
    }

    if (world->layers != NULL) {
        for (i = 0; i < world->layer_count; i++) {
            world_free_layer(world->layers[i]);
            world->layers[i] = NULL;
        }

        free(world->layers);
    }

    world->layers = NULL;
    world->layer_count = 0;
}

