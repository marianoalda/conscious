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
} options_t;

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
        {NULL,      0,                 NULL,  0}
    };

    int option;

    while ((option = getopt_long(argc, argv, "hvc:n:", long_options, NULL)) != -1) {
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
        .name = NULL
    };

    configuration_t configuration = {
        .save_state_on_shutdown = true,
        .state_on_start = SIMULATE,
        .world_file = "world.bin"
    };

    char executable_dir[PATH_MAX];
    char default_config[PATH_MAX];
    char absolute_world_path[PATH_MAX];

    FILE *config_file = NULL;

    if (parse_arguments(argc, argv, &options) != 0) {
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
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

    if (world_load(absolute_world_path) != 0) {
        fprintf(stderr, "Error: Unable to load world %s.\n", absolute_world_path);
        return EXIT_FAILURE;
    }

    /*************************
     * Main simulation loop begins
     *************************/

    simulation_t *simulation = NULL;
    struct termios original_terminal;
    main_state_t main_state;

    long long previous_step_count = 0;

    if (simulation_init(&simulation) != 0) {
        fprintf(stderr, "Error: Unable to initialize simulation.\n");
        return EXIT_FAILURE;
    }

    if (simulation_start(simulation) != 0) {
        fprintf(stderr, "Error: Unable to start simulation.\n");
        simulation_destroy(simulation);
        return EXIT_FAILURE;
    }

    if (configuration.state_on_start == SIMULATE) {
        main_state = SIMULATING;

        simulation_resume(simulation);
    }
    else {
        main_state = ON_HOLD;

        simulation_pause(simulation);
        simulation_wait_until_paused(simulation);
    }

    if (enable_operator_input(&original_terminal) != 0) {
        fprintf(stderr, "Error: Unable to configure terminal input.\n");
        simulation_destroy(simulation);
        return EXIT_FAILURE;
    }

    for (;;) {
        int key;
        int result;
        fd_set read_fds;
        struct timeval timeout;

        if (main_state == SIMULATING) {
            long long current_step_count;
            long long steps_per_second;

            current_step_count = simulation_get_step_count(simulation);
            steps_per_second = current_step_count - previous_step_count;
            previous_step_count = current_step_count;

            printf(
                "\r[SIMULATING]  %lld steps/s  total: %lld  "
                "[p] pause  [q] shutdown\033[K",
                steps_per_second,
                current_step_count);
        }
        else {
            printf(
                "\r[ON HOLD]     [s] resume  [q] shutdown\033[K");
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
            simulation_pause(simulation);
            simulation_wait_until_paused(simulation);
            break;
        }

        if (key == 'p' && main_state == SIMULATING) {
            simulation_pause(simulation);
            simulation_wait_until_paused(simulation);
            main_state = ON_HOLD;
        }
        else if (key == 's' && main_state == ON_HOLD) {
            simulation_resume(simulation);
            main_state = SIMULATING;
        }
    }
 
    disable_operator_input(&original_terminal);

    simulation_destroy(simulation);

    /*************************
     * Main simulation loop ends
     *************************/

    if (configuration.save_state_on_shutdown) {
        if (options.verbose) {
            printf("Saving world: %s\n", absolute_world_path);
        }

        if (world_save(absolute_world_path) != 0) {
            fprintf(stderr, "Error: Unable to save world %s.\n", absolute_world_path);
            return EXIT_FAILURE;
        }
    }

    /* Exit */
    printf("Exiting.\n");

    return EXIT_SUCCESS;
}
