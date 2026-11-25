#ifndef PSEUDOSCOPE_CLI_H
#define PSEUDOSCOPE_CLI_H

#include "pseudoscope/config.h"
#include "pseudoscope/target.h"

#define PSEUDOSCOPE_VERSION "1.0.0"

typedef enum {
    CMD_TRANSLATE, CMD_CHECK, CMD_BUILD, CMD_RUN, CMD_EXEC, CMD_TOKENS, CMD_AST, CMD_UI, CMD_TEST
} Command;

typedef enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } ColorMode;

typedef struct {
    Command command;
    Config config;
    const char *input;     /* "-" is the standard input */
    const char *output;    /* NULL: standard output, or a default name */
    const char *cc;        /* the compiler, or what runs the program */
    Lang target;           /* the language to translate to */
    bool target_given;
    ColorMode color;
    bool quiet;            /* no warnings */
    bool werror;
    bool verbose;
    bool dot;
    const char *lang;      /* ui: it or en */
    const char *keys;      /* ui without a terminal, for the tests */
    int width, height;
    long max_steps;        /* exec; 0: no limit */
    const char *canvas;    /* exec: where to save what the program drew */
} Options;

enum { EXIT_OK = 0, EXIT_PROBLEMS = 1, EXIT_USAGE = 2, EXIT_IO = 3 };

/* Returns false when the program should stop with *status. */
bool parse_options(int argc, char **argv, Options *options, int *status);

int run_command(const Options *options);

#endif
