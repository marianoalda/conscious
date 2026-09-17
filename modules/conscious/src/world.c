#include <stdio.h>

#include "world.h"

int world_load(const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        return -1;
    }

    /*
     * Dummy implementation:
     * The world contents are not processed yet.
     */

    fclose(file);

    return 0;
}

int world_serialize(const char *filename)
{
    FILE *file = fopen(filename, "wb");

    if (file == NULL) {
        return -1;
    }

    /*
     * Dummy implementation:
     * Write a placeholder binary world.
     */

    fclose(file);

    return 0;
}