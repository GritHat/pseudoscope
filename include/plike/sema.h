#ifndef PLIKE_SEMA_H
#define PLIKE_SEMA_H

#include "plike/ast.h"

typedef enum { SYM_VAR, SYM_PARAM, SYM_RESULT, SYM_FUNCTION } SymbolKind;

struct Symbol {
    SymbolKind kind;
    Loc loc;
    const char *name;
    const char *c_name;   /* unique and safe to use in C */
    Type *type;           /* for functions: the result, TY_VOID for procedures */
    Symbol *next;         /* in the scope; for externals, in the program list */

    bool is_global;
    bool indexed;         /* some array access goes through it */

    ParamMode mode;       /* SYM_PARAM */
    bool by_reference;    /* scalar out or inout parameter: a pointer in C */
    bool flattened;       /* its inner bounds use a later parameter */
    int position;

    Function *function;   /* SYM_FUNCTION; NULL when external */
    const char *builtin;  /* SYM_FUNCTION: C library function */
    Symbol **params;
    int param_count;
    Symbol *result;
};

/* Resolves names and types. Problems go to diag. Returns whether the program
 * can be translated. */
bool analyze(Program *program, const Config *config, Arena *arena, Diag *diag);

/* Value of a constant integer expression. */
bool const_int(const Expr *expr, long long *value);

/* "array[1..3] of integer" and the like, for messages. */
const char *type_name(Arena *arena, const Type *type);

#endif
