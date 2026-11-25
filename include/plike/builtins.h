#ifndef PLIKE_BUILTINS_H
#define PLIKE_BUILTINS_H

#include <stdbool.h>

/* The functions every program can use without declaring them. */
typedef struct {
    const char *name;
    int arity;
    char result;        /* i integer, r real, t text, b logical, v nothing, n as its arguments */
    const char *takes;  /* a letter each: n number, t text, a anything simple */
    bool here_only;     /* files, drawings, texts and numbers: run here, not translated */
} Builtin;

const Builtin *builtin_named(const char *name);

#endif
