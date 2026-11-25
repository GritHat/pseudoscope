#include "plike/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 500

typedef struct {
    const Token *tokens;
    int count, pos;
    const Config *config;
    Arena *arena;
    Diag *diag;
    bool panic;     /* an error was reported and not recovered from yet */
    int depth;      /* nesting of statements and expressions */
    int brackets;   /* open ( and [ around the current expression */
    Program *program;
    TypeDecl **types_tail;
    VarDecl **globals_tail;
    Function **functions_tail;
    Class **classes_tail;
    Link **links_tail;
    Class *klass;   /* being parsed */
    bool in_test;
    bool signature_only; /* the next method is abstract */

    /* other dialects */
    Function *function;         /* being read */
    struct Dimensioned *dimensioned;
    struct Constant *constants;
    Stmt *initial;              /* first values of the globals */
    int switches;
} Parser;

typedef struct Dimensioned {
    VarDecl *decl;
    struct Dimensioned *next;
} Dimensioned;

typedef struct Constant {
    const char *name;
    Expr *value;
    struct Constant *next;
} Constant;

static bool foreign(const Parser *p) { return p->config->dialect != DIALECT_PLIKE; }
static Stmt *foreign_statement(Parser *p, bool *handled);
static bool is_case_label(const Parser *p);
static Stmt *parse_statement(Parser *p);
static Stmt *parse_simple(Parser *p);
static Stmt *parse_return(Parser *p);
static Stmt *return_of(Parser *p, const char *name, Loc loc);
static Stmt *parse_io(Parser *p, StmtKind kind);
static bool parse_dims(Parser *p, DimList *dims);

static Expr *parse_expr(Parser *p);
static Expr *parse_postfix(Parser *p);
static Expr *parse_unary(Parser *p);
static bool starts_type(const Parser *p);
static Stmt *parse_statements(Parser *p);
static VarDecl **parse_var_group(Parser *p, VarDecl **tail);
static VarDecl **parse_var_section(Parser *p, VarDecl **tail, bool bare_names);
static TypeSpec *parse_type(Parser *p);
static void parse_type_decl(Parser *p);

/* Words that mean something only in some places, so that programs can go on
 * using them as names. */
static bool is_word(const Token *token, const char *word) {
    return token->kind == TOK_IDENT && equals_ignore_case(token->text, word);
}

/* ---- tokens ---- */

static const Token *cur(const Parser *p) { return &p->tokens[p->pos]; }

static const Token *peek(const Parser *p, int offset) {
    int index = p->pos + offset;
    return &p->tokens[index < p->count ? index : p->count - 1];
}

static bool check(const Parser *p, TokenKind kind) { return cur(p)->kind == kind; }

static const Token *advance(Parser *p) {
    const Token *token = cur(p);
    if (token->kind != TOK_EOF) p->pos++;
    return token;
}

static bool match(Parser *p, TokenKind kind) {
    if (!check(p, kind)) return false;
    advance(p);
    return true;
}

static void syntax_error(Parser *p, const char *expected) {
    if (p->panic) return;
    p->panic = true;
    const Token *token = cur(p);
    expected = diag_text(p->diag, expected);
    switch (token->kind) {
        case TOK_IDENT: case TOK_INT: case TOK_FLOAT:
            diag_error(p->diag, token->loc, "expected %s, found '%s'", expected, token->text);
            break;
        default: {
            char found[40];
            if (token->kind >= TOK_PROGRAM && token->text[0]) snprintf(found, sizeof found, "'%.36s'", token->text);
            else snprintf(found, sizeof found, "%s", diag_text(p->diag, token_kind_name(token->kind)));
            diag_error(p->diag, token->loc, "expected %s, found %s", expected, found);
        }
    }
}

static bool expect(Parser *p, TokenKind kind, const char *expected) {
    if (match(p, kind)) return true;
    syntax_error(p, expected);
    return false;
}

static bool enter(Parser *p) {
    if (p->depth < MAX_DEPTH) {
        p->depth++;
        return true;
    }
    if (!p->panic) diag_error(p->diag, cur(p)->loc, "code is nested too deeply");
    p->panic = true;
    return false;
}

static void leave(Parser *p) { p->depth--; }

/* ---- expressions ---- */

static void list_add(Parser *p, ExprList *list, Expr *expr) {
    if (list->count == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 4;
        Expr **items = arena_alloc(p->arena, (size_t)capacity * sizeof(Expr *));
        if (list->count) memcpy(items, list->items, (size_t)list->count * sizeof(Expr *));
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = expr;
}

static Expr *new_expr(Parser *p, ExprKind kind, Loc loc, Expr *base, Expr *right) {
    Expr *expr = ARENA_NEW(p->arena, Expr);
    expr->kind = kind;
    expr->loc = loc;
    expr->base = base;
    expr->right = right;
    int depth = 0;
    if (base && base->depth > depth) depth = base->depth;
    if (right && right->depth > depth) depth = right->depth;
    expr->depth = depth + 1;
    if (expr->depth > MAX_DEPTH * 2 && !p->panic) {
        diag_error(p->diag, loc, "expression is too long");
        p->panic = true;
    }
    return expr;
}

static Expr *new_leaf(Parser *p, ExprKind kind) {
    const Token *token = advance(p);
    Expr *expr = new_expr(p, kind, token->loc, NULL, NULL);
    expr->text = token->text;
    expr->op = token->kind;
    return expr;
}

/* Arguments up to the closing token, which is consumed. */
static bool parse_args(Parser *p, ExprList *args, TokenKind close, const char *expected) {
    p->brackets++;
    if (!check(p, close)) {
        do {
            Expr *arg = parse_expr(p);
            if (!arg) {
                p->brackets--;
                return false;
            }
            list_add(p, args, arg);
            while (p->config->dialect == DIALECT_VISUALG && check(p, TOK_COLON)) { /* x:8:2, a width */
                advance(p);
                match(p, TOK_INT);
            }
        } while (match(p, TOK_COMMA));
    }
    p->brackets--;
    return expect(p, close, expected);
}

static Expr *parse_primary(Parser *p) {
    switch (cur(p)->kind) {
        case TOK_INT: return new_leaf(p, EX_INT);
        case TOK_FLOAT: return new_leaf(p, EX_FLOAT);
        case TOK_STRING: return new_leaf(p, EX_STRING);
        case TOK_CHAR: return new_leaf(p, EX_CHAR);
        case TOK_TRUE: case TOK_FALSE: return new_leaf(p, EX_BOOL);
        case TOK_IDENT:
            for (const struct Constant *c = p->constants; c; c = c->next) {
                if (strcmp(c->name, cur(p)->text) == 0) {
                    advance(p);
                    return c->value;
                }
            }
            return new_leaf(p, EX_NAME);
        case TOK_LPAREN: {
            advance(p);
            p->brackets++;
            Expr *expr = parse_expr(p);
            p->brackets--;
            if (!expr || !expect(p, TOK_RPAREN, "')'")) return NULL;
            return expr;
        }
        default:
            syntax_error(p, "an expression");
            return NULL;
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *expr = parse_primary(p);
    while (expr) {
        Loc loc = cur(p)->loc;
        if (match(p, TOK_LBRACKET)) {
            expr = new_expr(p, EX_INDEX, loc, expr, NULL);
            if (!parse_args(p, &expr->args, TOK_RBRACKET, "']'")) return NULL;
            if (expr->args.count == 0) {
                diag_error(p->diag, loc, "missing array index");
                return NULL;
            }
        } else if (check(p, TOK_LPAREN) && expr->kind != EX_INT && expr->kind != EX_FLOAT) {
            advance(p);
            expr = new_expr(p, EX_APPLY, loc, expr, NULL);
            if (!parse_args(p, &expr->args, TOK_RPAREN, "')'")) return NULL;
        } else if (check(p, TOK_DOT) || check(p, TOK_ARROW)) {
            TokenKind op = advance(p)->kind;
            if (!check(p, TOK_IDENT)) {
                syntax_error(p, "a field name");
                return NULL;
            }
            expr = new_expr(p, EX_FIELD, cur(p)->loc, expr, NULL);
            expr->op = op;
            expr->text = advance(p)->text;
        } else {
            return expr;
        }
        if (p->panic) return NULL;
    }
    return NULL;
}

static Expr *parse_unary(Parser *p) {
    switch (cur(p)->kind) {
        case TOK_MINUS: case TOK_PLUS: case TOK_NOT: case TOK_TILDE:
        case TOK_STAR: case TOK_AMP: case TOK_AT: {
            if (!enter(p)) return NULL;
            const Token *op = advance(p);
            Expr *operand = parse_unary(p);
            leave(p);
            if (!operand) return NULL;
            Expr *expr = new_expr(p, EX_UNARY, op->loc, operand, NULL);
            expr->op = op->kind;
            return expr;
        }
        default:
            return parse_postfix(p);
    }
}

/* Same levels as C. */
static int precedence(TokenKind op) {
    switch (op) {
        case TOK_OR: return 1;
        case TOK_AND: return 2;
        case TOK_PIPE: return 3;
        case TOK_CARET: return 4;
        case TOK_AMP: return 5;
        case TOK_EQ: case TOK_NE: return 6;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 7;
        case TOK_SHL: case TOK_SHR: return 8;
        case TOK_PLUS: case TOK_MINUS: case TOK_CONCAT: return 9;
        case TOK_STAR: case TOK_SLASH: case TOK_MOD: case TOK_RDIV: case TOK_IDIV: return 10;
        case TOK_POWER: return 11;
        default: return 0;
    }
}

static Expr *parse_binary(Parser *p, int min_precedence) {
    Expr *left = parse_unary(p);
    while (left && !p->panic) {
        const Token *op = cur(p);
        TokenKind kind = op->kind;
        /* "a = b" compares, even when '=' is also the assignment operator. */
        if (kind == TOK_ASSIGN && strcmp(op->text, "=") == 0 && !foreign(p)) kind = TOK_EQ;
        int level = precedence(kind);
        if (!level || level < min_precedence) break;
        /* Statements need no ';', so a '*' that opens a line starts a new one:
         *     x := a
         *     *p := b                                                        */
        if (kind == TOK_STAR && op->starts_line && p->brackets == 0) break;
        advance(p);
        Expr *right = parse_binary(p, level + 1);
        if (!right) return NULL;
        if (kind == TOK_POWER) {
            Expr *name = new_expr(p, EX_NAME, op->loc, NULL, NULL);
            name->text = "pow";
            Expr *call = new_expr(p, EX_APPLY, op->loc, name, NULL);
            list_add(p, &call->args, left);
            list_add(p, &call->args, right);
            call->depth = (left->depth > right->depth ? left->depth : right->depth) + 1;
            left = call;
            continue;
        }
        if (kind == TOK_IDIV) kind = TOK_SLASH;
        left = new_expr(p, EX_BINARY, op->loc, left, right);
        left->op = kind;
    }
    return p->panic ? NULL : left;
}

static Expr *parse_expr(Parser *p) {
    if (!enter(p)) return NULL;
    Expr *expr = parse_binary(p, 1);
    leave(p);
    return expr;
}

/* ---- types and declarations ---- */

static bool is_open_bracket(const Parser *p) {
    return check(p, TOK_LBRACKET) || (p->config->paren_arrays && check(p, TOK_LPAREN));
}

static void dim_add(Parser *p, DimList *dims, Dim dim) {
    if (dims->count == dims->capacity) {
        int capacity = dims->capacity ? dims->capacity * 2 : 4;
        Dim *items = arena_alloc(p->arena, (size_t)capacity * sizeof(Dim));
        if (dims->count) memcpy(items, dims->items, (size_t)dims->count * sizeof(Dim));
        dims->items = items;
        dims->capacity = capacity;
    }
    dims->items[dims->count++] = dim;
}

/* [n], [1..n, 0..m], [n][m], [], [,m] and the same with parentheses. */
static bool parse_dims(Parser *p, DimList *dims) {
    while (is_open_bracket(p)) {
        TokenKind close = advance(p)->kind == TOK_LBRACKET ? TOK_RBRACKET : TOK_RPAREN;
        p->brackets++;
        do {
            Dim dim = {NULL, NULL};
            if (check(p, close) || check(p, TOK_COMMA)) {
                if (dims->count > 0) {
                    syntax_error(p, "a size: only the first dimension can be left out");
                    break;
                }
            } else {
                dim.hi = parse_expr(p);
                if (dim.hi && (match(p, TOK_DOTDOT) || (p->config->dialect == DIALECT_CAMBRIDGE && match(p, TOK_COLON)))) {
                    dim.lo = dim.hi;
                    dim.hi = parse_expr(p);
                }
                if (!dim.hi) break;
            }
            dim_add(p, dims, dim);
        } while (match(p, TOK_COMMA));
        p->brackets--;
        if (p->panic || !expect(p, close, close == TOK_RBRACKET ? "']'" : "')'"))
            return false;
    }
    return true;
}

static int parse_stars(Parser *p) {
    int count = 0;
    while (match(p, TOK_STAR)) count++;
    return count;
}

static Record *parse_record(Parser *p, const char *name) { /* after 'record' */
    if (!enter(p)) return NULL;
    Record *record = ARENA_NEW(p->arena, Record);
    record->loc = cur(p)->loc;
    record->name = name;
    VarDecl **tail = &record->fields;
    while (!check(p, TOK_END) && !check(p, TOK_EOF) && !p->panic) {
        match(p, TOK_VAR);
        tail = parse_var_group(p, tail);
    }
    leave(p);
    if (p->panic || !expect(p, TOK_END, "'end' after the record fields")) return NULL;
    return record;
}

static bool starts_type(const Parser *p) {
    switch (cur(p)->kind) {
        case TOK_INTEGER: case TOK_REAL: case TOK_LOGICAL: case TOK_CHARACTER:
        case TOK_ARRAY: case TOK_DIM: case TOK_RECORD: case TOK_IDENT:
            return true;
        case TOK_STAR:
            return true;
        default:
            return false;
    }
}

static TypeSpec *parse_type(Parser *p) {
    TypeSpec *spec = ARENA_NEW(p->arena, TypeSpec);
    spec->loc = cur(p)->loc;

    while (check(p, TOK_DIM) || check(p, TOK_ARRAY)) {
        int rank = 0;
        if (check(p, TOK_DIM)) {
            const Token *token = advance(p);
            for (const char *c = token->text; *c >= '0' && *c <= '9' && rank < 1000; c++)
                rank = rank * 10 + (*c - '0');
            if (rank < 1 || rank > 16) {
                diag_error(p->diag, token->loc, "an array has between 1 and 16 dimensions");
                rank = 1;
            }
            if (!check(p, TOK_ARRAY)) {
                syntax_error(p, "'array'");
                return NULL;
            }
        }
        advance(p);
        int before = spec->dims.count;
        if (!parse_dims(p, &spec->dims)) return NULL;
        int written = spec->dims.count - before;
        if (rank && written && written != rank) {
            diag_error(p->diag, spec->loc, "a %dd array was given %d dimensions", rank, written);
        }
        spec->is_array = true;
        spec->rank += rank ? rank : written ? written : 1;
        if (!expect(p, TOK_OF, "'of'")) return NULL;
        /* "array of array[n] of t": the unsized level still takes its place */
        if (!written && (before > 0 || check(p, TOK_ARRAY) || check(p, TOK_DIM))) {
            for (int i = 0; i < (rank ? rank : 1); i++)
                dim_add(p, &spec->dims, (Dim){NULL, NULL});
        }
    }

    spec->pointers = parse_stars(p);
    switch (cur(p)->kind) {
        case TOK_INTEGER: case TOK_REAL: case TOK_LOGICAL: case TOK_CHARACTER: case TOK_STRINGTYPE:
            spec->base = advance(p)->kind;
            break;
        case TOK_IDENT:
            spec->base = TOK_IDENT;
            spec->name = advance(p)->text;
            break;
        case TOK_RECORD:
            spec->base = TOK_RECORD;
            advance(p);
            spec->record = parse_record(p, NULL);
            if (!spec->record) return NULL;
            break;
        default:
            syntax_error(p, "a type");
            return NULL;
    }
    /* "integer**", unless the star opens the next statement */
    while (check(p, TOK_STAR) && !cur(p)->starts_line) {
        advance(p);
        spec->pointers++;
    }
    return spec;
}

/* "a, *b, c[n] : type [;]", appended at tail. */
static VarDecl **parse_var_group(Parser *p, VarDecl **tail) {
    VarDecl *first = NULL;
    VarDecl **end = &first;
    do {
        VarDecl *decl = ARENA_NEW(p->arena, VarDecl);
        decl->pointers = parse_stars(p);
        decl->loc = cur(p)->loc;
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a variable name");
            return tail;
        }
        decl->name = advance(p)->text;
        decl->has_dims = is_open_bracket(p);
        if (!parse_dims(p, &decl->dims)) return tail;
        *end = decl;
        end = &decl->next;
    } while (match(p, TOK_COMMA));

    if (!expect(p, TOK_COLON, "':' and a type")) return tail;
    TypeSpec *spec = parse_type(p);
    if (!spec) return tail;
    match(p, TOK_SEMICOLON);
    for (VarDecl *decl = first; decl; decl = decl->next) decl->spec = spec;
    *tail = first;
    return end;
}

/* "var a: t", "var: a: t", then more groups: after ';' a new one needs no
 * 'var'. With bare_names the first 'var' is optional too. */
static VarDecl **parse_var_section(Parser *p, VarDecl **tail, bool bare_names) {
    if (match(p, TOK_VAR)) {
        match(p, TOK_COLON);
        tail = parse_var_group(p, tail);
    }
    while (bare_names && !p->panic && (check(p, TOK_IDENT) || check(p, TOK_STAR)))
        tail = parse_var_group(p, tail);
    return tail;
}

static void parse_type_decl(Parser *p) { /* at 'type' */
    advance(p);
    match(p, TOK_COLON);
    TypeDecl *decl = ARENA_NEW(p->arena, TypeDecl);
    decl->loc = cur(p)->loc;
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, "a type name");
        return;
    }
    decl->name = advance(p)->text;
    if (!match(p, TOK_EQ) && !match(p, TOK_ASSIGN) && !expect(p, TOK_COLON, "':'")) return;
    if (!expect(p, TOK_RECORD, "'record'")) return;
    decl->record = parse_record(p, decl->name);
    if (!decl->record) return;
    match(p, TOK_SEMICOLON);
    *p->types_tail = decl;
    p->types_tail = &decl->next;
}

static bool parse_mode(Parser *p, ParamMode *mode, bool *group) {
    if (match(p, TOK_IN)) *mode = match(p, TOK_OUT) ? MODE_INOUT : MODE_IN;
    else if (match(p, TOK_OUT)) *mode = MODE_OUT;
    else if (match(p, TOK_INOUT)) *mode = MODE_INOUT;
    else return false;
    *group = match(p, TOK_COLON);
    return true;
}

/* Parameters come in many shapes:
 *     (in a: integer, out b: real)
 *     (in: a, b; in/out: c)         types declared in the body
 *     (in a, b: integer)            a takes the type of b
 *     (a, n, out: found)            no mode means 'in'
 *     (in A[n][n] : array of real)  bounds next to the name
 * "mode:" holds until the next mode or ';'. Without the colon it holds for one
 * parameter, and for those that share its type. */
static void parse_params(Parser *p, Function *function) {
    VarDecl **tail = &function->params;
    while (*tail) tail = &(*tail)->next; /* self */
    VarDecl *untyped = NULL; /* run of plain names waiting for a shared type */
    ParamMode mode = MODE_IN;
    bool group = false;

    if (match(p, TOK_RPAREN)) return;
    for (;;) {
        bool explicit_mode = parse_mode(p, &mode, &group);
        if (explicit_mode) untyped = NULL;
        else if (!group) mode = MODE_IN;

        VarDecl *param = ARENA_NEW(p->arena, VarDecl);
        param->mode = mode;
        param->pointers = parse_stars(p);
        param->loc = cur(p)->loc;
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a parameter name");
            return;
        }
        param->name = advance(p)->text;
        param->has_dims = is_open_bracket(p);
        if (!parse_dims(p, &param->dims)) return;

        bool colon = match(p, TOK_COLON);
        if (starts_type(p)) {
            param->spec = parse_type(p);
            if (!param->spec) return;
        } else if (colon && !check(p, TOK_COMMA) && !check(p, TOK_SEMICOLON) &&
                   !check(p, TOK_RPAREN)) {
            syntax_error(p, "a type");
            return;
        }
        *tail = param;
        tail = &param->next;

        if (!param->spec) {
            if (param->has_dims || param->pointers) untyped = NULL;
            else if (!untyped) untyped = param;
        } else {
            if (!param->has_dims && !param->pointers && !param->spec->is_array) {
                for (VarDecl *d = untyped; d && d != param; d = d->next) {
                    d->spec = param->spec;
                    d->spec_inherited = true;
                    d->mode = param->mode = untyped->mode;
                }
            }
            untyped = NULL;
        }

        if (match(p, TOK_SEMICOLON)) {
            untyped = NULL;
            group = false;
        } else if (!match(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "')' after the parameters");
}

/* A body is closed by any of "end endfunction", "end end name", "end name",
 * "endfunction" and the same for procedures. */
static void parse_function_end(Parser *p, Function *function) {
    const char *closer = function->is_procedure ? "endprocedure" : "endfunction";
    TokenKind kind = function->is_procedure ? TOK_ENDPROCEDURE : TOK_ENDFUNCTION;
    const char *expected = arena_printf(p->arena, diag_text(p->diag, "'%s' or 'end %s'"), closer, function->name);

    bool ended = match(p, TOK_END);
    if (match(p, kind) || (ended && match(p, TOK_END)) || ended) {
        if (check(p, TOK_IDENT) && equals_ignore_case(cur(p)->text, function->name)) {
            advance(p);
        } else if (check(p, TOK_IDENT) && peek(p, 1)->kind != TOK_FUNCTION) {
            /* not the name, nor the type of "point function next(...)" */
            syntax_error(p, expected);
        } else if (p->tokens[p->pos - 1].kind == TOK_END &&
                   p->pos >= 2 && p->tokens[p->pos - 2].kind != TOK_END) {
            syntax_error(p, expected); /* a single bare 'end' */
        }
    } else {
        syntax_error(p, expected);
    }
    match(p, TOK_SEMICOLON);
}

static void parse_when(Parser *p, Function *function) {
    if (!is_word(cur(p), "when")) return;
    advance(p);
    StateName **tail = &function->guards;
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "the name of a state");
            return;
        }
        StateName *state = ARENA_NEW(p->arena, StateName);
        state->loc = cur(p)->loc;
        state->name = advance(p)->text;
        *tail = state;
        tail = &state->next;
    } while (match(p, TOK_COMMA) || match(p, TOK_OR));
}

static Function *parse_function(Parser *p, bool is_procedure, TypeSpec *result) {
    Function *function = ARENA_NEW(p->arena, Function);
    function->is_procedure = is_procedure;
    function->result = result;
    function->loc = cur(p)->loc;
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, is_procedure ? "a procedure name" : "a function name");
        return NULL;
    }
    function->name = function->full_name = advance(p)->text;
    function->owner = p->klass;
    if (p->klass) {
        function->full_name = arena_printf(p->arena, "%s.%s", p->klass->name, function->name);
        VarDecl *self = ARENA_NEW(p->arena, VarDecl);
        self->loc = function->loc;
        self->name = "self";
        self->mode = MODE_INOUT;
        self->spec = ARENA_NEW(p->arena, TypeSpec);
        self->spec->loc = function->loc;
        self->spec->base = TOK_IDENT;
        self->spec->name = p->klass->name;
        function->params = self;
    }
    *p->functions_tail = function;
    p->functions_tail = &function->next;

    if (match(p, TOK_LPAREN)) {
        p->brackets++;
        parse_params(p, function);
        p->brackets--;
        if (p->panic) return function;
    }
    parse_when(p, function);
    if (check(p, TOK_COLON) && !is_procedure && !result) {
        advance(p);
        function->result = parse_type(p);
        if (!function->result) return function;
    }
    match(p, TOK_SEMICOLON);
    if (p->signature_only) {
        function->is_abstract = true;
        function->last_line = p->tokens[p->pos > 0 ? p->pos - 1 : 0].loc.line;
        return function;
    }

    VarDecl **tail = &function->locals;
    while (!p->panic) {
        if (check(p, TOK_VAR) || check(p, TOK_IDENT) || check(p, TOK_STAR))
            tail = parse_var_section(p, tail, true);
        else if (check(p, TOK_TYPE))
            parse_type_decl(p);
        else
            break;
    }
    if (p->panic || !expect(p, TOK_BEGIN, "'begin'")) return function;
    function->body = parse_statements(p);
    if (!p->panic) parse_function_end(p, function);
    function->last_line = p->tokens[p->pos > 0 ? p->pos - 1 : 0].loc.line;
    return function;
}

/* ---- classes, components, tests ---- */

static void parse_states(Parser *p, Class *klass) {
    StateName **tail = &klass->states;
    while (*tail) tail = &(*tail)->next;
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "the name of a state");
            return;
        }
        StateName *state = ARENA_NEW(p->arena, StateName);
        state->loc = cur(p)->loc;
        state->name = advance(p)->text;
        *tail = state;
        tail = &state->next;
    } while (match(p, TOK_COMMA));
    match(p, TOK_SEMICOLON);
}

/* class Name / component Name ... end Name */
static void parse_class(Parser *p) {
    Class *klass = ARENA_NEW(p->arena, Class);
    if (is_word(cur(p), "abstract")) {
        advance(p);
        klass->is_abstract = true;
    }
    klass->is_interface = is_word(cur(p), "interface");
    if (klass->is_interface) klass->is_abstract = true;
    klass->is_component = is_word(advance(p), "component");
    klass->loc = cur(p)->loc;
    klass->name = advance(p)->text;
    BaseName **bases = &klass->bases;
    while (!p->panic && (is_word(cur(p), "extends") || is_word(cur(p), "implements") || is_word(cur(p), "inherits")) &&
           peek(p, 1)->kind == TOK_IDENT) {
        advance(p);
        do {
            if (!check(p, TOK_IDENT)) {
                syntax_error(p, "the name of a class");
                break;
            }
            BaseName *base = ARENA_NEW(p->arena, BaseName);
            base->loc = cur(p)->loc;
            base->name = advance(p)->text;
            *bases = base;
            bases = &base->next;
        } while (match(p, TOK_COMMA));
    }
    klass->record = ARENA_NEW(p->arena, Record);
    klass->record->loc = klass->loc;
    klass->record->name = klass->name;
    klass->record->klass = klass;
    *p->classes_tail = klass;
    p->classes_tail = &klass->next;
    TypeDecl *decl = ARENA_NEW(p->arena, TypeDecl);
    decl->loc = klass->loc;
    decl->name = klass->name;
    decl->record = klass->record;
    *p->types_tail = decl;
    p->types_tail = &decl->next;

    p->klass = klass;
    VarDecl **fields = &klass->record->fields;
    while (!p->panic && !check(p, TOK_END) && !check(p, TOK_EOF)) {
        if (match(p, TOK_SEMICOLON)) continue;
        int before = p->pos;
        p->signature_only = klass->is_interface;
        if (is_word(cur(p), "abstract") && peek(p, 1)->kind != TOK_COLON && peek(p, 1)->kind != TOK_COMMA &&
            peek(p, 1)->kind != TOK_ASSIGN) {
            advance(p);
            p->signature_only = true;
        }
        if (klass->is_component && is_word(cur(p), "states") && peek(p, 1)->kind == TOK_IDENT) {
            advance(p);
            parse_states(p, klass);
        } else if (klass->is_component && is_word(cur(p), "on") && peek(p, 1)->kind == TOK_IDENT) {
            advance(p);
            Function *handler = parse_function(p, true, NULL);
            if (handler) handler->is_handler = true;
        } else if (check(p, TOK_VAR) || (check(p, TOK_IDENT) && peek(p, 1)->kind != TOK_FUNCTION) ||
                   check(p, TOK_STAR)) {
            fields = parse_var_section(p, fields, false);
            if (!p->panic && check(p, TOK_IDENT) && !is_word(cur(p), "on") && !is_word(cur(p), "states") &&
                peek(p, 1)->kind != TOK_FUNCTION)
                fields = parse_var_group(p, fields);
        } else if (check(p, TOK_FUNCTION) || check(p, TOK_PROCEDURE)) {
            bool is_procedure = advance(p)->kind == TOK_PROCEDURE;
            parse_function(p, is_procedure, NULL);
        } else if (starts_type(p) && !check(p, TOK_RECORD)) {
            TypeSpec *result = parse_type(p);
            if (result && expect(p, TOK_FUNCTION, "'function'")) parse_function(p, false, result);
        } else {
            syntax_error(p, "a variable, a procedure or a function of the class");
        }
        if (!p->panic && p->pos == before) /* nothing here that we can read */
            syntax_error(p, "a variable, a procedure or a function of the class");
        p->signature_only = false;
    }
    p->signature_only = false;
    p->klass = NULL;
    if (p->panic || !expect(p, TOK_END, "'end'")) return;
    if (check(p, TOK_IDENT)) {
        if (equals_ignore_case(cur(p)->text, klass->name)) advance(p);
        else syntax_error(p, arena_printf(p->arena, diag_text(p->diag, "'end %s'"), klass->name));
    }
    klass->last_line = p->tokens[p->pos - 1].loc.line;
    match(p, TOK_SEMICOLON);
}

/* test "what it shows" [var ...] begin ... end test */
static void parse_test(Parser *p) {
    Function *test = ARENA_NEW(p->arena, Function);
    test->loc = cur(p)->loc;
    advance(p);
    test->is_procedure = test->is_test = true;
    test->name = test->full_name = advance(p)->text;
    *p->functions_tail = test;
    p->functions_tail = &test->next;
    VarDecl **tail = &test->locals;
    while (!p->panic && (check(p, TOK_VAR) || (check(p, TOK_IDENT) && tail != &test->locals))) {
        int before = p->pos;
        tail = parse_var_section(p, tail, true);
        if (p->pos == before) break;
    }
    if (p->panic || !expect(p, TOK_BEGIN, "'begin'")) return;
    p->in_test = true;
    test->body = parse_statements(p);
    p->in_test = false;
    if (p->panic || !expect(p, TOK_END, "'end test'")) return;
    test->last_line = p->tokens[p->pos - 1].loc.line;
    if (is_word(cur(p), "test") && peek(p, 1)->kind != TOK_STRING) advance(p);
    match(p, TOK_SEMICOLON);
}

static void parse_link(Parser *p) {
    Link *link = ARENA_NEW(p->arena, Link);
    link->loc = cur(p)->loc;
    advance(p);
    link->from = parse_postfix(p);
    if (!link->from) return;
    if (match(p, TOK_LT)) { /* "<->" */
        link->both_ways = true;
        if (!expect(p, TOK_ARROW, "'->'")) return;
    } else if (!expect(p, TOK_ARROW, "'->'")) {
        return;
    }
    link->to = parse_postfix(p);
    while (link->to && !p->panic && check(p, TOK_IDENT) && !cur(p)->starts_line) {
        bool delay = is_word(cur(p), "delay");
        if (!delay && !is_word(cur(p), "loss")) {
            syntax_error(p, "'delay' or 'loss'");
            return;
        }
        advance(p);
        Expr *value = parse_expr(p);
        if (!value) return;
        if (delay) {
            link->delay = value;
            if (match(p, TOK_DOTDOT)) link->delay_max = parse_expr(p);
        } else {
            link->loss = value;
            match(p, TOK_MOD); /* "loss 20%" */
        }
    }
    match(p, TOK_SEMICOLON);
    *p->links_tail = link;
    p->links_tail = &link->next;
}

/* ---- statements ---- */

static bool ends_statements(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: case TOK_END: case TOK_ENDFUNCTION: case TOK_ENDPROCEDURE:
        case TOK_ELSE: case TOK_ELSEIF: case TOK_ENDIF: case TOK_ENDWHILE:
        case TOK_ENDFOR: case TOK_UNTIL: case TOK_FUNCTION: case TOK_PROCEDURE:
        case TOK_ENDPROGRAM: case TOK_OTHERWISE: case TOK_ENDSWITCH: case TOK_NEXT: case TOK_UNTIL_WHILE:
        case TOK_RBRACE: case TOK_CASE: case TOK_ENDTYPE: case TOK_PROGRAM:
            return true;
        default:
            return false;
    }
}

static bool starts_statement(TokenKind kind) {
    switch (kind) {
        case TOK_IF: case TOK_WHILE: case TOK_FOR: case TOK_REPEAT:
        case TOK_RETURN: case TOK_PRINT: case TOK_READ: case TOK_BEGIN: case TOK_ELLIPSIS:
        case TOK_VAR: case TOK_IDENT: case TOK_STAR: case TOK_AT: case TOK_LPAREN:
            return true;
        default:
            return false;
    }
}

/* After an error: skip to something that looks like a statement boundary. */
static void recover(Parser *p) {
    p->panic = false;
    if (ends_statements(cur(p)->kind)) return;
    advance(p);
    while (!ends_statements(cur(p)->kind)) {
        if (match(p, TOK_SEMICOLON)) return;
        if (cur(p)->starts_line && starts_statement(cur(p)->kind)) return;
        advance(p);
    }
}

static Stmt *new_stmt(Parser *p, StmtKind kind) {
    Stmt *stmt = ARENA_NEW(p->arena, Stmt);
    stmt->kind = kind;
    stmt->loc = cur(p)->loc;
    return stmt;
}

static Stmt *parse_if(Parser *p) { /* at 'if'; "elseif" chains stay flat */
    Stmt *first = NULL;
    Stmt **link = &first;
    do {
        Stmt *branch = new_stmt(p, ST_IF);
        advance(p);
        branch->value = parse_expr(p);
        if (!branch->value) return NULL;
        if (!match(p, TOK_THEN) && !foreign(p) && !expect(p, TOK_THEN, "'then'")) return NULL;
        branch->body = parse_statements(p);
        *link = branch;
        link = &branch->otherwise;
    } while (check(p, TOK_ELSEIF));

    if (check(p, TOK_ELSE)) {
        Stmt *block = new_stmt(p, ST_BLOCK);
        advance(p);
        block->body = parse_statements(p);
        *link = block;
    }
    return expect(p, TOK_ENDIF, "'endif'") ? first : NULL;
}

static Stmt *parse_for(Parser *p) {
    Stmt *stmt = new_stmt(p, ST_FOR);
    advance(p);
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, "a loop variable");
        return NULL;
    }
    stmt->target = new_leaf(p, EX_NAME);
    if (!match(p, TOK_ASSIGN) && !match(p, TOK_EQ) && !(foreign(p) && match(p, TOK_OF))) {
        syntax_error(p, "':='");
        return NULL;
    }
    stmt->value = parse_expr(p);
    if (!stmt->value || !expect(p, TOK_TO, "'to'")) return NULL;
    stmt->limit = parse_expr(p);
    if (!stmt->limit) return NULL;
    if (match(p, TOK_STEP)) {
        stmt->step = parse_expr(p);
        if (!stmt->step) return NULL;
    }
    if (!match(p, TOK_DO) && !foreign(p) && !expect(p, TOK_DO, "'do'")) return NULL;
    stmt->body = parse_statements(p);
    if (match(p, TOK_NEXT)) { /* NEXT i */
        if (check(p, TOK_IDENT) && !cur(p)->starts_line) advance(p);
        return stmt;
    }
    return expect(p, TOK_ENDFOR, "'endfor'") ? stmt : NULL;
}

/* print and read: the parentheses around the arguments are optional. */
static Stmt *parse_io(Parser *p, StmtKind kind) {
    Stmt *stmt = new_stmt(p, kind);
    if (foreign(p)) stmt->text = cur(p)->text;
    advance(p);
    int start = p->pos;
    if (match(p, TOK_LPAREN)) {
        /* "print (a+b)*c" opens with a parenthesis that is not the list's. */
        int depth = 1, i = 0;
        for (; depth > 0 && peek(p, i)->kind != TOK_EOF; i++) {
            if (peek(p, i)->kind == TOK_LPAREN) depth++;
            if (peek(p, i)->kind == TOK_RPAREN) depth--;
        }
        const Token *after = peek(p, i);
        bool continues = (precedence(after->kind) || after->kind == TOK_COMMA) &&
                         !after->starts_line;
        if (!continues) {
            if (!parse_args(p, &stmt->args, TOK_RPAREN, "')'")) return NULL;
            if (stmt->args.count == 0 && kind == ST_READ) {
                diag_error(p->diag, stmt->loc, "read needs a variable");
            }
            return stmt;
        }
        p->pos = start;
    }
    do {
        if (foreign(p) && cur(p)->starts_line) break; /* just a new line */
        Expr *arg = parse_expr(p);
        if (!arg) return NULL;
        list_add(p, &stmt->args, arg);
    } while (match(p, TOK_COMMA));
    return stmt;
}

static Stmt *parse_return(Parser *p) {
    Stmt *stmt = new_stmt(p, ST_RETURN);
    advance(p);
    switch (cur(p)->kind) {
        case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR: case TOK_TRUE:
        case TOK_FALSE: case TOK_IDENT: case TOK_LPAREN: case TOK_MINUS: case TOK_PLUS:
        case TOK_NOT: case TOK_TILDE: case TOK_STAR: case TOK_AMP: case TOK_AT:
            if (cur(p)->starts_line) break;
            stmt->value = parse_expr(p);
            if (!stmt->value) return NULL;
            break;
        default:
            break;
    }
    /* the other pseudocodes keep the result in a variable of their own */
    if (!stmt->value && p->function && p->function->result_name) return return_of(p, p->function->result_name, stmt->loc);
    return stmt;
}

/* An assignment or a call. */
static Stmt *parse_simple(Parser *p) {
    Stmt *stmt = new_stmt(p, ST_CALL);
    Expr *expr = parse_unary(p);
    if (!expr) return NULL;
    if (match(p, TOK_ASSIGN) || (foreign(p) && match(p, TOK_EQ))) {
        stmt->kind = ST_ASSIGN;
        stmt->target = expr;
        stmt->value = parse_expr(p);
        return stmt->value ? stmt : NULL;
    }
    if (check(p, TOK_EQ)) {
        if (!p->panic)
            diag_error(p->diag, cur(p)->loc, "'%s' compares; to assign use ':='%s",
                       cur(p)->text,
                       strcmp(cur(p)->text, "=") == 0 ? " or --assignment=equals" : "");
        p->panic = true;
        return NULL;
    }
    if (expr->kind != EX_APPLY && expr->kind != EX_NAME && expr->kind != EX_FIELD) {
        syntax_error(p, "an assignment");
        return NULL;
    }
    stmt->value = expr;
    return stmt;
}

/* send Message(args) to target [after time] */
static Stmt *parse_send(Parser *p) {
    Stmt *stmt = new_stmt(p, ST_SEND);
    advance(p);
    stmt->value = new_leaf(p, EX_NAME);
    stmt->name = stmt->value->text;
    if (check(p, TOK_LPAREN)) {
        Expr *apply = new_expr(p, EX_APPLY, cur(p)->loc, stmt->value, NULL);
        advance(p);
        if (!parse_args(p, &apply->args, TOK_RPAREN, "')'")) return NULL;
        stmt->value = apply;
    }
    if (!expect(p, TOK_TO, "'to'")) return NULL;
    if (is_word(cur(p), "all") && peek(p, 1)->kind == TOK_IDENT && !peek(p, 1)->starts_line) {
        advance(p);
        stmt->to_all_name = advance(p)->text;
    } else {
        stmt->target = parse_unary(p);
        if (!stmt->target) return NULL;
    }
    if (is_word(cur(p), "after") && !cur(p)->starts_line) {
        advance(p);
        stmt->limit = parse_expr(p);
        if (!stmt->limit) return NULL;
    }
    return stmt;
}

static bool same_line(const Parser *p, int offset) { return !peek(p, offset)->starts_line; }

/* Statements that begin with a contextual word. Sets *handled. */
static Stmt *parse_word_statement(Parser *p, bool *handled) {
    const Token *word = cur(p);
    Stmt *stmt = NULL;
    *handled = true;
    if (is_word(word, "send") && peek(p, 1)->kind == TOK_IDENT && same_line(p, 1)) return parse_send(p);
    if (is_word(word, "become") && peek(p, 1)->kind == TOK_IDENT && same_line(p, 1)) {
        stmt = new_stmt(p, ST_ASSIGN); /* state := ... */
        stmt->target = new_leaf(p, EX_NAME);
        stmt->target->text = "state";
        stmt->value = new_leaf(p, EX_NAME);
        stmt->name = "become";
        return stmt;
    }
    if (is_word(word, "cancel") && peek(p, 1)->kind == TOK_IDENT && same_line(p, 1)) {
        stmt = new_stmt(p, ST_CANCEL);
        advance(p);
        stmt->name = advance(p)->text;
        return stmt;
    }
    if (p->in_test) {
        TokenKind next = peek(p, 1)->kind;
        bool plain = next == TOK_ASSIGN || next == TOK_LBRACKET || next == TOK_DOT;
        if (is_word(word, "check") && !plain) {
            stmt = new_stmt(p, ST_CHECK);
            advance(p);
            stmt->value = parse_expr(p);
            if (!stmt->value) return NULL;
            if (match(p, TOK_COMMA)) {
                if (!check(p, TOK_STRING)) {
                    syntax_error(p, "a text between quotes");
                    return NULL;
                }
                stmt->text = advance(p)->text;
            }
            return stmt;
        }
        if (is_word(word, "run") && !plain) {
            stmt = new_stmt(p, ST_RUN);
            advance(p);
            if (!cur(p)->starts_line && (check(p, TOK_INT) || check(p, TOK_IDENT) || check(p, TOK_LPAREN))) {
                stmt->value = parse_expr(p);
                if (!stmt->value) return NULL;
            }
            return stmt;
        }
        if (is_word(word, "step") || (word->kind == TOK_STEP && next == TOK_STRING)) {
            stmt = new_stmt(p, ST_STEP);
            advance(p);
            if (!check(p, TOK_STRING)) {
                syntax_error(p, "a text between quotes");
                return NULL;
            }
            stmt->text = advance(p)->text;
            return stmt;
        }
        if (is_word(word, "expect") && next == TOK_IDENT && same_line(p, 1)) {
            stmt = new_stmt(p, ST_EXPECT);
            advance(p);
            stmt->target = new_leaf(p, EX_NAME);
            while (match(p, TOK_LBRACKET)) {
                Expr *index = new_expr(p, EX_INDEX, cur(p)->loc, stmt->target, NULL);
                if (!parse_args(p, &index->args, TOK_RBRACKET, "']'")) return NULL;
                stmt->target = index;
            }
            if (!expect(p, TOK_ARROW, "'->'")) return NULL;
            stmt->limit = parse_postfix(p);
            if (!stmt->limit || !expect(p, TOK_COLON, "':'")) return NULL;
            if (!check(p, TOK_IDENT)) {
                syntax_error(p, "the name of a message");
                return NULL;
            }
            stmt->name = advance(p)->text;
            return stmt;
        }
    }
    *handled = false;
    return NULL;
}

static Stmt *parse_statement(Parser *p) {
    Stmt *stmt;
    bool handled = false;
    if (foreign(p)) {
        stmt = foreign_statement(p, &handled);
        if (handled) return stmt;
    } else if (check(p, TOK_IDENT) || (p->in_test && check(p, TOK_STEP))) {
        stmt = parse_word_statement(p, &handled);
        if (handled) return stmt;
    }
    switch (cur(p)->kind) {
        case TOK_ELLIPSIS:
            stmt = new_stmt(p, ST_TODO);
            advance(p);
            return stmt;
        case TOK_IF: return parse_if(p);
        case TOK_FOR: return parse_for(p);
        case TOK_RETURN: return parse_return(p);
        case TOK_PRINT: return parse_io(p, ST_PRINT);
        case TOK_READ: return parse_io(p, ST_READ);
        case TOK_WHILE:
            stmt = new_stmt(p, ST_WHILE);
            advance(p);
            stmt->value = parse_expr(p);
            if (!stmt->value) return NULL;
            if (!match(p, TOK_DO) && !foreign(p) && !expect(p, TOK_DO, "'do'")) return NULL;
            stmt->body = parse_statements(p);
            return expect(p, TOK_ENDWHILE, "'endwhile'") ? stmt : NULL;
        case TOK_REPEAT:
            stmt = new_stmt(p, ST_REPEAT);
            advance(p);
            stmt->body = parse_statements(p);
            if (match(p, TOK_UNTIL_WHILE)) { /* Mientras Que: goes on while it is true */
                Expr *test = parse_expr(p);
                if (!test) return NULL;
                stmt->value = new_expr(p, EX_UNARY, test->loc, test, NULL);
                stmt->value->op = TOK_NOT;
                return stmt;
            }
            if (!(foreign(p) && match(p, TOK_TO)) && !expect(p, TOK_UNTIL, "'until'")) return NULL;
            stmt->value = parse_expr(p);
            return stmt->value ? stmt : NULL;
        case TOK_BEGIN:
            stmt = new_stmt(p, ST_BLOCK);
            advance(p);
            stmt->body = parse_statements(p);
            return expect(p, TOK_END, "'end'") ? stmt : NULL;
        case TOK_VAR:
            stmt = new_stmt(p, ST_VAR);
            parse_var_section(p, &stmt->vars, false);
            return p->panic ? NULL : stmt;
        case TOK_IDENT: case TOK_STAR: case TOK_AT: case TOK_LPAREN:
            return parse_simple(p);
        default:
            syntax_error(p, "a statement");
            return NULL;
    }
}

static Stmt *parse_statements(Parser *p) {
    Stmt *first = NULL;
    Stmt **tail = &first;
    if (!enter(p)) return NULL;
    int brackets = p->brackets;
    p->brackets = 0;
    for (;;) {
        while (match(p, TOK_SEMICOLON)) {}
        if (ends_statements(cur(p)->kind) || p->diag->too_many) break;
        if (p->switches > 0 && is_case_label(p)) break;
        if (foreign(p) && (check(p, TOK_TYPE) || check(p, TOK_TO))) break;
        Stmt *stmt = parse_statement(p);
        if (stmt) {
            *tail = stmt;
            while (stmt->next) stmt = stmt->next; /* some dialects give more than one */
            tail = &stmt->next;
        } else {
            p->brackets = 0;
            recover(p);
        }
    }
    p->brackets = brackets;
    leave(p);
    return first;
}

#include "foreign.inc"

/* ---- program ---- */

static void parse_declaration(Parser *p) {
    switch (cur(p)->kind) {
        case TOK_FUNCTION: case TOK_PROCEDURE: {
            bool is_procedure = advance(p)->kind == TOK_PROCEDURE;
            parse_function(p, is_procedure, NULL);
            return;
        }
        case TOK_VAR:
            p->globals_tail = parse_var_section(p, p->globals_tail, false);
            while (!p->panic && check(p, TOK_IDENT) && p->tokens[p->pos - 1].kind == TOK_SEMICOLON &&
                   (peek(p, 1)->kind == TOK_COLON || peek(p, 1)->kind == TOK_COMMA))
                p->globals_tail = parse_var_group(p, p->globals_tail);
            return;
        case TOK_TYPE:
            parse_type_decl(p);
            return;
        default:
            if (check(p, TOK_IDENT)) {
                TokenKind next = peek(p, 1)->kind;
                bool abstract = is_word(cur(p), "abstract") && is_word(peek(p, 1), "class") && peek(p, 2)->kind == TOK_IDENT;
                if (((is_word(cur(p), "class") || is_word(cur(p), "component") || is_word(cur(p), "interface")) && next == TOK_IDENT) ||
                    abstract) {
                    parse_class(p);
                    return;
                }
                if (is_word(cur(p), "test") && next == TOK_STRING) {
                    parse_test(p);
                    return;
                }
                if (is_word(cur(p), "link") && next == TOK_IDENT) {
                    parse_link(p);
                    return;
                }
                if (is_word(cur(p), "seed") && next == TOK_INT) {
                    advance(p);
                    p->program->seed = parse_expr(p);
                    return;
                }
            }
            if (starts_type(p) && !check(p, TOK_RECORD)) { /* "real function f(...)" */
                TypeSpec *result = parse_type(p);
                if (result && expect(p, TOK_FUNCTION, "'function'"))
                    parse_function(p, false, result);
                return;
            }
            syntax_error(p, "a declaration (function, procedure, var or type)");
    }
}

Program *parse(TokenList tokens, const Config *config, Arena *arena, Diag *diag) {
    Program *program = ARENA_NEW(arena, Program);
    Parser parser = {.tokens = tokens.items, .count = tokens.count, .config = config,
                     .arena = arena, .diag = diag, .program = program,
                     .types_tail = &program->types, .globals_tail = &program->globals,
                     .functions_tail = &program->functions,
                     .classes_tail = &program->classes, .links_tail = &program->links};
    Parser *p = &parser;
    if (foreign(p)) {
        foreign_program(p);
        if (!diag->errors) infer_types(p);
        return program;
    }
    for (;;) {
        while (match(p, TOK_SEMICOLON)) {}
        if (check(p, TOK_EOF) || diag->too_many) break;
        int before = p->pos;
        parse_declaration(p);
        if (p->panic) {
            p->panic = false;
            p->depth = p->brackets = 0;
            p->klass = NULL;
            p->in_test = false;
            if (p->pos == before) advance(p);
            while (!check(p, TOK_EOF) && !check(p, TOK_FUNCTION) && !check(p, TOK_PROCEDURE))
                advance(p);
        }
    }
    return program;
}
