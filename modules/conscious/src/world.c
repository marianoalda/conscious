#include "world.h"
#include "world_io.h"
#include "world_internal.h"

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

    error = world_v3_serialize(
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
        } else if (layer->type == WORLD_LAYER_STATICWATER) {
            world_staticwater_payload_t *water = layer->payload;

            free(water->published);
            free(water->pending);
        } else if (layer->type == WORLD_LAYER_DIFFLIGHT) {
            world_difflight_payload_t *light = layer->payload;

            free(light->published);
            free(light->pending);
        } else if (layer->type == WORLD_LAYER_HUMIDITY) {
            world_u16_payload_t *grid = layer->payload;

            free(grid->published);
            free(grid->pending);
        } else if (layer->type == WORLD_LAYER_FERTILITY ||
                   layer->type == WORLD_LAYER_GRASS) {
            world_u8_payload_t *grid = layer->payload;

            free(grid->published);
            free(grid->pending);
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

    if (world_find_layer(world, type) != NULL) {
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

    error = world_append_layer(
        world,
        WORLD_LAYER_HEIGHTMAP,
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

    error = world_append_layer(
        world,
        WORLD_LAYER_STATICWATER,
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
 * Fertility and grass. cell_size is 10 cm.
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
        type != WORLD_LAYER_GRASS) {
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

