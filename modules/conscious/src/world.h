#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#define WORLD_MAGIC "CWLD"

typedef struct {
    uint32_t format_version;
} world_state_t;

int world_load(
    const char *filename,
    world_state_t *world);

int world_serialize(
    const char *filename,
    const world_state_t *world);

#endif