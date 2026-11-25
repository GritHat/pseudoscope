#ifndef PSEUDOSCOPE_AST_H
#define PSEUDOSCOPE_AST_H

#include "pseudoscope/lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct Record Record;
typedef struct VarDecl VarDecl;
typedef struct Function Function;
typedef struct Class Class;

typedef struct {
    Expr **items;
    int count, capacity;
} ExprList;

/* ---- types ---- */

/* One array dimension: [lo..hi], [size] (lo is NULL) or [] (both are NULL). */
typedef struct {
    Expr *lo;
    Expr *hi;
} Dim;

typedef struct {
    Dim *items;
    int count, capacity;
} DimList;

/* A type as written, e.g. "2d array of *real". Shared by "var a, b : ...". */
typedef struct {
    Loc loc;
    TokenKind base;     /* TOK_INTEGER... TOK_IDENT (named) or TOK_RECORD */
    const char *name;   /* TOK_IDENT */
    Record *record;     /* TOK_RECORD */
    int pointers;
    bool is_array;
    int rank;           /* from "2d array"; 0 if not given */
    DimList dims;
} TypeSpec;

typedef enum {
    TY_ERROR, TY_VOID, TY_INT, TY_REAL, TY_BOOL, TY_CHAR, TY_STRING,
    TY_POINTER, TY_ARRAY, TY_RECORD
} TypeKind;

struct Type {
    TypeKind kind;
    Type *base;      /* pointer, array */
    DimList dims;    /* array */
    Record *record;  /* record */
    const Class *states; /* integer holding one of the states of a component */
};

struct Record {
    Loc loc;
    const char *name;   /* NULL when anonymous */
    const char *c_name; /* struct tag, set by the analysis */
    VarDecl *fields;
    Record *next;       /* in definition order, set by the analysis */
    Class *klass;       /* when it holds the variables of a class */
    int state;
};

/* ---- expressions ---- */

typedef enum {
    EX_INT, EX_FLOAT, EX_BOOL, EX_STRING, EX_CHAR,
    EX_NAME,
    EX_APPLY,  /* base(args): a call or an array access, told apart later */
    EX_INDEX,  /* base[args] */
    EX_FIELD,  /* base.text */
    EX_UNARY,  /* op base */
    EX_BINARY  /* base op right */
} ExprKind;

struct Expr {
    ExprKind kind;
    Loc loc;
    int depth;
    TokenKind op;
    const char *text;
    Expr *base;
    Expr *right;
    ExprList args;

    /* Filled in by the analysis. */
    Type *type;
    Symbol *symbol;     /* EX_NAME; the callee of a call */
    bool is_call;       /* EX_APPLY, or EX_NAME calling without parentheses */
    bool is_method;     /* the receiver is the first argument */
    bool is_static;     /* super.m(), Base.m(): this very method, whatever the object is */
};

/* ---- declarations ---- */

typedef enum { MODE_IN, MODE_OUT, MODE_INOUT } ParamMode;

struct VarDecl {
    Loc loc;
    const char *name;
    int pointers;       /* stars before the name */
    bool has_dims;      /* brackets after the name */
    DimList dims;
    TypeSpec *spec;     /* NULL for a parameter typed in the body */
    bool inferred;      /* its type was not written */
    bool spec_inherited;/* "a" in "a, b : real" inside a parameter list */
    ParamMode mode;
    Symbol *symbol;
    const VarDecl *origin;  /* of a field that was inherited: where it was written */
    const Class *from;      /* and the class it comes from */
    VarDecl *next;
};

/* What a component does when a message arrives; "when" limits it to some
 * states. */
typedef struct StateName {
    Loc loc;
    const char *name;
    struct StateName *next;
} StateName;

struct Function {
    Loc loc;
    int last_line;
    const char *name;
    const char *full_name;  /* "Stack.push" */
    const char *result_name;/* PSeInt: the variable that holds the result */
    bool is_procedure;
    bool is_handler;
    bool is_test;
    bool is_abstract;       /* only its signature: the classes that extend say what it does */
    bool is_virtual;        /* replaced in some class, or replacing: chosen by the object */
    StateName *guards;
    Class *owner;
    TypeSpec *result;   /* NULL for procedures or when declared in the body */
    VarDecl *params;
    VarDecl *locals;
    Stmt *body;
    Symbol *symbol;
    Function *next;
};

/* A class, or a component: a class that also has states and gets messages. */
typedef struct BaseName {
    Loc loc;
    const char *name;
    Class *klass;           /* set by the analysis; NULL if wrong */
    struct BaseName *next;
} BaseName;

struct Class {
    Loc loc;
    int last_line;
    const char *name;
    bool is_component;
    bool is_abstract;       /* cannot have objects of its own */
    bool is_interface;      /* only signatures */
    bool has_derived;       /* some class extends it */
    BaseName *bases;        /* the classes it extends */
    const Function *pending;/* an abstract method nobody has written yet, if any */
    int mark;
    Record *record;
    StateName *states;
    Function *methods;      /* handlers too */
    Class *next;
};

/* "link a <-> b delay 1..3 loss 10" */
typedef struct Link {
    Loc loc;
    Expr *from, *to;
    bool both_ways;
    Expr *delay, *delay_max, *loss;
    struct Link *next;
} Link;

typedef struct TypeDecl {
    Loc loc;
    const char *name;
    Record *record;
    struct TypeDecl *next;
} TypeDecl;

/* ---- statements ---- */

typedef enum {
    ST_ASSIGN, ST_CALL, ST_IF, ST_WHILE, ST_FOR, ST_REPEAT, ST_RETURN,
    ST_PRINT, ST_READ, ST_BLOCK, ST_VAR,
    ST_SEND,    /* send value to target [after limit] */
    ST_STOP,    /* the simulation ends */
    ST_CANCEL,  /* forget the messages `name` sent to oneself */
    ST_CHECK,   /* check value [, text] */
    ST_RUN,     /* let time pass: until nothing is left, or for value */
    ST_EXPECT,  /* expect target -> limit : name */
    ST_STEP,    /* step text */
    ST_TODO     /* ... */
} StmtKind;

struct Stmt {
    StmtKind kind;
    Loc loc;
    Expr *target;   /* assign, for */
    Expr *value;    /* assign, call, return, conditions, for: first value */
    Expr *limit;    /* for */
    Expr *step;     /* for; may be NULL */
    ExprList args;  /* print, read */
    VarDecl *vars;  /* var */
    const char *name;
    const char *text;
    bool same_line;  /* print without a new line */
    bool joined;     /* print without blanks in between, numbers as short as possible */
    const Class *to_all; /* send ... to all C */
    const char *to_all_name;
    Stmt *body;
    Stmt *otherwise;/* if: an ST_IF (elseif) or an ST_BLOCK (else) */
    Stmt *next;
};

typedef struct {
    TypeDecl *types;
    VarDecl *globals;
    Function *functions;    /* methods, handlers and tests are here too */
    Class *classes;
    Link *links;
    Expr *seed;
    Record *records;        /* filled in by the analysis */
    Symbol *externals;      /* undeclared functions that get called */
} Program;

#endif
