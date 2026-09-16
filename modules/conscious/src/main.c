#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char **argv)
{
    options_t options = {
        .verbose = 0,
        .config_file = NULL,
        .name = NULL
    };

    if (parse_arguments(argc, argv, &options) != 0) {
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* initialization */
    printf("Conscious Project - Main and Sync module.\n");

    /* Main logic here */

    /* Exit */
    printf("Exiting.\n");

    return EXIT_SUCCESS;
}

 