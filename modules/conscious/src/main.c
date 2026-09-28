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
#include <math.h>
#include <time.h>

#include "configuration.h"
#include "world.h"
#include "simulation.h"
#include "simulation_layer_grass.h"
#include "debug.h"

typedef enum {
    ON_HOLD,
    SIMULATING
} main_state_t;

typedef struct {
    int verbose;
    int debug;
    const char *config_file;
    const char *name;
    const char *validate_world_file;
    world_tick_t run_ms;
    int run_ms_set;
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

        case WORLD_LAYER_GRASSAGE:
            return WORLD_LAYER_TYPE_GRASSAGE;

        default:
            return "unknown";
    }
}

static const char *layer_stored_name(world_layer_type_t type)
{
    switch (type) {
        case WORLD_LAYER_HEIGHTMAP:
            return WORLD_LAYER_NAME_HEIGHTMAP;

        case WORLD_LAYER_STATICWATER:
            return WORLD_LAYER_NAME_STATICWATER;

        case WORLD_LAYER_DIFFLIGHT:
            return WORLD_LAYER_NAME_DIFFLIGHT;

        case WORLD_LAYER_HUMIDITY:
            return WORLD_LAYER_NAME_HUMIDITY;

        case WORLD_LAYER_FERTILITY:
            return WORLD_LAYER_NAME_FERTILITY;

        case WORLD_LAYER_GRASS:
            return WORLD_LAYER_NAME_GRASS;

        case WORLD_LAYER_GRASSAGE:
            return WORLD_LAYER_NAME_GRASSAGE;

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

        if (layer->type == WORLD_LAYER_HUMIDITY &&
            layer->payload != NULL) {
            const world_u16_payload_t *grid = layer->payload;

            printf(
                "      cell_size: %" PRIu32 " %s\n",
                grid->cell_size,
                WORLD_DISTANCE_UNIT);
            printf("      value: uint16\n");
        }

        if ((layer->type == WORLD_LAYER_FERTILITY ||
             layer->type == WORLD_LAYER_GRASS ||
             layer->type == WORLD_LAYER_GRASSAGE) &&
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

typedef struct {
    uint64_t n;
    uint64_t nonzero;
    double min;
    double max;
    double mean;
    double m2;
} layer_stats_acc_t;

static void layer_stats_add(layer_stats_acc_t *acc, double value)
{
    double delta;
    double delta2;

    if (acc->n == 0) {
        acc->min = value;
        acc->max = value;
    } else {
        if (value < acc->min) {
            acc->min = value;
        }

        if (value > acc->max) {
            acc->max = value;
        }
    }

    acc->n++;

    if (value != 0.0) {
        acc->nonzero++;
    }

    delta = value - acc->mean;
    acc->mean += delta / (double)acc->n;
    delta2 = value - acc->mean;
    acc->m2 += delta * delta2;
}

static bool layer_cell_count(
    const world_state_t *world,
    uint32_t cell_size,
    uint64_t *count)
{
    if (world == NULL ||
        count == NULL ||
        cell_size == 0 ||
        world->width % cell_size != 0 ||
        world->depth % cell_size != 0) {
        return false;
    }

    *count =
        (uint64_t)(world->width / cell_size) *
        (uint64_t)(world->depth / cell_size);
    return true;
}

/*
 * Published-grid statistics for one layer. min, mean, max, and the
 * population standard deviation cover every cell, including zeros.
 */
static bool compute_layer_stats(
    const world_state_t *world,
    const world_layer_t *layer,
    const char **name,
    uint64_t *nonzero,
    double *min,
    double *mean,
    double *max,
    double *stddev)
{
    layer_stats_acc_t acc = {0};
    uint64_t cell_count;
    uint64_t i;

    if (world == NULL ||
        layer == NULL ||
        layer->payload == NULL ||
        name == NULL ||
        nonzero == NULL ||
        min == NULL ||
        mean == NULL ||
        max == NULL ||
        stddev == NULL) {
        return false;
    }

    *name = layer_stored_name(layer->type);

    switch (layer->type) {
        case WORLD_LAYER_HEIGHTMAP: {
            const world_heightmap_payload_t *grid = layer->payload;

            if (grid->published == NULL ||
                !layer_cell_count(world, grid->cell_size, &cell_count)) {
                return false;
            }

            for (i = 0; i < cell_count; i++) {
                layer_stats_add(&acc, (double)grid->published[i]);
            }
            break;
        }

        case WORLD_LAYER_STATICWATER: {
            const world_staticwater_payload_t *grid = layer->payload;

            if (grid->published == NULL ||
                !layer_cell_count(world, grid->cell_size, &cell_count)) {
                return false;
            }

            for (i = 0; i < cell_count; i++) {
                layer_stats_add(&acc, (double)grid->published[i]);
            }
            break;
        }

        case WORLD_LAYER_DIFFLIGHT: {
            const world_difflight_payload_t *grid = layer->payload;

            if (grid->published == NULL ||
                !layer_cell_count(world, grid->cell_size, &cell_count)) {
                return false;
            }

            for (i = 0; i < cell_count; i++) {
                layer_stats_add(&acc, (double)grid->published[i]);
            }
            break;
        }

        case WORLD_LAYER_HUMIDITY: {
            const world_u16_payload_t *grid = layer->payload;

            if (grid->published == NULL ||
                !layer_cell_count(world, grid->cell_size, &cell_count)) {
                return false;
            }

            for (i = 0; i < cell_count; i++) {
                layer_stats_add(&acc, (double)grid->published[i]);
            }
            break;
        }

        case WORLD_LAYER_FERTILITY:
        case WORLD_LAYER_GRASS:
        case WORLD_LAYER_GRASSAGE: {
            const world_u8_payload_t *grid = layer->payload;

            if (grid->published == NULL ||
                !layer_cell_count(world, grid->cell_size, &cell_count)) {
                return false;
            }

            for (i = 0; i < cell_count; i++) {
                layer_stats_add(&acc, (double)grid->published[i]);
            }
            break;
        }

        default:
            return false;
    }

    if (acc.n == 0) {
        return false;
    }

    *nonzero = acc.nonzero;
    *min = acc.min;
    *mean = acc.mean;
    *max = acc.max;
    *stddev = sqrt(acc.m2 / (double)acc.n);
    return true;
}

static void print_operator_commands(main_state_t state)
{
    if (state == SIMULATING) {
        fputs("[P]ausar - [Q]uitar - [E]stadísticas", stdout);
    } else {
        fputs(
            "[S]eguir - [I]ncremento - snapsho[T] - [Q]uitar - [E]stadísticas",
            stdout);
    }
}

static void print_selected_layer_stats(
    const world_state_t *world,
    int stats_layer)
{
    const world_layer_t *layer;
    const char *name;
    uint64_t nonzero;
    double min;
    double mean;
    double max;
    double stddev;

    if (stats_layer < 0 ||
        world == NULL ||
        world->layers == NULL ||
        (uint32_t)stats_layer >= world->layer_count) {
        return;
    }

    layer = world->layers[stats_layer];

    if (layer == NULL ||
        !compute_layer_stats(
            world,
            layer,
            &name,
            &nonzero,
            &min,
            &mean,
            &max,
            &stddev)) {
        printf(" |  %s", layer != NULL ? layer_stored_name(layer->type) : "?");
        return;
    }

    printf(
        " |  %s  nz=%" PRIu64 "  min=%.0f  mean=%.2f  max=%.0f  sd=%.2f",
        name,
        nonzero,
        min,
        mean,
        max,
        stddev);

    if (layer->type == WORLD_LAYER_GRASS) {
        uint64_t births;
        uint64_t deaths;
        uint64_t births_total;
        uint64_t deaths_total;

        grass_cycle_stats(&births, &deaths);
        grass_total_stats(&births_total, &deaths_total);
        printf(
            "  born=%" PRIu64 "  died=%" PRIu64
            "  Σborn=%" PRIu64 "  Σdied=%" PRIu64,
            births,
            deaths,
            births_total,
            deaths_total);
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
    printf("  -d, --debug             Log operator rate and simulation steps to stderr\n");
    printf("  -c, --config FILE       Configuration file\n");
    printf("  -n, --name NAME         Node/process name\n");
    printf("  -w, --validate-world FILE Validate world file\n");
    printf("  -r, --run-ms MS         Simulate this many world ms, print grass births/deaths, exit\n");
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
        {"debug",   no_argument,       NULL, 'd'},
        {"config",  required_argument, NULL, 'c'},
        {"name",    required_argument, NULL, 'n'},
        {"validate-world", required_argument, NULL, 'w'},
        {"run-ms", required_argument, NULL, 'r'},
        {NULL,      0,                 NULL,  0}
    };

    int option;

    while ((option = getopt_long(argc, argv, "hvdc:n:w:r:", long_options, NULL)) != -1) {
        switch (option) {
        case 'h':
            print_help(argv[0]);
            exit(EXIT_SUCCESS);

        case 'v':
            options->verbose = 1;
            break;

        case 'd':
            options->debug = 1;
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

        case 'r': {
            char *end = NULL;
            unsigned long long value;

            errno = 0;
            value = strtoull(optarg, &end, 10);

            if (errno != 0 ||
                end == optarg ||
                *end != '\0' ||
                value == 0 ||
                value > UINT64_MAX) {
                fprintf(stderr, "Error: --run-ms needs a positive integer.\n");
                return -1;
            }

            options->run_ms = (world_tick_t)value;
            options->run_ms_set = 1;
            break;
        }

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
        .debug = 0,
        .config_file = NULL,
        .name = NULL,
        .validate_world_file = NULL,
        .run_ms = 0,
        .run_ms_set = 0
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

    debug_set(options.debug);
    if (options.debug) {
        debug_log("debug enabled (stderr); status line stays on stdout");
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
    double last_ticks_per_second = 0.0;
    bool incremental_active = false;
    world_tick_t incremental_stop_tick = 0;
    int stats_layer = -1;

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

    if (options.run_ms_set) {
        uint64_t births;
        uint64_t deaths;
        uint64_t births_total;
        uint64_t deaths_total;
        world_tick_t age;
        const world_layer_t *grass_layer;
        const char *name;
        uint64_t nonzero;
        double min;
        double mean;
        double max;
        double stddev;

        printf(
            "Simulating %" PRIu64 " ms (%.2f days)...\n",
            options.run_ms,
            (double)options.run_ms / GRASS_MS_PER_DAY);

        if (simulation_resume_for(simulation, options.run_ms) != 0) {
            fprintf(stderr, "Error: --run-ms overflows the world age.\n");
            simulation_destroy(simulation);
            world_destroy(&world);
            return EXIT_FAILURE;
        }

        while (simulation_get_world_tick(simulation) < options.run_ms) {
            struct timespec wait = {
                .tv_sec = 0,
                .tv_nsec = 50000000L
            };

            nanosleep(&wait, NULL);
        }

        simulation_wait_until_paused(simulation);
        age = simulation_get_world_tick(simulation);
        grass_cycle_stats(&births, &deaths);
        grass_total_stats(&births_total, &deaths_total);

        printf(
            "age %" PRIu64 " ms (%.2f days)\n",
            age,
            (double)age / GRASS_MS_PER_DAY);
        printf(
            "grass last cycle  born=%" PRIu64 "  died=%" PRIu64 "\n",
            births,
            deaths);
        printf(
            "grass total       born=%" PRIu64 "  died=%" PRIu64 "\n",
            births_total,
            deaths_total);

        grass_layer = NULL;

        if (world.layers != NULL) {
            uint32_t i;

            for (i = 0; i < world.layer_count; i++) {
                if (world.layers[i] != NULL &&
                    world.layers[i]->type == WORLD_LAYER_GRASS) {
                    grass_layer = world.layers[i];
                    break;
                }
            }
        }

        if (grass_layer != NULL &&
            compute_layer_stats(
                &world,
                grass_layer,
                &name,
                &nonzero,
                &min,
                &mean,
                &max,
                &stddev)) {
            printf(
                "%s  nz=%" PRIu64 "  min=%.0f  mean=%.2f  max=%.0f  sd=%.2f\n",
                name,
                nonzero,
                min,
                mean,
                max,
                stddev);
        }

        simulation_destroy(simulation);
        world_destroy(&world);
        return EXIT_SUCCESS;
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
            double time_factor;

            current_world_tick = simulation_get_world_tick(simulation);

            clock_gettime(CLOCK_MONOTONIC, &current_time);

            elapsed_seconds =
                (double)(current_time.tv_sec - previous_time.tv_sec) +
                (double)(current_time.tv_nsec - previous_time.tv_nsec) / 1000000000.0;

            ticks_per_second = last_ticks_per_second;

            /*
             * CLK_0016 humidity/fertility steps on a large grid
             * take seconds. world_tick does not move until that
             * step returns, so a 1 s sample can see delta 0.
             * Publishing that 0 would alternate with the burst of
             * empty ticks that follows. Hold the last rate and
             * leave the window open until ticks move, so the stall
             * sits in the denominator of the next sample.
             */
            if (elapsed_seconds >= 0.5) {
                world_tick_t delta_ticks;

                delta_ticks = current_world_tick - previous_world_tick;

                if (delta_ticks > 0) {
                    ticks_per_second =
                        (double)delta_ticks / elapsed_seconds;
                    last_ticks_per_second = ticks_per_second;
                    previous_world_tick = current_world_tick;
                    previous_time = current_time;
                    debug_log(
                        "ui   rate apply elapsed=%.4fs ticks=%" PRIu64
                        " delta=%" PRIu64 " show=%.1f",
                        elapsed_seconds,
                        current_world_tick,
                        delta_ticks,
                        ticks_per_second);
                } else {
                    debug_log(
                        "ui   rate stall elapsed=%.4fs ticks=%" PRIu64
                        " show=%.1f",
                        elapsed_seconds,
                        current_world_tick,
                        ticks_per_second);
                }
            } else {
                debug_log(
                    "ui   rate keep  elapsed=%.4fs ticks=%" PRIu64
                    " delta=%" PRIu64 " show=%.1f",
                    elapsed_seconds,
                    current_world_tick,
                    current_world_tick - previous_world_tick,
                    ticks_per_second);
            }

            /* One tick is one millisecond of world time. */
            time_factor = ticks_per_second / 1000.0;

            if (incremental_active) {
                printf(
                    "\r[SIMULATING]  %.1f %s/s  x%.1f  age: %" PRIu64 " %s  "
                    "until: %" PRIu64 " %s",
                    ticks_per_second,
                    WORLD_TICK_UNIT,
                    time_factor,
                    current_world_tick,
                    WORLD_TICK_UNIT,
                    incremental_stop_tick,
                    WORLD_TICK_UNIT);
            } else {
                printf(
                    "\r[SIMULATING]  %.1f %s/s  x%.1f  age: %" PRIu64 " %s",
                    ticks_per_second,
                    WORLD_TICK_UNIT,
                    time_factor,
                    current_world_tick,
                    WORLD_TICK_UNIT);
            }
        } else {
            printf("\r[ON HOLD]");
        }

        print_selected_layer_stats(&world, stats_layer);
        fputs("  ", stdout);
        print_operator_commands(main_state);
        fputs("\033[K", stdout);

        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        {
            struct timespec wait_start;
            struct timespec wait_end;

            if (debug_on()) {
                clock_gettime(CLOCK_MONOTONIC, &wait_start);
            }

            result = select(
                STDIN_FILENO + 1,
                &read_fds,
                NULL,
                NULL,
                &timeout);

            if (debug_on()) {
                double wait;

                clock_gettime(CLOCK_MONOTONIC, &wait_end);
                wait =
                    (double)(wait_end.tv_sec - wait_start.tv_sec) +
                    (double)(wait_end.tv_nsec - wait_start.tv_nsec) /
                    1000000000.0;

                if (result < 0) {
                    debug_log("ui   select error wait=%.3fs", wait);
                } else if (result == 0) {
                    debug_log("ui   select timeout wait=%.3fs", wait);
                } else {
                    debug_log("ui   select ready wait=%.3fs", wait);
                }
            }
        }

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

        key = tolower((unsigned char)key);
        debug_log("ui   key='%c'", key);

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
        else if (key == 't' && main_state == ON_HOLD) {
           save_snapshot(
            simulation,
            &world,
            absolute_world_path);
        }
        else if (key == 'e') {
            if (world.layer_count == 0) {
                stats_layer = -1;
            } else if (stats_layer + 1 >= (int)world.layer_count) {
                stats_layer = -1;
            } else {
                stats_layer++;
            }

            debug_log("ui   stats_layer=%d of %" PRIu32, stats_layer, world.layer_count);
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
