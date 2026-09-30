/*
 * One-shot: load a world, append two SPECIES_RABBIT_FUNCTIONAL individuals
 * on distant flat plain cells, write a new file. Does not modify the source.
 *
 * Build from modules/:
 *   gcc -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L \
 *     -Iconcious/src -o /tmp/add_rabbit_test utils/add_rabbit_test.c \
 *     conscious/build/world*.o -lm
 */

#include "world.h"
#include "world_internal.h"
#include "world_species_rabbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *in_path;
    const char *out_path;
    world_state_t world;
    world_individual_t *individuals;
    world_clock_t clock;
    world_error_t error;
    int exit_code = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <in.bin> <out.bin>\n", argv[0]);
        return 1;
    }

    in_path = argv[1];
    out_path = argv[2];

    memset(&world, 0, sizeof(world));
    error = world_load(in_path, &world);
    if (error != WORLD_OK) {
        fprintf(stderr, "load %s: %s\n", in_path, world_error_string(error));
        return 1;
    }

    individuals = calloc(2, sizeof(*individuals));
    if (individuals == NULL) {
        fprintf(stderr, "out of memory\n");
        goto done;
    }

    /*
     * Plain elevation = min_height (500) + stored offset (500) = 1000 mm.
     * Pool centre is 13.6 m east, 10.0 m north, radius 1 m
     * (create_world_v3_pool_mountain.py). One rabbit sits 1 m south of
     * the southern shore; the other stays on the flat SW margin.
     */
    individuals[0].id = 1;
    individuals[0].x_mm = 2550;
    individuals[0].y_mm = 2550;
    individuals[0].z_mm = 1000;
    individuals[0].orientation_mrad = 0;

    individuals[1].id = 2;
    individuals[1].x_mm = 13600; /* pool east */
    individuals[1].y_mm = 8000;  /* 1 m south of pool shore (10 m - 1 m - 1 m) */
    individuals[1].z_mm = 1000;
    individuals[1].orientation_mrad = 1571; /* ~90 deg, north */

    clock.mode = WORLD_CLOCK_NOEV;
    clock.exponent = 0;

    error = world_append_individual_layer(
        &world,
        clock,
        world.age, /* already past midnights before the rabbits existed */
        WORLD_SPECIES_RABBIT_FUNCTIONAL,
        WORLD_STORAGE_DEPTH_FUNCTIONAL,
        WORLD_SPECIES_VERSION_RABBIT_FUNCTIONAL_V1,
        2,
        individuals);
    if (error != WORLD_OK) {
        fprintf(stderr, "append rabbits: %s\n", world_error_string(error));
        free(individuals);
        goto done;
    }
    /* layer now owns individuals */

    error = world_serialize(out_path, &world);
    if (error != WORLD_OK) {
        fprintf(stderr, "serialize %s: %s\n", out_path, world_error_string(error));
        goto done;
    }

    printf(
        "wrote %s with 2 rabbits at (%d,%d,%d) and (%d,%d,%d)\n",
        out_path,
        individuals[0].x_mm,
        individuals[0].y_mm,
        individuals[0].z_mm,
        individuals[1].x_mm,
        individuals[1].y_mm,
        individuals[1].z_mm);
    exit_code = 0;

done:
    world_destroy(&world);
    return exit_code;
}
