#include "plike/cli.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    Command command;
} COMMANDS[] = {
    {"translate", CMD_TRANSLATE}, {"check", CMD_CHECK}, {"build", CMD_BUILD},
    {"run", CMD_RUN}, {"tokens", CMD_TOKENS}, {"ast", CMD_AST},
};

static void print_help(FILE *out) {
    fputs(
        "Usage: plike [COMMAND] [OPTIONS] FILE [OUTPUT]\n"
        "\n"
        "Translates P-like pseudocode to C. FILE may be - for the standard input.\n"
        "\n"
        "Commands:\n"
        "  translate   write the C code (the default command)\n"
        "  check       only report errors and warnings\n"
        "  build       translate and compile to an executable\n"
        "  run         build in a temporary directory and execute\n"
        "  tokens      list the tokens\n"
        "  ast         print the syntax tree\n"
        "\n"
        "Language:\n"
        "  -a, --assignment=STYLE    colon-equals (default) or equals\n"
        "  -i, --indexing=BASE       first index of arrays without a range:\n"
        "                            one (default) or zero\n"
        "  -p, --params=STYLE        where parameter types go: decl, body or mixed (default)\n"
        "      --operators=STYLE     standard (and), dotted (.and.) or mixed (default)\n"
        "  -m, --mixed-arrays=BOOL   accept a(i) for a[i]: true (default) or false\n"
        "\n"
        "Output:\n"
        "  -o, --output=FILE         translate: C file (default: standard output)\n"
        "                            build: executable (default: FILE without extension)\n"
        "  -b, --bounds-check        stop with an error on array indices out of bounds\n"
        "      --cc=COMPILER         C compiler for build and run (default: $CC or cc)\n"
        "      --dot                 ast: Graphviz format\n"
        "\n"
        "Messages:\n"
        "      --color=WHEN          auto (default), always or never\n"
        "  -q, --quiet               no warnings\n"
        "  -W, --werror              treat warnings as errors\n"
        "  -v, --verbose             show the steps\n"
        "  -h, --help                show this help\n"
        "  -V, --version             show the version\n"
        "\n"
        "Exit status: 0 success, 1 errors in the program, 2 wrong usage, 3 files or\n"
        "compiler. run exits with the status of the program.\n",
        out);
}

/* Index of value among the NULL-terminated choices, or -1 with a message. */
static int choose(const char *option, const char *value, const char *const *choices) {
    for (int i = 0; choices[i]; i++)
        if (strcmp(value, choices[i]) == 0) return i;
    fprintf(stderr, "plike: invalid value '%s' for --%s (use", value, option);
    for (int i = 0; choices[i]; i++)
        fprintf(stderr, "%s %s", i == 0 ? "" : choices[i + 1] ? "," : " or", choices[i]);
    fputs(")\n", stderr);
    return -1;
}

enum { OPT_OPERATORS = 256, OPT_CC, OPT_DOT, OPT_COLOR };

bool parse_options(int argc, char **argv, Options *options, int *status) {
    static const struct option LONG[] = {
        {"assignment", required_argument, NULL, 'a'},
        {"indexing", required_argument, NULL, 'i'},
        {"params", required_argument, NULL, 'p'},
        {"operators", required_argument, NULL, OPT_OPERATORS},
        {"mixed-arrays", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'o'},
        {"bounds-check", no_argument, NULL, 'b'},
        {"cc", required_argument, NULL, OPT_CC},
        {"dot", no_argument, NULL, OPT_DOT},
        {"color", required_argument, NULL, OPT_COLOR},
        {"quiet", no_argument, NULL, 'q'},
        {"werror", no_argument, NULL, 'W'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };
    static const char *const ASSIGNMENT[] = {"colon-equals", "equals", NULL};
    static const char *const INDEXING[] = {"zero", "one", NULL};
    static const char *const PARAMS[] = {"decl", "body", "mixed", NULL};
    static const char *const OPERATORS[] = {"standard", "dotted", "mixed", NULL};
    static const char *const BOOLEAN[] = {"false", "true", NULL};
    static const char *const COLOR[] = {"auto", "always", "never", NULL};

    *options = (Options){.command = CMD_TRANSLATE, .config = CONFIG_DEFAULT};
    *status = EXIT_USAGE;

    if (argc > 1) {
        for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
            if (strcmp(argv[1], COMMANDS[i].name) != 0) continue;
            options->command = COMMANDS[i].command;
            argv[1] = argv[0];
            argv++;
            argc--;
            break;
        }
    }

    int opt, value = 0;
    while ((opt = getopt_long(argc, argv, "a:i:p:m:o:bqWvhV", LONG, NULL)) != -1) {
        switch (opt) {
            case 'a':
                value = choose("assignment", optarg, ASSIGNMENT);
                options->config.assign = value == 1 ? ASSIGN_EQUALS : ASSIGN_COLON_EQUALS;
                break;
            case 'i':
                value = choose("indexing", optarg, INDEXING);
                options->config.index_base = value;
                break;
            case 'p':
                value = choose("params", optarg, PARAMS);
                options->config.params = value == 0 ? PARAMS_DECL
                                       : value == 1 ? PARAMS_BODY : PARAMS_MIXED;
                break;
            case OPT_OPERATORS:
                value = choose("operators", optarg, OPERATORS);
                options->config.operators = value == 0 ? OPS_STANDARD
                                          : value == 1 ? OPS_DOTTED : OPS_MIXED;
                break;
            case 'm':
                value = choose("mixed-arrays", optarg, BOOLEAN);
                options->config.paren_arrays = value == 1;
                break;
            case OPT_COLOR:
                value = choose("color", optarg, COLOR);
                options->color = value == 1 ? COLOR_ALWAYS : value == 2 ? COLOR_NEVER : COLOR_AUTO;
                break;
            case 'o': options->output = optarg; break;
            case 'b': options->config.bounds_check = true; break;
            case OPT_CC: options->cc = optarg; break;
            case OPT_DOT: options->dot = true; break;
            case 'q': options->quiet = true; break;
            case 'W': options->werror = true; break;
            case 'v': options->verbose = true; break;
            case 'h':
                print_help(stdout);
                *status = EXIT_OK;
                return false;
            case 'V':
                puts("plike " PLIKE_VERSION);
                *status = EXIT_OK;
                return false;
            default:
                fputs("Try 'plike --help'.\n", stderr);
                return false;
        }
        if (value < 0) return false;
    }

    if (optind >= argc) {
        fputs("plike: no input file\nTry 'plike --help'.\n", stderr);
        return false;
    }
    options->input = argv[optind++];
    if (optind < argc && !options->output) options->output = argv[optind++];
    if (optind < argc) {
        fprintf(stderr, "plike: unexpected argument '%s'\n", argv[optind]);
        return false;
    }
    return true;
}
