#ifndef PLIKE_DIAG_H
#define PLIKE_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    int line;   /* 1-based; 0 means "no position" */
    int column; /* 1-based, in bytes */
} Loc;

typedef struct {
    const char *name;
    const char *text; /* NUL-terminated */
    size_t length;
} Source;

typedef struct {
    const Source *source;
    FILE *out;
    bool color;
    bool silent_warnings;
    int errors;
    int warnings;
    bool too_many;
    Loc last_error;
} Diag;

#define DIAG_MAX_ERRORS 20

void diag_error(Diag *diag, Loc loc, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void diag_warning(Diag *diag, Loc loc, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#endif
