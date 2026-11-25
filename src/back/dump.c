#include "pseudoscope/dump.h"

#define MAX_PATH 4096

void dump_tokens(TokenList tokens, StrBuf *out) {
    for (int i = 0; i < tokens.count; i++) {
        const Token *t = &tokens.items[i];
        strbuf_printf(out, "%d:%d\t%s", t->loc.line, t->loc.column, token_kind_name(t->kind));
        if (t->text[0]) strbuf_printf(out, "\t%s", t->text);
        strbuf_puts(out, "\n");
    }
}

/* Both formats share one walk: each node opens with a label and closes. */
typedef struct {
    StrBuf *out;
    bool dot;
    int next_id;
    int depth;
    int path[MAX_PATH];
} Dump;



static void open_node(Dump *d, const char *label, const char *detail, Loc loc) {
    if (!d->dot) {
        for (int i = 0; i < d->depth; i++) strbuf_puts(d->out, "  ");
        strbuf_printf(d->out, "%s%s%s", label, detail ? " " : "", detail ? detail : "");
        if (loc.line) strbuf_printf(d->out, " @%d:%d", loc.line, loc.column);
        strbuf_puts(d->out, "\n");
    } else {
        int id = d->next_id++;
        strbuf_printf(d->out, "  n%d [label=\"%s", id, label);
        if (detail) {
            strbuf_puts(d->out, "\\n");
            for (const char *c = detail; *c; c++) {
                if (*c == '"' || *c == '\\') strbuf_puts(d->out, "\\");
                strbuf_append(d->out, c, 1);
            }
        }
        strbuf_puts(d->out, "\"];\n");
        if (d->depth > 0 && d->depth <= MAX_PATH)
            strbuf_printf(d->out, "  n%d -> n%d;\n", d->path[d->depth - 1], id);
        if (d->depth < MAX_PATH) d->path[d->depth] = id;
    }
    d->depth++;
}

static void close_node(Dump *d) { d->depth--; }

static void walk_expr(Dump *d, const Expr *expr);
static void walk_vars(Dump *d, const VarDecl *vars, bool params);

static void walk_list(Dump *d, const ExprList *list) {
    for (int i = 0; i < list->count; i++) walk_expr(d, list->items[i]);
}

static void walk_expr(Dump *d, const Expr *expr) {
    if (!expr) return;
    static const char *const LABELS[] = {
        [EX_INT] = "integer", [EX_FLOAT] = "real", [EX_BOOL] = "logical",
        [EX_STRING] = "string", [EX_CHAR] = "character", [EX_NAME] = "name",
        [EX_APPLY] = "apply", [EX_INDEX] = "index", [EX_FIELD] = "field",
        [EX_UNARY] = "unary", [EX_BINARY] = "binary",
    };
    bool has_op = expr->kind == EX_UNARY || expr->kind == EX_BINARY;
    open_node(d, LABELS[expr->kind], has_op ? token_kind_name(expr->op) : expr->text, expr->loc);
    walk_expr(d, expr->base);
    walk_expr(d, expr->right);
    walk_list(d, &expr->args);
    close_node(d);
}

static void walk_dims(Dump *d, const DimList *dims) {
    for (int i = 0; i < dims->count; i++) {
        open_node(d, dims->items[i].lo ? "range" : "size", NULL, (Loc){0, 0});
        walk_expr(d, dims->items[i].lo);
        walk_expr(d, dims->items[i].hi);
        close_node(d);
    }
}

static void walk_type(Dump *d, const TypeSpec *spec) {
    if (!spec) return;
    const char *name = spec->base == TOK_INTEGER ? "integer"
                     : spec->base == TOK_REAL ? "real"
                     : spec->base == TOK_LOGICAL ? "logical"
                     : spec->base == TOK_CHARACTER ? "character"
                     : spec->base == TOK_RECORD ? "record" : spec->name;
    open_node(d, spec->is_array ? "array of" : "type", name, spec->loc);
    for (int i = 0; i < spec->pointers; i++) {
        open_node(d, "pointer", NULL, (Loc){0, 0});
        close_node(d);
    }
    walk_dims(d, &spec->dims);
    if (spec->record) walk_vars(d, spec->record->fields, false);
    close_node(d);
}

static void walk_vars(Dump *d, const VarDecl *vars, bool params) {
    static const char *const MODES[] = {"in", "out", "in/out"};
    for (; vars; vars = vars->next) {
        open_node(d, params ? MODES[vars->mode] : "var", vars->name, vars->loc);
        walk_dims(d, &vars->dims);
        walk_type(d, vars->spec);
        close_node(d);
    }
}

static void walk_stmts(Dump *d, const Stmt *stmt) {
    static const char *const LABELS[] = {
        [ST_ASSIGN] = "assign", [ST_CALL] = "call", [ST_IF] = "if", [ST_WHILE] = "while",
        [ST_FOR] = "for", [ST_REPEAT] = "repeat", [ST_RETURN] = "return",
        [ST_PRINT] = "print", [ST_READ] = "read", [ST_BLOCK] = "block", [ST_VAR] = "declare",
    };
    for (; stmt; stmt = stmt->next) {
        for (const Stmt *s = stmt; s; s = s->otherwise) {
            open_node(d, s == stmt ? LABELS[s->kind] : s->kind == ST_IF ? "elseif" : "else",
                      NULL, s->loc);
            walk_vars(d, s->vars, false);
            walk_expr(d, s->target);
            walk_expr(d, s->value);
            walk_expr(d, s->limit);
            walk_expr(d, s->step);
            walk_list(d, &s->args);
            if (s->body) {
                open_node(d, "body", NULL, (Loc){0, 0});
                walk_stmts(d, s->body);
                close_node(d);
            }
            close_node(d);
        }
    }
}

static void walk_program(Dump *d, const Program *program) {
    open_node(d, "program", NULL, (Loc){0, 0});
    for (const TypeDecl *t = program->types; t; t = t->next) {
        open_node(d, "record", t->name, t->loc);
        walk_vars(d, t->record->fields, false);
        close_node(d);
    }
    walk_vars(d, program->globals, false);
    for (const Function *f = program->functions; f; f = f->next) {
        open_node(d, f->is_procedure ? "procedure" : "function", f->name, f->loc);
        walk_vars(d, f->params, true);
        walk_type(d, f->result);
        walk_vars(d, f->locals, false);
        open_node(d, "body", NULL, (Loc){0, 0});
        walk_stmts(d, f->body);
        close_node(d);
        close_node(d);
    }
    close_node(d);
}

void dump_tree(const Program *program, StrBuf *out) {
    Dump dump = {.out = out};
    walk_program(&dump, program);
}

void dump_tree_dot(const Program *program, StrBuf *out) {
    Dump dump = {.out = out, .dot = true};
    strbuf_puts(out, "digraph program {\n  node [shape=box, fontname=\"monospace\"];\n");
    walk_program(&dump, program);
    strbuf_puts(out, "}\n");
}
