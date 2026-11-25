#ifndef PSEUDOSCOPE_SEMA_H
#define PSEUDOSCOPE_SEMA_H

#include "pseudoscope/ast.h"

typedef enum { SYM_VAR, SYM_PARAM, SYM_RESULT, SYM_FUNCTION,
    SYM_FIELD,   /* of the class: stands for self.field */
    SYM_STATE,   /* of the component: a constant */
    SYM_NOW,     /* the time of the simulation */
    SYM_SENDER   /* who sent the message being handled */
} SymbolKind;

struct Symbol {
    SymbolKind kind;
    Loc loc;
    const char *name;
    const char *c_name;   /* unique and safe to use in C */
    Type *type;           /* for functions: the result, TY_VOID for procedures */
    Symbol *next;         /* in the scope; for externals, in the program list */

    int slot;             /* for the virtual machine: place in the frame, or in
                             the record for a field */
    const Symbol *field;  /* SYM_FIELD */
    long long value;      /* SYM_STATE */

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

/* Components and tests only run on the virtual machine. */
bool has_components(const Program *program);
bool has_tests(const Program *program);

/* The class of a type, also through a pointer; NULL if none. */
const Class *class_of(const Type *type);
/* Its own, or the one it inherits. */
const Function *find_method(const Program *program, const Class *klass, const char *name, bool handler);
/* Whether klass is base, or extends it, however far. */
bool derives_from(const Class *klass, const Class *base);
bool has_inheritance(const Program *program);

/* Value of a constant integer expression. */
bool const_int(const Expr *expr, long long *value);

/* "array[1..3] of integer" and the like, for messages. */
const char *type_name(Arena *arena, const Type *type);

#endif
