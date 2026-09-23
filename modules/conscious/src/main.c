#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <termios.h>
#include <sys/select.h>
#include <inttypes.h>
#include <time.h>

#include "configuration.h"
#include "world.h"
#include "simulation.h"

typedef enum {
    ON_HOLD,
    SIMULATING
} main_state_t;

typedef struct {
    int verbose;
    const char *config_file;
    const char *name;
    const char *validate_world_file;
} options_t;

static const char *layer_type_name(world_layer_type_t type)
{
    switch (type) {
        case WORLD_LAYER_HEIGHTMAP:
            return WORLD_LAYER_TYPE_HEIGHTMAP;

        case WORLD_LAYER_STATICWATER:
            return WORLD_LAYER_TYPE_STATICWATER;

        case WORLD_LAYER_DIFFLIGHT:
            return WORLD_LAYER_TYPE_DIFFLIGHT;

        case WORLD_LAYER_HUMIDITY:
            return WORLD_LAYER_TYPE_HUMIDITY;

        case WORLD_LAYER_FERTILITY:
            return WORLD_LAYER_TYPE_FERTILITY;

        case WORLD_LAYER_GRASS:
            return WORLD_LAYER_TYPE_GRASS;

        default:
            return "unknown";
    }
}

static void print_layer_clock(const world_clock_t *clock)
{
    if (clock->mode == WORLD_CLOCK_NOEV) {
        printf("%s", WORLD_LAYER_CLOCK_NOEV);
        return;
    }

    if (clock->mode == WORLD_CLOCK_DIVISOR) {
        printf("%s%04u", WORLD_LAYER_CLOCK_PREFIX, clock->exponent);
        return;
    }

    printf("unknown");
}

static const char *modularity_name(world_modularity_t modularity)
{
    switch (modularity) {
        case WORLD_MODULARITY_CLOSED:
            return WORLD_MODULARITY_CLOSED_VALUE;

        case WORLD_MODULARITY_MODULAR:
            return WORLD_MODULARITY_MODULAR_VALUE;

        default:
            return "unknown";
    }
}

/************************************
 * Print the loaded world and its layers when verbose.
 */
static void print_loaded_world(const world_state_t *world)
{
    uint32_t i;

    printf("World\n");
    printf("  format_version: %" PRIu32 "\n", world->format_version);
    printf("  width: %" PRIu32 " %s\n", world->width, WORLD_DISTANCE_UNIT);
    printf("  depth: %" PRIu32 " %s\n", world->depth, WORLD_DISTANCE_UNIT);
    printf("  modularity: %s\n", modularity_name(world->modularity));
    printf("  age: %" PRIu64 " %s\n", world->age, WORLD_TICK_UNIT);
    printf("  layers: %" PRIu32 "\n", world->layer_count);

    for (i = 0; i < world->layer_count; i++) {
        const world_layer_t *layer = world->layers[i];

        printf("    [%u]\n", i);

        if (layer == NULL) {
            printf("      (null)\n");
            continue;
        }

        printf("      type: %s\n", layer_type_name(layer->type));
        printf("      clock: ");
        print_layer_clock(&layer->clock);
        printf("\n");
        printf(
            "      last_simulation_tick: %" PRIu64 " %s\n",
            layer->last_simulation_tick,
            WORLD_TICK_UNIT);

        if (layer->type == WORLD_LAYER_HEIGHTMAP &&
            layer->payload != NULL) {
            const world_heightmap_payload_t *heightmap = layer->payload;

            printf(
                "      cell_size: %" PRIu32 " %s\n",
                heightmap->cell_size,
                WORLD_DISTANCE_UNIT);
            printf(
                "      min_height: %" PRId32 " %s\n",
                heightmap->min_height,
                WORLD_DISTANCE_UNIT);
        }

        if (layer->type == WORLD_LAYER_STATICWATER &&
            layer->payload != NULL) {
            const world_staticwater_payload_t *water = layer->payload;

            printf(
                "      cell_size: %" PRIu32 " %s\n",
                water->cell_size,
                WORLD_DISTANCE_UNIT);
            printf(
                "      min_depth: %" PRId32 " %s\n",
                water->min_depth,
                WORLD_DISTANCE_UNIT);
        }

        if (layer->type == WORLD_LAYER_DIFFLIGHT &&
            layer->payload != NULL) {
            const world_difflight_payload_t *light = layer->payload;

            printf(
                "      cell_size: %" PRIu32 " %s\n",
                light->cell_size,
                WORLD_DISTANCE_UNIT);
            printf(
                "      max_irradiance: %" PRIu32 " %s\n",
                light->max_irradiance,
                WORLD_IRRADIANCE_UNIT);
        }

        if ((layer->type == WORLD_LAYER_HUMIDITY ||
             layer->type == WORLD_LAYER_FERTILITY ||
             layer->type == WORLD_LAYER_GRASS) &&
            layer->payload != NULL) {
            const world_u8_payload_t *grid = layer->payload;

            printf(
                "      cell_size: %" PRIu32 " %s\n",
                grid->cell_size,
                WORLD_DISTANCE_UNIT);
            printf("      value: uint8\n");
        }
    }
}

/************************************
 * Print help
 */
static void print_help(const char *program)
{
    printf("Usage: %s [OPTIONS]\n\n", program);
    printf("Options:\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -c, --config FILE       Configuration file\n");
    printf("  -n, --name NAME         Node/process name\n");
    printf("  -w, --validate-world FILE Validate world file\n");
}

/************************************
 * Enable operator input
 */
static int enable_operator_input(struct termios *original)
{
    struct termios terminal;

    if (tcgetattr(STDIN_FILENO, original) != 0) {
        return -1;
    }

    terminal = *original;

    terminal.c_lflag &= ~(ICANON | ECHO);
    terminal.c_cc[VMIN] = 1;
    terminal.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0) {
        return -1;
    }

    return 0;
}

/************************************
 * Disable operator input
 */
static void disable_operator_input(const struct termios *original)
{
    tcsetattr(STDIN_FILENO, TCSANOW, original);
}


/************************************
 * Parse command-line arguments
 */
static int parse_arguments(int argc, char **argv, options_t *options)
{
    static const struct option long_options[] = {
        {"help",    no_argument,       NULL, 'h'},
        {"verbose", no_argument,       NULL, 'v'},
        {"config",  required_argument, NULL, 'c'},
        {"name",    required_argument, NULL, 'n'},
        {"validate-world", required_argument, NULL, 'w'},
        {NULL,      0,                 NULL,  0}
    };

    int option;

    while ((option = getopt_long(argc, argv, "hvc:n:w:", long_options, NULL)) != -1) {
        switch (option) {
        case 'h':
            print_help(argv[0]);
            exit(EXIT_SUCCESS);

        case 'v':
            options->verbose = 1;
            break;

        case 'c':
            options->config_file = optarg;
            break;

        case 'n':
            options->name = optarg;
            break;

        case 'w':
            options->validate_world_file = optarg;
            break;

        default:
            return -1;
        }
    }

    return 0;
}

/************************************
 * Parse configuration file
 */
static int parse_configuration(FILE *config_file, configuration_t *configuration)
{
    char line[1024];
    unsigned int line_number = 0;

    while (fgets(line, sizeof(line), config_file) != NULL) {
        char *variable;
        char *value;
        char *separator;
        char *end;

        line_number++;

        /* Remove trailing newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* Skip leading whitespace */
        variable = line;
        while (isspace((unsigned char)*variable))
            variable++;

        /* Ignore empty lines and comments */
        if (*variable == '\0' || *variable == '#')
            continue;

        /* Find '=' */
        separator = strchr(variable, '=');
        if (separator == NULL) {
            fprintf(stderr,
                    "Error: Invalid configuration at line %u: expected 'variable = value'.\n",
                    line_number);
            return -1;
        }

        /* Split variable and value */
        *separator = '\0';
        value = separator + 1;

        /* Remove trailing whitespace from variable */
        end = separator - 1;
        while (end >= variable && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }

        /* Skip leading whitespace from value */
        while (isspace((unsigned char)*value))
            value++;

        /* Remove trailing whitespace from value */
        end = value + strlen(value) - 1;
        while (end >= value && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }

        if (*variable == '\0' || *value == '\0') {
            fprintf(stderr,
                    "Error: Invalid configuration at line %u.\n",
                    line_number);
            return -1;
        }

        /*
         * save_state_on_shutdown = true|false
         */
        if (strcmp(variable, "save_state_on_shutdown") == 0) {

            if (strcmp(value, "true") == 0) {
                configuration->save_state_on_shutdown = true;
            }
            else if (strcmp(value, "false") == 0) {
                configuration->save_state_on_shutdown = false;
            }
            else {
                fprintf(stderr,
                        "Error: Invalid value for '%s' at line %u: '%s'. "
                        "Expected true or false.\n",
                        variable, line_number, value);
                return -1;
            }
        }

        /*
         * state_on_start = SIMULATE|HOLD
         */
        else if (strcmp(variable, "state_on_start") == 0) {

            if (strcmp(value, "SIMULATE") == 0) {
                configuration->state_on_start = SIMULATE;
            }
            else if (strcmp(value, "HOLD") == 0) {
                configuration->state_on_start = HOLD;
            }
            else {
                fprintf(stderr,
                        "Error: Invalid value for '%s' at line %u: '%s'. "
                        "Expected SIMULATE or HOLD.\n",
                        variable, line_number, value);
                return -1;
            }
        }

        else if (strcmp(variable, "incremental_steps") == 0) {
            char *end_pointer;
            unsigned long long steps;

            errno = 0;
            steps = strtoull(value, &end_pointer, 10);

            if (value[0] == '-' ||
                errno != 0 ||
                end_pointer == value ||
                *end_pointer != '\0' ||
                steps == 0) {
                fprintf(stderr,
                        "Error: Invalid value for '%s' at line %u: '%s'. "
                        "Expected a positive integer.\n",
                        variable, line_number, value);
                return -1;
            }

            configuration->incremental_steps = steps;
        }

        else if (strcmp(variable, "step_delay_us") == 0) {
            char *end_pointer;
            unsigned long long delay_us;

            errno = 0;
            delay_us = strtoull(value, &end_pointer, 10);

            if (value[0] == '-' ||
                errno != 0 ||
                end_pointer == value ||
                *end_pointer != '\0' ||
                delay_us == 0) {
                fprintf(stderr,
                        "Error: Invalid value for '%s' at line %u: '%s'. "
                        "Expected a positive integer.\n",
                        variable, line_number, value);
                return -1;
            }

            configuration->step_delay_set = true;
            configuration->step_delay_us = delay_us;
        }

        else if (strcmp(variable, "world_file") == 0) {
            if (strlen(value) >= sizeof(configuration->world_file)) {
                fprintf(stderr,
                    "Error: Value for '%s' at line %u is too long.\n",
                    variable, line_number);
                return -1;
            }
            strcpy(configuration->world_file, value);
        }

        else {
            fprintf(stderr,
                    "Error: Unknown configuration parameter at line %u: '%s'.\n",
                    line_number, variable);
            return -1;
        }
    }

    if (ferror(config_file)) {
        fprintf(stderr, "Error: Error reading configuration file.\n");
        return -1;
    }

    return 0;
}

/************************************
 * Get absolute world file path
 */
static int get_absolute_world_path(
    const char *executable_dir,
    const char *configured_path,
    char *absolute_path,
    size_t size)
{
    int length;

    if (configured_path[0] == '/') {
        length = snprintf(
            absolute_path,
            size,
            "%s",
            configured_path);
    }
    else {
        length = snprintf(
            absolute_path,
            size,
            "%s/%s",
            executable_dir,
            configured_path);
    }

    if (length < 0 || (size_t)length >= size) {
        fprintf(stderr, "Error: World file path is too long.\n");
        return -1;
    }

    return 0;
}

/*
 * Write the published grids beside the world file.
 * The name ends with the age in milliseconds. The original file is kept.
 */
static int save_snapshot(
    simulation_t *simulation,
    const world_state_t *world,
    const char *world_path)
{
    char snapshot_path[PATH_MAX];
    world_tick_t world_tick;
    world_state_t snapshot_world;
    int length;

    world_tick = simulation_get_world_tick(simulation);

    snapshot_world = *world;
    snapshot_world.age = world_tick;

    length = snprintf(
        snapshot_path,
        sizeof(snapshot_path),
        "%s.%" PRIu64,
        world_path,
        world_tick);

    if (length < 0 || (size_t)length >= sizeof(snapshot_path)) {
        fprintf(
            stderr,
            "Error: Snapshot file path is too long.\n");
        return -1;
    }

    if (world_serialize(snapshot_path, &snapshot_world) != 0) {
        fprintf(
            stderr,
            "Error: Unable to save snapshot: %s\n",
            snapshot_path);
        return -1;
    }

    printf(
        "\nSnapshot saved: %s\n",
        snapshot_path);

    return 0;
}

/************************************
 * Get executable directory
 */
static int get_executable_directory(char *buffer, size_t size)
{
    ssize_t length;
    char *last_slash;

    length = readlink("/proc/self/exe", buffer, size - 1);

    if (length == -1 || (size_t)length >= size)
        return -1;

    buffer[length] = '\0';

    last_slash = strrchr(buffer, '/');

    if (last_slash == NULL)
        return -1;

    *last_slash = '\0';

    return 0;
}

/************************************
 * Main function
 */
int main(int argc, char **argv)
{
    options_t options = {
        .verbose = 0,
        .config_file = NULL,
        .name = NULL,
        .validate_world_file = NULL
    };

    configuration_t configuration = {
        .save_state_on_shutdown = true,
        .state_on_start = SIMULATE,
        .incremental_steps = 3600000,
        .step_delay_set = false,
        .step_delay_us = 0,
        .world_file = "world.bin"
    };

    char executable_dir[PATH_MAX];
    char default_config[PATH_MAX];
    char absolute_world_path[PATH_MAX];

    FILE *config_file = NULL;

    world_state_t world;

    if (parse_arguments(argc, argv, &options) != 0) {
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /*
     * Validate a world file and exit if specified.
     */
    if (options.validate_world_file != NULL) {
        world_error_t world_error;

        printf(
            "Validating world: %s\n",
            options.validate_world_file);

        world_error = world_load(
            options.validate_world_file,
            &world);

        if (world_error != WORLD_OK) {
            fprintf(
                stderr,
                "World validation failed: %s\n",
                world_error_string(world_error));

            world_destroy(&world);

            return EXIT_FAILURE;
        }

        printf("World validation successful.\n");

        world_destroy(&world);

        return EXIT_SUCCESS;
    }
    
    /*
    * Get executable directory.
    */
    if (get_executable_directory(
            executable_dir,
            sizeof(executable_dir)) != 0) {

        fprintf(stderr, "Unable to determine executable directory.\n");
        return EXIT_FAILURE;
    }

    /*
    * Use conscious.cfg from the executable directory
    * when no configuration file was specified.
    */
    if (options.config_file == NULL) {
        if (snprintf(
                default_config,
                sizeof(default_config),
                "%s/conscious.cfg",
                executable_dir) >= (int)sizeof(default_config)) {

            fprintf(stderr, "Configuration file path is too long.\n");
            return EXIT_FAILURE;
        }

        options.config_file = default_config;
    }
    /* initialization */
    printf("Conscious Project - Main and Sync module.\n");

    /* Main logic here */
    printf("Initializing...\n");

    /* print configuration file only if verbose */
    if (options.verbose) {
        printf("Configuration: %s\n", options.config_file);
    }

    config_file = fopen(options.config_file, "r");
    if (config_file == NULL) {
        fprintf(stderr,
                "Error: Unable to open configuration file '%s'.\n",
                options.config_file);
        return EXIT_FAILURE;
    }

    /*
     * Parse configuration file and assign values to the
     * configuration structure.
     */
    if (parse_configuration(config_file, &configuration) != 0) {
        fclose(config_file);
        return EXIT_FAILURE;
    }

    fclose(config_file);

    /*
     * Resolve world file path.
     */
    if (get_absolute_world_path(
            executable_dir,
            configuration.world_file,
            absolute_world_path,
            sizeof(absolute_world_path)) != 0) {

        return EXIT_FAILURE;
    }

    if (options.verbose) {
        printf("World: %s\n", absolute_world_path);
    }

    /*
     * Load world.
     */
    if (options.verbose) {
        printf("Loading world: %s\n", absolute_world_path);
    }

    world_error_t world_error;

    world_error = world_load(
        absolute_world_path,
        &world);

    if (world_error != WORLD_OK) {
        fprintf(
            stderr,
            "Error: Unable to load world: %s.\n",
            world_error_string(world_error));

        world_destroy(&world);

        return EXIT_FAILURE;
    }

    if (options.verbose) {
        print_loaded_world(&world);
    }

    /*************************
     * Main control loop begins (operator, not simulation)
     *************************/

    simulation_t *simulation = NULL;
    struct termios original_terminal;
    main_state_t main_state;

    world_tick_t previous_world_tick = 0;
    struct timespec previous_time;
    bool incremental_active = false;
    world_tick_t incremental_stop_tick = 0;

    if (simulation_init(
            &simulation,
            &world,
            configuration.step_delay_set,
            configuration.step_delay_us) != 0) {
        fprintf(stderr, "Error: Unable to initialize simulation.\n");
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    if (simulation_start(simulation) != 0) {
        fprintf(stderr, "Error: Unable to start simulation.\n");
        simulation_destroy(simulation);
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    if (configuration.state_on_start == SIMULATE) {
        main_state = SIMULATING;

        simulation_resume(simulation);

        previous_world_tick = simulation_get_world_tick(simulation);
        clock_gettime(CLOCK_MONOTONIC, &previous_time);
    }
    else {
        main_state = ON_HOLD;

        simulation_pause(simulation);
        simulation_wait_until_paused(simulation);
    }

    if (enable_operator_input(&original_terminal) != 0) {
        fprintf(stderr, "Error: Unable to configure terminal input.\n");
        simulation_destroy(simulation);
        world_destroy(&world);
        return EXIT_FAILURE;
    }

    for (;;) {
        int key;
        int result;
        fd_set read_fds;
        struct timeval timeout;

        if (main_state == SIMULATING && incremental_active) {
            world_tick_t current_world_tick;

            current_world_tick = simulation_get_world_tick(simulation);

            if (current_world_tick >= incremental_stop_tick) {
                simulation_wait_until_paused(simulation);
                main_state = ON_HOLD;
                incremental_active = false;
            }
        }

        if (main_state == SIMULATING) {
            world_tick_t current_world_tick;
            struct timespec current_time;
            double elapsed_seconds;
            double ticks_per_second;

            current_world_tick = simulation_get_world_tick(simulation);

            clock_gettime(CLOCK_MONOTONIC, &current_time);

            elapsed_seconds =
                (double)(current_time.tv_sec - previous_time.tv_sec) +
                (double)(current_time.tv_nsec - previous_time.tv_nsec) / 1000000000.0;

            ticks_per_second = 0.0;

            if (elapsed_seconds > 0.0) {
                ticks_per_second =
                    (double)(current_world_tick - previous_world_tick) /
                    elapsed_seconds;
            }

            previous_world_tick = current_world_tick;
            previous_time = current_time;

            if (incremental_active) {
                printf(
                    "\r[SIMULATING]  %.1f %s/s  age: %" PRIu64 " %s  "
                    "until: %" PRIu64 " %s  "
                    "[p] pause  [q] shutdown\033[K",
                    ticks_per_second,
                    WORLD_TICK_UNIT,
                    current_world_tick,
                    WORLD_TICK_UNIT,
                    incremental_stop_tick,
                    WORLD_TICK_UNIT);
            } else {
                printf(
                    "\r[SIMULATING]  %.1f %s/s  age: %" PRIu64 " %s  "
                    "[p] pause  [q] shutdown\033[K",
                    ticks_per_second,
                    WORLD_TICK_UNIT,
                    current_world_tick,
                    WORLD_TICK_UNIT);
            }
        } else {
            printf(
                "\r[ON HOLD]     [s] resume  [i] increment  "
                "[w] snapshot  [q] shutdown\033[K");
        }

        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        result = select(
            STDIN_FILENO + 1,
            &read_fds,
            NULL,
            NULL,
            &timeout);

        if (result < 0) {
            break;
        }

        if (result == 0) {
            continue;
        }

        key = getchar();

        if (key == EOF) {
            break;
        }

        if (key == 'q') {
            /* to avoid pausing twice, we rely on the code 
             * AFTER the "for" loop to pause, even if it 
             * wouldn't hurt to do it twice 
             */

            /*
            simulation_pause(simulation);
            simulation_wait_until_paused(simulation);
            */
            break;
        }

        if (key == 'p' && main_state == SIMULATING) {
            simulation_pause(simulation);
            simulation_wait_until_paused(simulation);
            main_state = ON_HOLD;
            incremental_active = false;
        }
        else if (key == 'w' && main_state == ON_HOLD) {
           save_snapshot(
            simulation,
            &world,
            absolute_world_path);
        }
        else if (key == 's' && main_state == ON_HOLD) {
            simulation_resume(simulation);
            main_state = SIMULATING;
            incremental_active = false;

            previous_world_tick = simulation_get_world_tick(simulation);
            clock_gettime(CLOCK_MONOTONIC, &previous_time);
        }
        else if (key == 'i' && main_state == ON_HOLD) {
            world_tick_t now;

            now = simulation_get_world_tick(simulation);

            if (simulation_resume_for(
                    simulation,
                    configuration.incremental_steps) != 0) {
                fprintf(
                    stderr,
                    "\nError: Incremental steps overflow the world age.\n");
            } else {
                incremental_stop_tick = now + configuration.incremental_steps;
                incremental_active = true;
                main_state = SIMULATING;

                previous_world_tick = now;
                clock_gettime(CLOCK_MONOTONIC, &previous_time);
            }
        }
    }
 
    disable_operator_input(&original_terminal);

    simulation_pause(simulation);
    simulation_wait_until_paused(simulation);

    world.age = simulation_get_world_tick(simulation);

    simulation_destroy(simulation);

    /*************************
     * Main control loop ends (operator, not simulation)
     *************************/

    if (configuration.save_state_on_shutdown) {
        if (options.verbose) {
            printf("Saving world: %s\n", absolute_world_path);
        }

        if (world_serialize(absolute_world_path, &world) != 0) {
            fprintf(stderr, "Error: Unable to save world %s.\n", absolute_world_path);
            world_destroy(&world);
            return EXIT_FAILURE;
        }
    }

    /* Exit */
    printf("Exiting.\n");

    world_destroy(&world);

    return EXIT_SUCCESS;
}
