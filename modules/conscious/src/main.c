#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int verbose;
    const char *config_file;
    const char *name;
} options_t;

static void print_help(const char *program)
{
    printf("Usage: %s [OPTIONS]\n\n", program);
    printf("Options:\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -c, --config FILE       Configuration file\n");
    printf("  -n, --name NAME         Node/process name\n");
}

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

int main(int argc, char **argv)
{
    options_t options = {
        .verbose = 0,
        .config_file = NULL,
        .name = NULL
    };

    char executable_dir[PATH_MAX];
    char default_config[PATH_MAX];

    if (parse_arguments(argc, argv, &options) != 0) {
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /*
     * Use conscious.cfg from the executable directory
     * when no configuration file was specified.
     */
    if (options.config_file == NULL) {
        if (get_executable_directory(
                executable_dir,
                sizeof(executable_dir)) != 0) {

            fprintf(stderr, "Unable to determine executable directory.\n");
            return EXIT_FAILURE;
        }

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

    printf("Configuration: %s\n", options.config_file);

    /* Main logic here */

    /* Exit */
    printf("Exiting.\n");

    return EXIT_SUCCESS;
}