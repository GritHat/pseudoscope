#ifndef PLIKE_CONFIG_H
#define PLIKE_CONFIG_H

#include <stdbool.h>

typedef enum { ASSIGN_COLON_EQUALS, ASSIGN_EQUALS } AssignStyle;
typedef enum { PARAMS_DECL, PARAMS_BODY, PARAMS_MIXED } ParamStyle;
typedef enum { OPS_STANDARD, OPS_DOTTED, OPS_MIXED } OperatorStyle;

/* Dialect and code generation settings. */
typedef struct {
    AssignStyle assign;
    ParamStyle params;
    OperatorStyle operators;
    int index_base;      /* first index of arrays declared without a range */
    bool paren_arrays;   /* a(i) is accepted as a[i] */
    bool bounds_check;
} Config;

#define CONFIG_DEFAULT                                                         \
    ((Config){ASSIGN_COLON_EQUALS, PARAMS_MIXED, OPS_MIXED, 1, true, false})

#endif
