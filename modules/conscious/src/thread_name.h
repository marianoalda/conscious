#ifndef THREAD_NAME_H
#define THREAD_NAME_H

/*
 * Set the calling thread's name for debuggers / top.
 * Linux allows at most 15 characters (TASK_COMM_LEN - 1).
 */
void conscious_thread_name_set(const char *name);

#define CONSCIOUS_THREAD_MAIN "cons main"
#define CONSCIOUS_THREAD_GRID "cons grid"

/*
 * Individual threads: "cons <TAG> <id>" with id up to 999999.
 * <TAG> is defined per species (e.g. WORLD_SPECIES_RABBIT_THREAD_TAG).
 */
#define CONSCIOUS_THREAD_INDIVIDUAL_ID_MAX 999999u

#endif
