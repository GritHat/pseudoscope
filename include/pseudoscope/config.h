#ifndef PSEUDOSCOPE_CONFIG_H
#define PSEUDOSCOPE_CONFIG_H

#include <stdbool.h>

typedef enum { ASSIGN_COLON_EQUALS, ASSIGN_EQUALS } AssignStyle;
typedef enum { PARAMS_DECL, PARAMS_BODY, PARAMS_MIXED } ParamStyle;
typedef enum { OPS_STANDARD, OPS_DOTTED, OPS_MIXED } OperatorStyle;

/* The pseudocodes that can be read. */
typedef enum { DIALECT_PLIKE, DIALECT_PSEINT, DIALECT_VISUALG, DIALECT_PORTUGOL, DIALECT_CAMBRIDGE, DIALECT_CORMEN } Dialect;

/* Dialect and code generation settings. */
typedef struct {
    AssignStyle assign;
    ParamStyle params;
    OperatorStyle operators;
    int index_base;      /* first index of arrays declared without a range */
    bool paren_arrays;   /* a(i) is accepted as a[i] */
    bool bounds_check;
    Dialect dialect;
    bool dialect_given, indexing_given;
    bool cormen;         /* rewritten from Cormen's pseudocode: "r" and "R" are two names, and it says TRUE and FALSE */
} Config;

#define CONFIG_DEFAULT                                                         \
    ((Config){ASSIGN_COLON_EQUALS, PARAMS_MIXED, OPS_MIXED, 1, true, false, DIALECT_PLIKE, false, false, false})

/* The same settings, with those that depend on the kind of file. */
Config config_for(Config config, const char *path);

#endif
