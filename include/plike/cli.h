#ifndef PLIKE_CLI_H
#define PLIKE_CLI_H

#include "plike/config.h"

#define PLIKE_VERSION "2.0.0"

typedef enum {
    CMD_TRANSLATE, CMD_CHECK, CMD_BUILD, CMD_RUN, CMD_EXEC, CMD_TOKENS, CMD_AST, CMD_UI
} Command;

typedef enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } ColorMode;

typedef struct {
    Command command;
    Config config;
    const char *input;     /* "-" is the standard input */
    const char *output;    /* NULL: standard output, or a default name */
    const char *cc;
    ColorMode color;
    bool quiet;            /* no warnings */
    bool werror;
    bool verbose;
    bool dot;
    const char *lang;      /* ui: it or en */
    const char *keys;      /* ui without a terminal, for the tests */
    int width, height;
    long max_steps;        /* exec; 0: no limit */
} Options;

enum { EXIT_OK = 0, EXIT_PROBLEMS = 1, EXIT_USAGE = 2, EXIT_IO = 3 };

/* Returns false when the program should stop with *status. */
bool parse_options(int argc, char **argv, Options *options, int *status);

int run_command(const Options *options);

#endif
