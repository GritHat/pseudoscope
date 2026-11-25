#include "plike/cli.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct {
    const char *name;
    Command command;
} COMMANDS[] = {
    {"translate", CMD_TRANSLATE}, {"check", CMD_CHECK}, {"build", CMD_BUILD},
    {"run", CMD_RUN}, {"exec", CMD_EXEC}, {"tokens", CMD_TOKENS}, {"ast", CMD_AST},
    {"ui", CMD_UI}, {"test", CMD_TEST},
};

static void print_help(FILE *out) {
    fputs(
        "Usage: pseudoscope [COMMAND] [OPTIONS] FILE [OUTPUT]\n"
        "\n"
        "Pseudocode, and designs, brought to life. FILE may be - for the standard input.\n"
        "\n"
        "Commands:\n"
        "  ui          explore programs and watch them run (FILE may be a folder;\n"
        "              the default without arguments)\n"
        "  translate   write the program in C or in another language (the default command)\n"
        "  check       only report errors and warnings\n"
        "  build       translate and compile to an executable (C, C++, Rust)\n"
        "  run         translate, and execute with the tools of that language\n"
        "  exec        execute here, without other tools, checking every access\n"
        "  test        run the tests of the program (with --to: translated)\n"
        "  tokens      list the tokens\n"
        "  ast         print the syntax tree\n"
        "\n"
        "Language:\n"
        "      --dialect=NAME        plike, pseint, visualg, portugol or cambridge\n"
        "                            (default: from the extension .plike .psc .alg .por .pse)\n"
        "  -a, --assignment=STYLE    colon-equals (default) or equals\n"
        "  -i, --indexing=BASE       first index of arrays without a range:\n"
        "                            one (default) or zero\n"
        "  -p, --params=STYLE        where parameter types go: decl, body or mixed (default)\n"
        "      --operators=STYLE     standard (and), dotted (.and.) or mixed (default)\n"
        "  -m, --mixed-arrays=BOOL   accept a(i) for a[i]: true (default) or false\n"
        "\n"
        "Output:\n"
        "  -t, --to=LANGUAGE         c (default), cpp, java, rust, ts, js or python;\n"
        "                            translate also tells from the extension of the output\n"
        "  -o, --output=FILE         translate: where to write (default: standard output)\n"
        "                            build: executable (default: FILE without extension)\n"
        "  -b, --bounds-check        stop with an error on array indices out of bounds\n"
        "      --cc=COMMAND          the compiler, or what runs the program, for build and run\n"
        "                            (default: $CC or cc, c++, rustc, java, node, python3)\n"
        "      --steps=N             exec: stop after N steps\n"
        "      --canvas=FILE         exec: save what the program drew, as a PPM picture\n"
        "      --lang=LANG           it or en (default: $PSEUDOSCOPE_LANG, or it for ui and en for\n"
        "                            the other commands)\n"
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
    fprintf(stderr, "pseudoscope: invalid value '%s' for --%s (use", value, option);
    for (int i = 0; choices[i]; i++)
        fprintf(stderr, "%s %s", i == 0 ? "" : choices[i + 1] ? "," : " or", choices[i]);
    fputs(")\n", stderr);
    return -1;
}

enum { OPT_OPERATORS = 256, OPT_CC, OPT_DOT, OPT_COLOR, OPT_STEPS, OPT_DIALECT, OPT_LANG, OPT_KEYS, OPT_SIZE, OPT_CANVAS };

bool parse_options(int argc, char **argv, Options *options, int *status) {
    static const struct option LONG[] = {
        {"assignment", required_argument, NULL, 'a'},
        {"indexing", required_argument, NULL, 'i'},
        {"params", required_argument, NULL, 'p'},
        {"operators", required_argument, NULL, OPT_OPERATORS},
        {"mixed-arrays", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'o'},
        {"to", required_argument, NULL, 't'},
        {"bounds-check", no_argument, NULL, 'b'},
        {"cc", required_argument, NULL, OPT_CC},
        {"dot", no_argument, NULL, OPT_DOT},
        {"color", required_argument, NULL, OPT_COLOR},
        {"dialect", required_argument, NULL, OPT_DIALECT},
        {"lang", required_argument, NULL, OPT_LANG},
        {"keys", required_argument, NULL, OPT_KEYS},
        {"size", required_argument, NULL, OPT_SIZE},
        {"steps", required_argument, NULL, OPT_STEPS},
        {"canvas", required_argument, NULL, OPT_CANVAS},
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
    static const char *const DIALECTS[] = {"plike", "pseint", "visualg", "portugol", "cambridge", NULL};
    static const char *const LANGUAGES[] = {"it", "en", NULL};
    static const char *const COLOR[] = {"auto", "always", "never", NULL};

    *options = (Options){.command = CMD_TRANSLATE, .config = CONFIG_DEFAULT, .width = 100, .height = 30};
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
    while ((opt = getopt_long(argc, argv, "a:i:p:m:o:t:bqWvhV", LONG, NULL)) != -1) {
        switch (opt) {
            case 'a':
                value = choose("assignment", optarg, ASSIGNMENT);
                options->config.assign = value == 1 ? ASSIGN_EQUALS : ASSIGN_COLON_EQUALS;
                break;
            case 'i':
                value = choose("indexing", optarg, INDEXING);
                options->config.index_base = value;
                options->config.indexing_given = true;
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
            case 't':
                if (!lang_from_name(optarg, &options->target)) {
                    fprintf(stderr, "pseudoscope: invalid value '%s' for --to (use c, cpp, java, rust, ts, js or python)\n", optarg);
                    return false;
                }
                options->target_given = true;
                break;
            case 'b': options->config.bounds_check = true; break;
            case OPT_CC: options->cc = optarg; break;
            case OPT_DIALECT:
                value = choose("dialect", optarg, DIALECTS);
                options->config.dialect = (Dialect)(value < 0 ? 0 : value);
                options->config.dialect_given = true;
                break;
            case OPT_LANG:
                value = choose("lang", optarg, LANGUAGES);
                options->lang = optarg;
                break;
            case OPT_KEYS: options->keys = optarg; break;
            case OPT_SIZE:
                if (sscanf(optarg, "%dx%d", &options->width, &options->height) != 2 ||
                    options->width < 1 || options->width > 500 || options->height < 1 || options->height > 200) {
                    fprintf(stderr, "pseudoscope: invalid value '%s' for --size\n", optarg);
                    return false;
                }
                break;
            case OPT_STEPS: {
                char *end;
                options->max_steps = strtol(optarg, &end, 10);
                if (*end || end == optarg || options->max_steps <= 0) {
                    fprintf(stderr, "pseudoscope: invalid value '%s' for --steps\n", optarg);
                    return false;
                }
                break;
            }
            case OPT_DOT: options->dot = true; break;
            case OPT_CANVAS: options->canvas = optarg; break;
            case 'q': options->quiet = true; break;
            case 'W': options->werror = true; break;
            case 'v': options->verbose = true; break;
            case 'h':
                print_help(stdout);
                *status = EXIT_OK;
                return false;
            case 'V':
                puts("pseudoscope " PLIKE_VERSION);
                *status = EXIT_OK;
                return false;
            default:
                fputs("Try 'pseudoscope --help'.\n", stderr);
                return false;
        }
        if (value < 0) return false;
    }

    if (optind >= argc && (options->command == CMD_UI ||
                           (argc == 1 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)))) {
        options->command = CMD_UI;
        options->input = ".";
        return true;
    }
    if (optind >= argc) {
        fputs("pseudoscope: no input file\nTry 'pseudoscope --help'.\n", stderr);
        return false;
    }
    options->input = argv[optind++];
    if (optind < argc && !options->output) options->output = argv[optind++];
    if (optind < argc) {
        fprintf(stderr, "pseudoscope: unexpected argument '%s'\n", argv[optind]);
        return false;
    }
    return true;
}
