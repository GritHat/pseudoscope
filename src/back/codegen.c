#include "plike/codegen.h"

#include <string.h>

typedef struct {
    const Program *program;
    const Config *config;
    Arena *arena;
    StrBuf *out;
    const Symbol *function;
    int indent;
    bool uses_fail;
    bool uses_math;
    bool uses_abs;
    bool uses_random;
    bool uses_text;
    LineMap *map;
} Gen;

/* C operator precedence; higher binds tighter. */
enum {
    P_LOWEST, P_OR, P_AND, P_BITOR, P_BITXOR, P_BITAND, P_EQ, P_REL, P_SHIFT,
    P_ADD, P_MUL, P_UNARY, P_POSTFIX
};

static void gen_expr(Gen *g, const Expr *expr, int min);
static void gen_statements(Gen *g, const Stmt *stmt);
static const char *c_operator(TokenKind op);

static void put(Gen *g, const char *text) { strbuf_puts(g->out, text); }

static void indent(Gen *g) {
    for (int i = 0; i < g->indent; i++) put(g, "    ");
}

/* ---- types ---- */

static const char *base_type(Gen *g, const Type *type) {
    switch (type->kind) {
        case TY_REAL: return "float";
        case TY_BOOL: return "bool";
        case TY_CHAR: return "char";
        case TY_VOID: return "void";
        case TY_STRING: return "const char *";
        case TY_RECORD: return arena_printf(g->arena, "struct %s", type->record->c_name);
        case TY_POINTER: {
            const char *base = base_type(g, type->base);
            return arena_printf(g->arena, "%s%s*", base,
                                base[strlen(base) - 1] == '*' ? "" : " ");
        }
        case TY_ARRAY: return base_type(g, type->base);
        case TY_INT: case TY_ERROR: break;
    }
    return "int";
}

/* "int x", "float *p": no blank after a star. */
static void gen_declarator(Gen *g, const char *type, const char *stars, const char *name) {
    bool tight = type[strlen(type) - 1] == '*';
    strbuf_printf(g->out, "%s%s%s%s", type, tight ? "" : " ", stars, name);
}

/* ---- array bounds ---- */

/* The lower bound when it is known at compile time. */
static bool constant_lo(Gen *g, const Dim *dim, long long *lo) {
    *lo = g->config->index_base;
    return !dim->lo || const_int(dim->lo, lo);
}

static void gen_dim_size(Gen *g, const Dim *dim) {
    long long lo, hi;
    bool lo_known = constant_lo(g, dim, &lo);
    if (!dim->hi) return;
    if (!dim->lo || (lo_known && lo == 1)) {
        gen_expr(g, dim->hi, P_LOWEST);
    } else if (lo_known && const_int(dim->hi, &hi)) {
        strbuf_printf(g->out, "%lld", hi - lo + 1);
    } else if (lo_known && lo <= 0) {
        gen_expr(g, dim->hi, P_ADD);
        strbuf_printf(g->out, " + %lld", 1 - lo);
    } else {
        gen_expr(g, dim->hi, P_ADD);
        put(g, " - ");
        gen_expr(g, dim->lo, P_MUL);
        put(g, " + 1");
    }
}

static void gen_dims(Gen *g, const Type *type, int first) {
    for (int i = first; i < type->dims.count; i++) {
        put(g, "[");
        gen_dim_size(g, &type->dims.items[i]);
        put(g, "]");
    }
}

/* Bounds that are not constants are saved when the array comes into scope: the
 * variables they are made of may change later. */
static const char *saved_bound(Gen *g, const Symbol *sym, int dim, bool upper) {
    return arena_printf(g->arena, "plike_%s_%s_%d", upper ? "hi" : "lo", sym->c_name, dim + 1);
}

static bool saves_hi(Gen *g, const Dim *dim) {
    long long value;
    return g->config->bounds_check && dim->hi && !const_int(dim->hi, &value);
}

static void gen_saved_bounds(Gen *g, const Symbol *sym) {
    if (!sym || sym->type->kind != TY_ARRAY || !sym->indexed) return;
    for (int i = 0; i < sym->type->dims.count; i++) {
        const Dim *dim = &sym->type->dims.items[i];
        long long lo;
        if (!constant_lo(g, dim, &lo)) {
            indent(g);
            strbuf_printf(g->out, "const int %s = ", saved_bound(g, sym, i, false));
            gen_expr(g, dim->lo, P_LOWEST);
            put(g, ";\n");
        }
        if (saves_hi(g, dim)) {
            indent(g);
            strbuf_printf(g->out, "const int %s = ", saved_bound(g, sym, i, true));
            /* [n] counts elements, [lo..n] names the last one */
            bool count = !dim->lo && g->config->index_base == 0;
            gen_expr(g, dim->hi, count ? P_ADD : P_LOWEST);
            if (count) put(g, " - 1");
            put(g, ";\n");
        }
    }
}

/* ---- expressions ---- */

static bool is_by_reference(const Expr *expr) {
    return expr->kind == EX_NAME && expr->symbol && expr->symbol->by_reference;
}

/* `index - lo`, folded when that keeps the output readable. */
static void gen_shifted(Gen *g, const Expr *index, long long lo) {
    long long value;
    if (lo == 0) {
        gen_expr(g, index, P_LOWEST);
        return;
    }
    if (const_int(index, &value)) {
        strbuf_printf(g->out, "%lld", value - lo);
        return;
    }
    if (index->kind == EX_BINARY && (index->op == TOK_PLUS || index->op == TOK_MINUS) &&
        const_int(index->right, &value) && value >= 0 && value < 1000000) {
        long long rest = (index->op == TOK_PLUS ? value : -value) - lo;
        gen_expr(g, index->base, rest ? P_ADD : P_LOWEST);
        if (rest > 0) strbuf_printf(g->out, " + %lld", rest);
        if (rest < 0) strbuf_printf(g->out, " - %lld", -rest);
        return;
    }
    gen_expr(g, index, P_ADD);
    strbuf_printf(g->out, lo > 0 ? " - %lld" : " + %lld", lo > 0 ? lo : -lo);
}

/* The variable an array access starts from, and how many indices were already
 * applied to it. NULL for arrays inside records, whose bounds are constant. */
static const Symbol *indexed_symbol(const Expr *base, int *skipped) {
    *skipped = 0;
    while ((base->kind == EX_INDEX || base->kind == EX_APPLY) && !base->is_call &&
           base->base->type->kind == TY_ARRAY) {
        *skipped += base->args.count;
        base = base->base;
    }
    return base->kind == EX_NAME ? base->symbol : NULL;
}

static void gen_index(Gen *g, const Expr *expr) {
    const Type *type = expr->base->type;
    bool is_pointer = type->kind == TY_POINTER;
    if (is_pointer && is_by_reference(expr->base)) {
        put(g, "(");
        gen_expr(g, expr->base, P_LOWEST);
        put(g, ")");
    } else {
        gen_expr(g, expr->base, P_POSTFIX);
    }
    int skipped = 0;
    const Symbol *sym = is_pointer ? NULL : indexed_symbol(expr->base, &skipped);

    for (int i = 0; i < expr->args.count; i++) {
        const Expr *index = expr->args.items[i];
        put(g, "[");
        if (is_pointer) { /* no declared bounds: the base index applies */
            gen_shifted(g, index, g->config->index_base);
            put(g, "]");
            continue;
        }
        const Dim *dim = &type->dims.items[i];
        long long lo, hi;
        bool lo_known = constant_lo(g, dim, &lo);
        if (g->config->bounds_check) {
            g->uses_fail = true;
            put(g, "plike_index(");
            gen_expr(g, index, P_LOWEST);
            if (lo_known) strbuf_printf(g->out, ", %lld, ", lo);
            else strbuf_printf(g->out, ", %s, ", saved_bound(g, sym, skipped + i, false));
            if (!dim->hi) put(g, "INT_MAX");
            else if (const_int(dim->hi, &hi))
                strbuf_printf(g->out, "%lld", !dim->lo ? lo + hi - 1 : hi);
            else put(g, saved_bound(g, sym, skipped + i, true));
            strbuf_printf(g->out, ", %d)", index->loc.line);
        } else if (lo_known) {
            gen_shifted(g, index, lo);
        } else {
            gen_expr(g, index, P_ADD);
            strbuf_printf(g->out, " - %s", saved_bound(g, sym, skipped + i, false));
        }
        put(g, "]");
    }
}

/* The address of a variable. */
static void gen_address(Gen *g, const Expr *expr, int min) {
    if (is_by_reference(expr)) {
        put(g, expr->symbol->c_name);
    } else if (expr->kind == EX_UNARY && expr->op == TOK_STAR) {
        gen_expr(g, expr->base, min);
    } else {
        if (min > P_UNARY) put(g, "(");
        put(g, "&");
        gen_expr(g, expr, P_UNARY);
        if (min > P_UNARY) put(g, ")");
    }
}

static void gen_call(Gen *g, const Expr *expr) {
    const Symbol *callee = expr->symbol;
    const char *name = callee->c_name;
    if (callee->builtin && strcmp(name, "random_below") == 0) {
        g->uses_random = true;
        put(g, "plike_random(0, ");
        gen_expr(g, expr->args.items[0], P_ADD);
        put(g, " - 1)");
        return;
    }
    if (callee->builtin && (strcmp(name, "trunc") == 0 || strcmp(name, "round") == 0)) {
        g->uses_math = true;
        strbuf_printf(g->out, "(int)%s(", name);
        gen_expr(g, expr->args.items[0], P_LOWEST);
        put(g, ")");
        return;
    }
    if (callee->builtin && strcmp(name, "length") == 0) {
        g->uses_text = true;
        put(g, "(int)strlen(");
        gen_expr(g, expr->args.items[0], P_LOWEST);
        put(g, ")");
        return;
    }
    if (callee->builtin && expr->type->kind == TY_STRING) {
        g->uses_text = true;
        strbuf_printf(g->out, "plike_%s(", name);
        for (int i = 0; i < expr->args.count; i++) {
            if (i) put(g, ", ");
            gen_expr(g, expr->args.items[i], P_LOWEST);
        }
        if (strcmp(name, "substring") == 0) strbuf_printf(g->out, ", %d", g->config->index_base);
        put(g, ")");
        return;
    }
    if (callee->builtin && (strcmp(name, "min") == 0 || strcmp(name, "max") == 0)) {
        if (expr->type->kind == TY_REAL) {
            g->uses_math = true;
            strbuf_printf(g->out, "f%s(", name);
            gen_expr(g, expr->args.items[0], P_LOWEST);
            put(g, ", ");
            gen_expr(g, expr->args.items[1], P_LOWEST);
            put(g, ")");
        } else { /* C has none for integers */
            put(g, "(");
            gen_expr(g, expr->args.items[0], P_REL + 1);
            put(g, name[1] == 'i' ? " < " : " > ");
            gen_expr(g, expr->args.items[1], P_REL + 1);
            put(g, " ? ");
            gen_expr(g, expr->args.items[0], P_OR + 1);
            put(g, " : ");
            gen_expr(g, expr->args.items[1], P_OR + 1);
            put(g, ")");
        }
        return;
    }
    if (callee->builtin && strcmp(name, "random") == 0) {
        g->uses_random = true;
        name = "plike_random";
    } else if (callee->builtin && expr->type->kind == TY_INT) {
        g->uses_abs = true;
    } else if (callee->builtin) {
        g->uses_math = true;
        if (strcmp(name, "abs") == 0) name = "fabs";
    }
    strbuf_printf(g->out, "%s(", name);
    for (int i = 0; i < expr->args.count; i++) {
        const Expr *arg = expr->args.items[i];
        const Symbol *param = callee->params ? callee->params[i] : NULL;
        if (i > 0) put(g, ", ");
        if (param && param->flattened) {
            put(g, "(");
            gen_declarator(g, base_type(g, param->type), "*", "");
            put(g, ")");
            gen_expr(g, arg, P_UNARY);
            continue;
        }
        if (param && param->by_reference && arg->kind == EX_UNARY &&
            (arg->op == TOK_AMP || arg->op == TOK_AT))
            gen_expr(g, arg, P_LOWEST);
        else if (param && param->by_reference)
            gen_address(g, arg, P_LOWEST);
        else
            gen_expr(g, arg, P_LOWEST);
    }
    put(g, ")");
}

static void gen_number(Gen *g, const Expr *expr) {
    const char *text = expr->text;
    size_t length = strlen(text);
    if (expr->kind == EX_INT) {
        long long value;
        bool translated = length > 2 && text[0] == '0' && strchr("oObB", text[1]);
        if (translated && const_int(expr, &value)) /* not standard C */
            strbuf_printf(g->out, text[1] == 'b' || text[1] == 'B' ? "0x%llX" : "0%llo",
                          (unsigned long long)value);
        else put(g, text);
        return;
    }
    put(g, text); /* "1." and "1.f" are valid C */
}

/* Joining or comparing texts. */
static bool is_text_op(const Expr *expr) {
    return expr->kind == EX_BINARY && (expr->base->type->kind == TY_STRING || expr->right->type->kind == TY_STRING);
}

static int precedence(const Expr *expr) {
    switch (expr->kind) {
        case EX_UNARY:
            if (expr->op != TOK_AT) return P_UNARY;
            return is_by_reference(expr->base) ? P_POSTFIX : precedence(expr->base);
        case EX_NAME: return is_by_reference(expr) ? P_UNARY : P_POSTFIX;
        case EX_BINARY:
            if (is_text_op(expr)) return expr->type->kind == TY_STRING ? P_POSTFIX : P_EQ;
            break;
        default: return P_POSTFIX;
    }
    switch (expr->op) {
        case TOK_OR: return P_OR;
        case TOK_AND: return P_AND;
        case TOK_PIPE: return P_BITOR;
        case TOK_CARET: return P_BITXOR;
        case TOK_AMP: return P_BITAND;
        case TOK_EQ: case TOK_NE: return P_EQ;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return P_REL;
        case TOK_SHL: case TOK_SHR: return P_SHIFT;
        case TOK_PLUS: case TOK_MINUS: return P_ADD;
        default: return P_MUL;
    }
}

static const char *c_operator(TokenKind op) {
    switch (op) {
        case TOK_OR: return "||";
        case TOK_AND: return "&&";
        case TOK_PIPE: return "|";
        case TOK_CARET: return "^";
        case TOK_AMP: return "&";
        case TOK_EQ: return "==";
        case TOK_NE: return "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        default: return "%";
    }
}

/* Mixing these families without parentheses is legal C that nobody can read
 * (and that compilers warn about). */
static bool wants_parens(const Expr *parent, const Expr *child) {
    if (child->kind == EX_UNARY) /* "(!a) != b" */
        return child->op == TOK_NOT && precedence(parent) >= P_BITOR;
    if (child->kind != EX_BINARY) return false;
    int outer = precedence(parent), inner = precedence(child);
    if (outer == inner) return outer == P_EQ || outer == P_REL;
    if (outer == P_OR) return inner == P_AND;
    return (outer >= P_BITOR && outer <= P_BITAND) || outer == P_SHIFT ||
           (outer == P_EQ && inner == P_REL);
}

static void gen_operand(Gen *g, const Expr *parent, const Expr *child, int min) {
    gen_expr(g, child, wants_parens(parent, child) ? P_POSTFIX : min);
}

static void gen_unary(Gen *g, const Expr *expr) {
    const Expr *operand = expr->base;
    switch (expr->op) {
        case TOK_AT:
            if (is_by_reference(operand)) put(g, operand->symbol->c_name);
            else gen_expr(g, operand, P_LOWEST);
            return;
        case TOK_AMP: gen_address(g, operand, P_LOWEST); return;
        case TOK_NOT: put(g, "!"); break;
        case TOK_TILDE: put(g, "~"); break;
        case TOK_STAR: put(g, "*"); break;
        default: put(g, expr->op == TOK_MINUS ? "-" : "+");
    }
    /* "- -x" must not become "--x" */
    bool sign = operand->kind == EX_UNARY && (operand->op == TOK_MINUS || operand->op == TOK_PLUS);
    gen_expr(g, operand, sign ? P_POSTFIX : P_UNARY);
}

static void gen_field(Gen *g, const Expr *expr) {
    const Expr *base = expr->base;
    if (base->type->kind == TY_POINTER) {
        gen_expr(g, base, P_POSTFIX);
        put(g, "->");
    } else if (is_by_reference(base)) {
        strbuf_printf(g->out, "%s->", base->symbol->c_name);
    } else if (base->kind == EX_UNARY && base->op == TOK_STAR) {
        gen_expr(g, base->base, P_POSTFIX);
        put(g, "->");
    } else {
        gen_expr(g, base, P_POSTFIX);
        put(g, ".");
    }
    put(g, expr->symbol->c_name);
}

static void gen_as_text(Gen *g, const Expr *expr) {
    bool text = expr->type->kind == TY_STRING;
    if (!text) put(g, expr->type->kind == TY_REAL ? "plike_real_text(" : "plike_int_text(");
    gen_expr(g, expr, P_LOWEST);
    if (!text) put(g, ")");
}

static void gen_text_op(Gen *g, const Expr *expr) {
    g->uses_text = true;
    put(g, expr->type->kind == TY_STRING ? "plike_concat(" : "strcmp(");
    gen_as_text(g, expr->base);
    put(g, ", ");
    gen_as_text(g, expr->right);
    put(g, ")");
    if (expr->type->kind != TY_STRING) strbuf_printf(g->out, " %s 0", c_operator(expr->op));
}

static void gen_expr(Gen *g, const Expr *expr, int min) {
    int level = precedence(expr);
    bool parens = level < min;
    if (parens) put(g, "(");
    switch (expr->kind) {
        case EX_INT: case EX_FLOAT: gen_number(g, expr); break;
        case EX_BOOL: put(g, expr->op == TOK_TRUE ? "true" : "false"); break;
        case EX_STRING: strbuf_printf(g->out, "\"%s\"", expr->text); break;
        case EX_CHAR: strbuf_printf(g->out, "'%s'", expr->text); break;
        case EX_NAME:
            if (expr->is_call) gen_call(g, expr);
            else if (expr->symbol->kind == SYM_STATE) strbuf_printf(g->out, "%lld", expr->symbol->value);
            else strbuf_printf(g->out, "%s%s", is_by_reference(expr) ? "*" : "",
                               expr->symbol->c_name);
            break;
        case EX_APPLY:
            if (expr->is_call) gen_call(g, expr);
            else gen_index(g, expr);
            break;
        case EX_INDEX: gen_index(g, expr); break;
        case EX_FIELD: gen_field(g, expr); break;
        case EX_UNARY: gen_unary(g, expr); break;
        case EX_BINARY:
            if (is_text_op(expr)) {
                gen_text_op(g, expr);
                break;
            }
            if (expr->op == TOK_RDIV) {
                put(g, "(float)");
                gen_expr(g, expr->base, P_UNARY);
                put(g, " / ");
                gen_operand(g, expr, expr->right, level + 1);
                break;
            }
            gen_operand(g, expr, expr->base, level);
            strbuf_printf(g->out, " %s ", c_operator(expr->op));
            gen_operand(g, expr, expr->right, level + 1);
            break;
    }
    if (parens) put(g, ")");
}

/* ---- statements ---- */

static void gen_variable(Gen *g, const VarDecl *decl, const char *storage) {
    const Symbol *sym = decl->symbol;
    indent(g);
    put(g, storage);
    gen_declarator(g, base_type(g, sym->type), "", sym->c_name);
    if (sym->type->kind == TY_ARRAY) gen_dims(g, sym->type, 0);
    put(g, ";\n");
    if (!sym->is_global) gen_saved_bounds(g, sym);
}

static void gen_body(Gen *g, const Stmt *body) {
    put(g, "{\n");
    g->indent++;
    gen_statements(g, body);
    g->indent--;
    indent(g);
    put(g, "}");
}

static int line_count(const Gen *g);
static void map_lines(Gen *g, const Stmt *stmt, int first, int last);

static void gen_if(Gen *g, const Stmt *stmt) {
    for (const Stmt *branch = stmt; branch; branch = branch->otherwise) {
        if (branch != stmt) {
            if (g->map) map_lines(g, branch, line_count(g), line_count(g));
            put(g, " else ");
        }
        if (branch->kind == ST_IF) {
            put(g, "if (");
            gen_expr(g, branch->value, P_LOWEST);
            put(g, ") ");
        }
        gen_body(g, branch->body);
    }
    put(g, "\n");
}

static void gen_for(Gen *g, const Stmt *stmt) {
    long long step = 1;
    bool known = !stmt->step || const_int(stmt->step, &step);
    /* a step like -0.5 is not an integer, but its sign is plain to see */
    bool negative = known ? step < 0
                          : stmt->step->kind == EX_UNARY && stmt->step->op == TOK_MINUS &&
                            stmt->step->base->kind == EX_FLOAT;
    bool sign_known = known || negative || stmt->step->kind == EX_FLOAT;

    put(g, "for (");
    gen_expr(g, stmt->target, P_LOWEST);
    put(g, " = ");
    gen_expr(g, stmt->value, P_LOWEST);
    put(g, "; ");
    if (!sign_known) {
        gen_expr(g, stmt->step, P_REL + 1);
        put(g, " > 0 ? ");
        gen_expr(g, stmt->target, P_REL);
        put(g, " <= ");
        gen_expr(g, stmt->limit, P_REL + 1);
        put(g, " : ");
    }
    gen_expr(g, stmt->target, P_REL);
    put(g, !sign_known || negative ? " >= " : " <= ");
    gen_expr(g, stmt->limit, P_REL + 1);
    put(g, "; ");
    if (known && (step == 1 || step == -1)) {
        gen_expr(g, stmt->target, P_POSTFIX);
        put(g, step == 1 ? "++" : "--");
    } else if (known) {
        gen_expr(g, stmt->target, P_LOWEST);
        strbuf_printf(g->out, step > 0 ? " += %lld" : " -= %lld", step > 0 ? step : -step);
    } else {
        gen_expr(g, stmt->target, P_LOWEST);
        put(g, " += ");
        gen_expr(g, stmt->step, P_LOWEST);
    }
    put(g, ") ");
    gen_body(g, stmt->body);
    put(g, "\n");
}

static void gen_print(Gen *g, const Stmt *stmt) {
    if (stmt->args.count == 0) {
        put(g, "printf(\"\\n\");\n");
        return;
    }
    put(g, "printf(\"");
    for (int i = 0; i < stmt->args.count; i++) {
        if (i > 0 && !stmt->joined) put(g, " ");
        if (stmt->joined) {
            TypeKind kind = stmt->args.items[i]->type->kind;
            if (kind == TY_REAL) {
                put(g, "%.10g");
                continue;
            }
            if (kind == TY_BOOL) {
                put(g, "%s");
                continue;
            }
        }
        switch (stmt->args.items[i]->type->kind) {
            case TY_REAL: put(g, "%f"); break;
            case TY_CHAR: put(g, "%c"); break;
            case TY_STRING: case TY_ARRAY: put(g, "%s"); break;
            case TY_POINTER:
                put(g, stmt->args.items[i]->type->base->kind == TY_CHAR ? "%s" : "%p");
                break;
            default: put(g, "%d");
        }
    }
    put(g, stmt->same_line ? "\"" : "\\n\"");
    for (int i = 0; i < stmt->args.count; i++) {
        const Expr *arg = stmt->args.items[i];
        put(g, ", ");
        if (stmt->joined && arg->type->kind == TY_BOOL) {
            static const char *const WORDS[][2] = {{"1", "0"}, {"VERDADERO", "FALSO"}, {"VERDADEIRO", "FALSO"},
                                                   {"verdadeiro", "falso"}, {"TRUE", "FALSE"}};
            gen_expr(g, arg, P_POSTFIX);
            strbuf_printf(g->out, " ? \"%s\" : \"%s\"", WORDS[g->config->dialect][0], WORDS[g->config->dialect][1]);
            continue;
        }
        bool address = arg->type->kind == TY_POINTER && arg->type->base->kind != TY_CHAR;
        if (address) put(g, "(void *)");
        gen_expr(g, arg, address ? P_UNARY : P_LOWEST);
    }
    put(g, ");\n");
}

static void gen_read(Gen *g, const Stmt *stmt) {
    g->uses_fail = true;
    for (int i = 0; i < stmt->args.count; i++) {
        const Expr *arg = stmt->args.items[i];
        if (i > 0) indent(g);
        if (arg->type->kind == TY_STRING) {
            g->uses_text = true;
            gen_expr(g, arg, P_LOWEST);
            strbuf_printf(g->out, " = plike_read_text(%d);\n", stmt->loc.line);
            continue;
        }
        if (arg->type->kind == TY_BOOL) { /* scanf cannot store into a bool */
            put(g, "{ int plike_input; if (scanf(\"%d\", &plike_input) != 1) ");
            strbuf_printf(g->out, "plike_fail(\"invalid input\", %d); ", stmt->loc.line);
            gen_expr(g, arg, P_LOWEST);
            put(g, " = plike_input != 0; }\n");
            continue;
        }
        strbuf_printf(g->out, "if (scanf(\"%s\", ", arg->type->kind == TY_REAL ? "%f"
                                                    : arg->type->kind == TY_CHAR ? " %c" : "%d");
        gen_address(g, arg, P_LOWEST);
        strbuf_printf(g->out, ") != 1) plike_fail(\"invalid input\", %d);\n", stmt->loc.line);
    }
}

static bool is_main(const Symbol *sym) { return strcmp(sym->c_name, "main") == 0; }

static void gen_return(Gen *g, const Stmt *stmt) {
    if (stmt->value) {
        put(g, "return ");
        gen_expr(g, stmt->value, P_LOWEST);
        put(g, ";\n");
    } else if (g->function->result) {
        strbuf_printf(g->out, "return %s;\n", g->function->result->c_name);
    } else {
        put(g, is_main(g->function) ? "return 0;\n" : "return;\n");
    }
}

static int line_count(const Gen *g) {
    int count = 0;
    for (size_t i = 0; i < g->out->length; i++) count += g->out->data[i] == '\n';
    return count;
}

static void map_lines(Gen *g, const Stmt *stmt, int first, int last) {
    LineMap *map = g->map;
    if (!map) return;
    if (map->count == map->capacity) {
        int capacity = map->capacity ? map->capacity * 2 : 64;
        LineSpan *items = arena_alloc(g->arena, (size_t)capacity * sizeof(LineSpan));
        if (map->count) memcpy(items, map->items, (size_t)map->count * sizeof(LineSpan));
        map->items = items;
        map->capacity = capacity;
    }
    map->items[map->count++] = (LineSpan){stmt, first, last};
}

static void gen_one_statement(Gen *g, const Stmt *stmt);

static void gen_statement(Gen *g, const Stmt *stmt) {
    int first = g->map ? line_count(g) : 0;
    gen_one_statement(g, stmt);
    if (g->map) map_lines(g, stmt, first, line_count(g) - 1);
}

static void gen_one_statement(Gen *g, const Stmt *stmt) {
    if (stmt->kind == ST_VAR) {
        for (const VarDecl *v = stmt->vars; v; v = v->next) gen_variable(g, v, "");
        return;
    }
    indent(g);
    switch (stmt->kind) {
        case ST_ASSIGN:
            gen_expr(g, stmt->target, P_LOWEST);
            put(g, " = ");
            gen_expr(g, stmt->value, P_LOWEST);
            put(g, ";\n");
            break;
        case ST_CALL:
            gen_expr(g, stmt->value, P_LOWEST);
            put(g, ";\n");
            break;
        case ST_IF: gen_if(g, stmt); break;
        case ST_FOR: gen_for(g, stmt); break;
        case ST_WHILE:
            put(g, "while (");
            gen_expr(g, stmt->value, P_LOWEST);
            put(g, ") ");
            gen_body(g, stmt->body);
            put(g, "\n");
            break;
        case ST_REPEAT:
            put(g, "do ");
            gen_body(g, stmt->body);
            put(g, " while (!");
            gen_expr(g, stmt->value, P_POSTFIX);
            put(g, ");\n");
            break;
        case ST_RETURN: gen_return(g, stmt); break;
        case ST_PRINT: gen_print(g, stmt); break;
        case ST_READ: gen_read(g, stmt); break;
        case ST_BLOCK:
            gen_body(g, stmt->body);
            put(g, "\n");
            break;
        case ST_TODO: put(g, "/* ... */\n"); break;
        default: break; /* systems are not translated */
    }
}

static void gen_statements(Gen *g, const Stmt *stmt) {
    for (; stmt; stmt = stmt->next) gen_statement(g, stmt);
}

/* ---- declarations ---- */

static bool mentions_param(const Expr *expr, int from) {
    if (!expr) return false;
    if (expr->kind == EX_NAME && expr->symbol && expr->symbol->kind == SYM_PARAM &&
        expr->symbol->position >= from)
        return true;
    if (mentions_param(expr->base, from) || mentions_param(expr->right, from)) return true;
    for (int i = 0; i < expr->args.count; i++)
        if (mentions_param(expr->args.items[i], from)) return true;
    return false;
}

static const char *flat_name(Gen *g, const Symbol *param) {
    return arena_printf(g->arena, "plike_flat_%s", param->c_name);
}

static void gen_param(Gen *g, const Symbol *param) {
    const char *type = base_type(g, param->type);
    if (param->type->kind != TY_ARRAY) {
        gen_declarator(g, type, param->by_reference ? "*" : "", param->c_name);
    } else if (param->flattened) {
        gen_declarator(g, type, "*", flat_name(g, param));
    } else {
        gen_declarator(g, type, "", param->c_name);
        /* C cannot refer to a parameter that comes later; the first bound can
         * simply be left out */
        const Dim *first = &param->type->dims.items[0];
        if (mentions_param(first->lo, param->position) || mentions_param(first->hi, param->position)) {
            put(g, "[]");
            gen_dims(g, param->type, 1);
        } else {
            gen_dims(g, param->type, 0);
        }
    }
}

static void gen_signature(Gen *g, const Symbol *sym) {
    const char *type = is_main(sym) ? "int" : base_type(g, sym->type);
    gen_declarator(g, type, "", sym->c_name);
    put(g, "(");
    for (int i = 0; i < sym->param_count; i++) {
        if (i > 0) put(g, ", ");
        gen_param(g, sym->params[i]);
    }
    put(g, sym->param_count ? ")" : "void)");
}

static void gen_external(Gen *g, const Symbol *sym) {
    put(g, "extern ");
    gen_declarator(g, base_type(g, sym->type), "", sym->c_name);
    put(g, "(");
    for (int i = 0; i < sym->param_count; i++) {
        const Type *type = sym->params[i]->type;
        const char *base = base_type(g, type);
        bool star = base[strlen(base) - 1] == '*';
        strbuf_printf(g->out, "%s%s%s", i ? ", " : "", base,
                      type->kind != TY_ARRAY ? "" : star ? "*" : " *");
    }
    put(g, sym->param_count ? ");\n" : "void);\n");
}

static bool ends_with_return(const Stmt *body) {
    while (body && body->next) body = body->next;
    return body && body->kind == ST_RETURN;
}

static void gen_function(Gen *g, const Function *function) {
    const Symbol *sym = function->symbol;
    g->function = sym;
    gen_signature(g, sym);
    put(g, " {\n");
    g->indent = 1;
    if (sym->result) {
        indent(g);
        gen_declarator(g, base_type(g, sym->type), "", sym->result->c_name);
        put(g, sym->type->kind == TY_RECORD ? " = {0};\n" : sym->type->kind == TY_STRING ? " = \"\";\n" : " = 0;\n");
    }
    for (int i = 0; i < sym->param_count; i++) {
        const Symbol *param = sym->params[i];
        if (param->flattened) { /* give the flat pointer its shape back */
            const char *type = base_type(g, param->type);
            indent(g);
            gen_declarator(g, type, "", "");
            strbuf_printf(g->out, "(*%s)", param->c_name);
            gen_dims(g, param->type, 1);
            put(g, " = (");
            gen_declarator(g, type, "", "");
            put(g, "(*)");
            gen_dims(g, param->type, 1);
            strbuf_printf(g->out, ")%s;\n", flat_name(g, param));
        }
        gen_saved_bounds(g, param);
    }
    for (const VarDecl *v = function->locals; v; v = v->next) gen_variable(g, v, "");
    gen_statements(g, function->body);
    if (!ends_with_return(function->body)) {
        if (sym->result) strbuf_printf(g->out, "    return %s;\n", sym->result->c_name);
        else if (is_main(sym)) put(g, "    return 0;\n");
    }
    put(g, "}\n");
    g->indent = 0;
}

static void gen_record(Gen *g, const Record *record) {
    strbuf_printf(g->out, "struct %s {\n", record->c_name);
    g->indent = 1;
    for (const VarDecl *field = record->fields; field; field = field->next)
        gen_variable(g, field, "");
    g->indent = 0;
    put(g, "};\n\n");
}

static const struct {
    const char *name, *code;
} TEXT_HELPERS[] = { /* only those a program uses are written */
    {"plike_concat(",
        "static inline const char *plike_concat(const char *a, const char *b) {\n"
        "    char *text = plike_new_text(strlen(a) + strlen(b));\n"
        "    strcpy(text, a);\n"
        "    strcat(text, b);\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_int_text(",
        "static inline const char *plike_int_text(int n) {\n"
        "    char *text = plike_new_text(16);\n"
        "    snprintf(text, 16, \"%d\", n);\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_real_text(",
        "static inline const char *plike_real_text(double x) {\n"
        "    char *text = plike_new_text(32);\n"
        "    snprintf(text, 32, \"%.10g\", x);\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_read_text(",
        "static inline const char *plike_read_text(int line) {\n"
        "    char *text = plike_new_text(1999);\n"
        "    if (scanf(\" %1999[^\\n]\", text) != 1) {\n"
        "        fprintf(stderr, \"runtime error at line %d: invalid input\\n\", line);\n"
        "        exit(1);\n"
        "    }\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_upper(",
        "static inline const char *plike_upper(const char *s) {\n"
        "    char *text = plike_new_text(strlen(s));\n"
        "    for (size_t i = 0; s[i]; i++) text[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_lower(",
        "static inline const char *plike_lower(const char *s) {\n"
        "    char *text = plike_new_text(strlen(s));\n"
        "    for (size_t i = 0; s[i]; i++) text[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);\n"
        "    return text;\n"
        "}\n\n"},
    {"plike_substring(",
        "static inline const char *plike_substring(const char *s, int from, int to, int base) {\n"
        "    int length = (int)strlen(s);\n"
        "    from = from - base < 0 ? 0 : from - base;\n"
        "    to = to - base > length - 1 ? length - 1 : to - base;\n"
        "    if (to < from) return \"\";\n"
        "    char *text = plike_new_text((size_t)(to - from + 1));\n"
        "    memcpy(text, s + from, (size_t)(to - from + 1));\n"
        "    return text;\n"
        "}\n\n"},
};

void generate(const Program *program, const Config *config, Arena *arena, StrBuf *out) {
    /* The body goes first, to know which helpers and headers it needs. */
    StrBuf body = {0};
    Gen gen = {.program = program, .config = config, .arena = arena, .out = &body};
    Gen *g = &gen;
    strbuf_puts(&body, "");

    for (const Record *r = program->records; r; r = r->next) gen_record(g, r);
    for (const VarDecl *v = program->globals; v; v = v->next) gen_variable(g, v, "");
    if (program->globals) put(g, "\n");

    for (const Symbol *e = program->externals; e; e = e->next) gen_external(g, e);
    if (program->externals) put(g, "\n");
    for (const Function *f = program->functions; f; f = f->next) {
        if (f->is_test) continue;
        gen_signature(g, f->symbol);
        put(g, ";\n");
    }
    for (const Function *f = program->functions; f; f = f->next) {
        if (f->is_test) continue;
        put(g, "\n");
        gen_function(g, f);
    }

    if (config->bounds_check && g->uses_fail) strbuf_puts(out, "#include <limits.h>\n");
    if (g->uses_math) strbuf_puts(out, "#include <math.h>\n");
    strbuf_puts(out, "#include <stdbool.h>\n#include <stdio.h>\n");
    if (g->uses_fail || g->uses_abs || g->uses_text) strbuf_puts(out, "#include <stdlib.h>\n");
    if (g->uses_text) strbuf_puts(out, "#include <string.h>\n");
    strbuf_puts(out, "\n");
    bool indexes = config->bounds_check && strstr(body.data, "plike_index(");
    if (strstr(body.data, "plike_fail(") || indexes) {
        strbuf_puts(out,
            "static inline void plike_fail(const char *message, int line) {\n"
            "    fflush(stdout);\n"
            "    fprintf(stderr, \"runtime error at line %d: %s\\n\", line, message);\n"
            "    exit(1);\n"
            "}\n\n");
    }
    if (g->uses_text) {
        strbuf_puts(out,
            "static inline char *plike_new_text(size_t length) {\n"
            "    char *text = malloc(length + 1);\n"
            "    if (!text) exit(1);\n"
            "    text[length] = '\\0';\n"
            "    return text;\n"
            "}\n\n");
        for (size_t i = 0; i < sizeof TEXT_HELPERS / sizeof TEXT_HELPERS[0]; i++)
            if (strstr(body.data, TEXT_HELPERS[i].name)) strbuf_puts(out, TEXT_HELPERS[i].code);
    }
    if (g->uses_random) {
        long long seed = 1;
        if (program->seed) const_int(program->seed, &seed);
        strbuf_printf(out, "static unsigned plike_seed = %uu;\n\n", (unsigned)seed);
        strbuf_puts(out,
            "static inline int plike_random(int lo, int hi) {\n"
            "    unsigned n = 0;\n"
            "    if (hi <= lo) return lo;\n"
            "    for (int i = 0; i < 2; i++) {\n"
            "        plike_seed = plike_seed * 1103515245u + 12345u;\n"
            "        n = n << 15 | (plike_seed >> 16 & 0x7fff);\n"
            "    }\n"
            "    return (int)(lo + (long long)(n % (unsigned)((long long)hi - lo + 1)));\n"
            "}\n\n");
    }
    if (indexes) {
        strbuf_puts(out,
            "static inline int plike_index(int index, int lo, int hi, int line) {\n"
            "    if (index < lo || index > hi) plike_fail(\"array index out of bounds\", line);\n"
            "    return index - lo;\n"
            "}\n\n");
    }
    strbuf_append(out, body.data, body.length);
    strbuf_free(&body);
}

bool has_main(const Program *program) {
    for (const Function *f = program->functions; f; f = f->next)
        if (f->symbol && is_main(f->symbol)) return true;
    return false;
}

void generate_function(const Program *program, const Config *config, Arena *arena,
                       const Function *function, StrBuf *out, LineMap *map) {
    Gen gen = {.program = program, .config = config, .arena = arena, .out = out, .map = map};
    strbuf_puts(out, "");
    gen_function(&gen, function);
}

void generate_record(const Program *program, const Config *config, Arena *arena,
                     const Record *record, StrBuf *out) {
    Gen gen = {.program = program, .config = config, .arena = arena, .out = out};
    strbuf_puts(out, "");
    gen_record(&gen, record);
}
