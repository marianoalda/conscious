#include "world.h"
#include "world_io.h"
#include "world_internal.h"
#include "world_species_rabbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int species_equal(const char *stored, const char *wanted)
{
    size_t wanted_len;

    if (stored == NULL || wanted == NULL) {
        return 0;
    }

    wanted_len = strlen(wanted);
    if (wanted_len > WORLD_SPECIES_FIELD_BYTES) {
        return 0;
    }

    return memcmp(stored, wanted, wanted_len) == 0 &&
        (wanted_len == WORLD_SPECIES_FIELD_BYTES ||
         stored[wanted_len] == '\0');
}

const world_layer_t *world_find_individual_layer(
    const world_state_t *world,
    const char *species)
{
    uint32_t i;

    if (world == NULL || world->layers == NULL || species == NULL) {
        return NULL;
    }

    for (i = 0; i < world->layer_count; i++) {
        const world_layer_t *layer = world->layers[i];
        const world_individual_payload_t *payload;

        if (layer == NULL ||
            layer->type != WORLD_LAYER_INDIVIDUAL ||
            layer->payload == NULL) {
            continue;
        }

        payload = layer->payload;
        if (species_equal(payload->species, species)) {
            return layer;
        }
    }

    return NULL;
}

world_error_t world_append_individual_layer(
    world_state_t *world,
    world_clock_t clock,
    world_tick_t last_simulation_tick,
    const char *species,
    world_storage_depth_t depth,
    uint32_t species_version,
    uint32_t count,
    world_individual_t *individuals)
{
    world_individual_payload_t *payload;
    world_layer_t *layer;
    world_layer_t **grown;
    size_t species_len;
    world_error_t error;

    if (world == NULL ||
        species == NULL ||
        (count > 0 && individuals == NULL)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    species_len = strlen(species);
    if (species_len == 0 || species_len > WORLD_SPECIES_FIELD_BYTES) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world_find_individual_layer(world, species) != NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (world_species_rabbit_is(species)) {
        error = world_species_rabbit_validate(
            species,
            depth,
            species_version);
        if (error != WORLD_OK) {
            return error;
        }
    } else {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    payload = calloc(1, sizeof(*payload));
    if (payload == NULL) {
        return WORLD_ERROR_FILE;
    }

    memcpy(payload->species, species, species_len);
    payload->depth = depth;
    payload->species_version = species_version;
    payload->count = count;
    payload->individuals = individuals;

    layer = calloc(1, sizeof(*layer));
    if (layer == NULL) {
        free(payload);
        return WORLD_ERROR_FILE;
    }

    grown = realloc(
        world->layers,
        (world->layer_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(layer);
        free(payload);
        return WORLD_ERROR_FILE;
    }

    layer->type = WORLD_LAYER_INDIVIDUAL;
    layer->clock = clock;
    layer->last_simulation_tick = last_simulation_tick;
    layer->payload = payload;

    world->layers = grown;
    world->layers[world->layer_count] = layer;
    world->layer_count++;

    return WORLD_OK;
}

static world_error_t read_padded_field(
    FILE *file,
    char *out,
    size_t field_size)
{
    if (fread(out, 1, field_size, file) != field_size) {
        return WORLD_ERROR_TRUNCATED;
    }

    return WORLD_OK;
}

static world_error_t parse_storage_depth(
    const char *depth_field,
    world_storage_depth_t *depth)
{
    if (memcmp(
            depth_field,
            WORLD_STORAGE_DEPTH_FUNCTIONAL_VALUE,
            strlen(WORLD_STORAGE_DEPTH_FUNCTIONAL_VALUE)) == 0) {
        *depth = WORLD_STORAGE_DEPTH_FUNCTIONAL;
        return WORLD_OK;
    }

    if (memcmp(
            depth_field,
            WORLD_STORAGE_DEPTH_DEEP_VALUE,
            strlen(WORLD_STORAGE_DEPTH_DEEP_VALUE)) == 0) {
        *depth = WORLD_STORAGE_DEPTH_DEEP;
        return WORLD_OK;
    }

    return WORLD_ERROR_INVALID_FORMAT;
}

static world_error_t deserialize_individual_layer_v4(
    FILE *file,
    world_state_t *world)
{
    char layer_name[16];
    char species[WORLD_SPECIES_FIELD_BYTES];
    char depth_field[WORLD_STORAGE_DEPTH_FIELD_BYTES];
    world_clock_t clock;
    uint64_t last_tick;
    uint32_t species_version;
    uint32_t count;
    world_individual_t *individuals = NULL;
    world_storage_depth_t depth;
    const char *species_name;
    world_error_t error;

    error = world_io_read_fixed_string(file, layer_name, sizeof(layer_name));
    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_name,
            WORLD_LAYER_NAME_INDIVIDUAL,
            strlen(WORLD_LAYER_NAME_INDIVIDUAL)) != 0) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_v3_read_clock(file, &clock);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint64_be(file, &last_tick);
    if (error != WORLD_OK) {
        return error;
    }

    error = read_padded_field(file, species, sizeof(species));
    if (error != WORLD_OK) {
        return error;
    }

    error = read_padded_field(file, depth_field, sizeof(depth_field));
    if (error != WORLD_OK) {
        return error;
    }

    error = parse_storage_depth(depth_field, &depth);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint32_be(file, &species_version);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_read_uint32_be(file, &count);
    if (error != WORLD_OK) {
        return error;
    }

    if (world_species_rabbit_is(species)) {
        error = world_species_rabbit_validate(
            WORLD_SPECIES_RABBIT_FUNCTIONAL,
            depth,
            species_version);
        if (error != WORLD_OK) {
            return error;
        }

        error = world_species_rabbit_read_records(
            file,
            count,
            &individuals);
        if (error != WORLD_OK) {
            return error;
        }

        species_name = WORLD_SPECIES_RABBIT_FUNCTIONAL;
    } else {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_append_individual_layer(
        world,
        clock,
        last_tick,
        species_name,
        depth,
        species_version,
        count,
        individuals);

    if (error != WORLD_OK) {
        free(individuals);
    }

    return error;
}

static world_error_t load_one_layer_v4(
    FILE *file,
    world_state_t *world)
{
    char layer_type[16];
    world_error_t error;
    long type_pos;

    type_pos = ftell(file);
    if (type_pos < 0) {
        return WORLD_ERROR_FILE;
    }

    error = world_io_read_fixed_string(file, layer_type, sizeof(layer_type));
    if (error != WORLD_OK) {
        return error;
    }

    if (memcmp(
            layer_type,
            WORLD_LAYER_TYPE_INDIVIDUAL,
            strlen(WORLD_LAYER_TYPE_INDIVIDUAL)) == 0) {
        return deserialize_individual_layer_v4(file, world);
    }

    if (fseek(file, type_pos, SEEK_SET) != 0) {
        return WORLD_ERROR_FILE;
    }

    return world_v3_load_one_layer(file, world);
}

static world_error_t write_individual_layer_v4(
    FILE *file,
    const world_layer_t *layer)
{
    const world_individual_payload_t *payload;
    const char *depth_text;
    world_error_t error;

    if (layer == NULL || layer->payload == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    payload = layer->payload;

    if (world_species_rabbit_is(payload->species)) {
        error = world_species_rabbit_validate(
            WORLD_SPECIES_RABBIT_FUNCTIONAL,
            payload->depth,
            payload->species_version);
        if (error != WORLD_OK) {
            return error;
        }
    } else {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (payload->count > 0 && payload->individuals == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    depth_text = payload->depth == WORLD_STORAGE_DEPTH_FUNCTIONAL ?
        WORLD_STORAGE_DEPTH_FUNCTIONAL_VALUE :
        WORLD_STORAGE_DEPTH_DEEP_VALUE;

    if (fwrite(WORLD_LAYER_MAGIC, 1, 4, file) != 4) {
        return WORLD_ERROR_FILE;
    }

    error = world_v3_write_padded_string(
        file,
        WORLD_LAYER_TYPE_INDIVIDUAL,
        16);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_v3_write_padded_string(
        file,
        WORLD_LAYER_NAME_INDIVIDUAL,
        16);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_v3_write_clock(file, &layer->clock);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint64_be(file, layer->last_simulation_tick);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_v3_write_padded_string(
        file,
        payload->species,
        WORLD_SPECIES_FIELD_BYTES);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_v3_write_padded_string(
        file,
        depth_text,
        WORLD_STORAGE_DEPTH_FIELD_BYTES);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint32_be(file, payload->species_version);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_io_write_uint32_be(file, payload->count);
    if (error != WORLD_OK) {
        return error;
    }

    return world_species_rabbit_write_records(
        file,
        payload->count,
        payload->individuals);
}

world_error_t world_v4_load(
    FILE *file,
    world_state_t *world)
{
    int layer_present;
    world_error_t error;

    error = world_v3_read_header(file, world);
    if (error != WORLD_OK) {
        return error;
    }

    for (;;) {
        error = world_v3_read_next_layer_marker(file, &layer_present);
        if (error != WORLD_OK) {
            return error;
        }

        if (!layer_present) {
            break;
        }

        error = load_one_layer_v4(file, world);
        if (error != WORLD_OK) {
            return error;
        }
    }

    return world_v3_check_staticwater_grid(world);
}

world_error_t world_v4_serialize(
    FILE *file,
    const world_state_t *world)
{
    uint32_t i;
    world_error_t error;

    if (world_find_layer(world, WORLD_LAYER_HEIGHTMAP) == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    error = world_v3_check_staticwater_grid(world);
    if (error != WORLD_OK) {
        return error;
    }

    error = world_v3_write_header(file, world);
    if (error != WORLD_OK) {
        return error;
    }

    for (i = 0; i < world->layer_count; i++) {
        const world_layer_t *layer = world->layers[i];

        if (layer == NULL) {
            return WORLD_ERROR_INVALID_FORMAT;
        }

        if (layer->type == WORLD_LAYER_INDIVIDUAL) {
            error = write_individual_layer_v4(file, layer);
        } else {
            error = world_v3_write_one_layer(file, world, layer);
        }

        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}
