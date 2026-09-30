#include "world_species_rabbit.h"

#include "world_io.h"

#include <stdlib.h>
#include <string.h>

bool world_species_rabbit_is(const char *species_field)
{
    size_t len;

    if (species_field == NULL) {
        return false;
    }

    len = strlen(WORLD_SPECIES_RABBIT_FUNCTIONAL);
    if (len > WORLD_SPECIES_FIELD_BYTES) {
        return false;
    }

    return memcmp(
               species_field,
               WORLD_SPECIES_RABBIT_FUNCTIONAL,
               len) == 0 &&
        (len == WORLD_SPECIES_FIELD_BYTES ||
         species_field[len] == '\0');
}

world_error_t world_species_rabbit_validate(
    const char *species,
    world_storage_depth_t depth,
    uint32_t species_version)
{
    if (!world_species_rabbit_is(species)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    if (depth != WORLD_STORAGE_DEPTH_FUNCTIONAL ||
        species_version != WORLD_SPECIES_VERSION_RABBIT_FUNCTIONAL_V1) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    return WORLD_OK;
}

world_error_t world_species_rabbit_read_records(
    FILE *file,
    uint32_t count,
    world_individual_t **out)
{
    world_individual_t *individuals = NULL;
    uint32_t i;
    world_error_t error;

    if (out == NULL || file == NULL) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    *out = NULL;

    if (count == 0) {
        return WORLD_OK;
    }

    individuals = calloc(count, sizeof(*individuals));
    if (individuals == NULL) {
        return WORLD_ERROR_FILE;
    }

    for (i = 0; i < count; i++) {
        char marker[4];
        uint32_t id;
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t orientation;

        if (fread(marker, 1, sizeof(marker), file) != sizeof(marker)) {
            free(individuals);
            return WORLD_ERROR_TRUNCATED;
        }

        if (memcmp(marker, WORLD_INDIVIDUAL_MAGIC, sizeof(marker)) != 0) {
            free(individuals);
            return WORLD_ERROR_INVALID_FORMAT;
        }

        error = world_io_read_uint32_be(file, &id);
        if (error != WORLD_OK || id == 0) {
            free(individuals);
            return error != WORLD_OK ? error : WORLD_ERROR_INVALID_FORMAT;
        }

        error = world_io_read_int32_be(file, &x);
        if (error != WORLD_OK) {
            free(individuals);
            return error;
        }

        error = world_io_read_int32_be(file, &y);
        if (error != WORLD_OK) {
            free(individuals);
            return error;
        }

        error = world_io_read_int32_be(file, &z);
        if (error != WORLD_OK) {
            free(individuals);
            return error;
        }

        error = world_io_read_int32_be(file, &orientation);
        if (error != WORLD_OK) {
            free(individuals);
            return error;
        }

        individuals[i].id = id;
        individuals[i].x_mm = x;
        individuals[i].y_mm = y;
        individuals[i].z_mm = z;
        individuals[i].orientation_mrad = orientation;
    }

    *out = individuals;
    return WORLD_OK;
}

world_error_t world_species_rabbit_write_records(
    FILE *file,
    uint32_t count,
    const world_individual_t *individuals)
{
    uint32_t i;
    world_error_t error;

    if (file == NULL || (count > 0 && individuals == NULL)) {
        return WORLD_ERROR_INVALID_FORMAT;
    }

    for (i = 0; i < count; i++) {
        const world_individual_t *ind = &individuals[i];

        if (ind->id == 0) {
            return WORLD_ERROR_INVALID_FORMAT;
        }

        if (fwrite(WORLD_INDIVIDUAL_MAGIC, 1, 4, file) != 4) {
            return WORLD_ERROR_FILE;
        }

        error = world_io_write_uint32_be(file, ind->id);
        if (error != WORLD_OK) {
            return error;
        }

        error = world_io_write_int32_be(file, ind->x_mm);
        if (error != WORLD_OK) {
            return error;
        }

        error = world_io_write_int32_be(file, ind->y_mm);
        if (error != WORLD_OK) {
            return error;
        }

        error = world_io_write_int32_be(file, ind->z_mm);
        if (error != WORLD_OK) {
            return error;
        }

        error = world_io_write_int32_be(file, ind->orientation_mrad);
        if (error != WORLD_OK) {
            return error;
        }
    }

    return WORLD_OK;
}
