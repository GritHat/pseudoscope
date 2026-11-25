#ifndef PLIKE_AST_H
#define PLIKE_AST_H

#include "plike/lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct Record Record;
typedef struct VarDecl VarDecl;
typedef struct Function Function;

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
};

struct Record {
    Loc loc;
    const char *name;   /* NULL when anonymous */
    const char *c_name; /* struct tag, set by the analysis */
    VarDecl *fields;
    Record *next;       /* in definition order, set by the analysis */
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
    bool spec_inherited;/* "a" in "a, b : real" inside a parameter list */
    ParamMode mode;
    Symbol *symbol;
    VarDecl *next;
};

struct Function {
    Loc loc;
    const char *name;
    bool is_procedure;
    TypeSpec *result;   /* NULL for procedures or when declared in the body */
    VarDecl *params;
    VarDecl *locals;
    Stmt *body;
    Symbol *symbol;
    Function *next;
};

typedef struct TypeDecl {
    Loc loc;
    const char *name;
    Record *record;
    struct TypeDecl *next;
} TypeDecl;

/* ---- statements ---- */

typedef enum {
    ST_ASSIGN, ST_CALL, ST_IF, ST_WHILE, ST_FOR, ST_REPEAT, ST_RETURN,
    ST_PRINT, ST_READ, ST_BLOCK, ST_VAR
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
    Stmt *body;
    Stmt *otherwise;/* if: an ST_IF (elseif) or an ST_BLOCK (else) */
    Stmt *next;
};

typedef struct {
    TypeDecl *types;
    VarDecl *globals;
    Function *functions;
    Record *records;        /* filled in by the analysis */
    Symbol *externals;      /* undeclared functions that get called */
} Program;

#endif
