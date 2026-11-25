#include "plike/target.h"
#include "plike/vm.h"

#include <string.h>

/* One generator for the languages that have classes: what changes from one to
 * the other is how things are spelled, and three choices.
 *  - Arrays know their length: they are vectors, lists, and so on.
 *  - Where objects are references (Java, JavaScript, TypeScript, Python) a
 *    pointer to a record is the object itself; pointers to plain values do not
 *    exist there.
 *  - Only C++ has parameters by reference: elsewhere a subprogram gives the
 *    values of its out and in/out parameters back with its result. */

enum {
    P_LOWEST, P_OR, P_AND, P_BITOR, P_BITXOR, P_BITAND, P_EQ, P_REL, P_SHIFT,
    P_ADD, P_MUL, P_UNARY, P_POSTFIX
};

typedef struct {
    Lang lang;
    const Program *program;
    const Config *config;
    Arena *arena;
    StrBuf *out;
    const Function *function;
    int indent;
    LineMap *map;
    unsigned needs;
    bool unsafe;        /* Rust: there are globals or pointers */
    bool in_class;      /* methods are being written inside their class */
    struct {
        const Expr *call;
        const char *name;
    } temps[32];        /* calls of the current statement done before it */
    int temp_count, serial;
    const Record *copied[64];
    int copied_count;
    const char *main_class;
    struct {
        const Symbol *symbol;
        const char *name;
    } renames[64];      /* variables of inner blocks that would hide, or be, others */
    int rename_count;
    const char *seen[128];
    int seen_count;
} Gen;

#define IS(l) (g->lang == (l))
#define PY IS(LANG_PYTHON)
#define RUST IS(LANG_RUST)
#define CPP IS(LANG_CPP)
#define JAVA IS(LANG_JAVA)
#define TS IS(LANG_TS)
#define JS (IS(LANG_JS) || IS(LANG_TS))

/* Objects are references. */
static bool refs(const Gen *g) { return g->lang == LANG_JAVA || g->lang == LANG_JS || g->lang == LANG_TS || g->lang == LANG_PYTHON; }
/* No parameters by reference for plain values. */
static bool gives_back(const Gen *g) { return g->lang != LANG_CPP; }

static void expr(Gen *g, const Expr *e, int min);
static void value(Gen *g, const Expr *e, const Type *want, int min);
static void statements(Gen *g, const Stmt *stmt);
static void call_text(Gen *g, const Expr *e);
static void zero(Gen *g, const Type *type, int dim);
static const char *type_text(Gen *g, const Type *type);

static void put(Gen *g, const char *text) { strbuf_puts(g->out, text); }
#define putf(g, ...) strbuf_printf((g)->out, __VA_ARGS__)

static void indent(Gen *g) {
    for (int i = 0; i < g->indent; i++) put(g, "    ");
}

static void end(Gen *g) { put(g, PY ? "\n" : ";\n"); }

static void open(Gen *g) {
    put(g, PY ? ":\n" : " {\n");
    g->indent++;
}

static void close(Gen *g) {
    g->indent--;
    if (PY) return;
    indent(g);
    put(g, "}");
}

/* ---- names ---- */

static const char *safe(Gen *g, const char *name) {
    return lang_reserves(g->lang, name) ? arena_printf(g->arena, "%s_", name) : name;
}

static const char *self_name(const Gen *g) {
    return g->lang == LANG_PYTHON || g->lang == LANG_RUST ? "self" : "this";
}

static bool is_self(const Expr *e) { return e->kind == EX_NAME && e->symbol && strcmp(e->symbol->name, "self") == 0 && e->symbol->kind == SYM_PARAM; }

static bool is_main(const Function *f) { return !f->owner && !f->is_test && f->symbol && strcmp(f->symbol->c_name, "main") == 0; }

static const char *function_name(Gen *g, const Function *f) {
    if (f->owner) return safe(g, f->name);
    if (f->is_handler) return arena_printf(g->arena, "on_%s", f->name);
    if (f->is_test) return f->symbol->c_name;
    if (is_main(f) && (RUST || JAVA || CPP)) return "run";
    return safe(g, f->symbol->c_name);
}

static const char *variable_name(Gen *g, const Symbol *sym) {
    for (int i = 0; i < g->rename_count; i++)
        if (g->renames[i].symbol == sym) return g->renames[i].name;
    if (sym->is_global && sym->kind == SYM_VAR && RUST) return arena_printf(g->arena, "(*G).%s", safe(g, sym->c_name));
    return safe(g, sym->c_name);
}

/* Types begin with a capital letter: "var origin: record" must not give a type
 * and a variable the same name, which only C keeps apart. */
static const char *record_name(Gen *g, const Record *record) {
    const char *name = record->c_name;
    if (name[0] >= 'a' && name[0] <= 'z') {
        char *capital = arena_printf(g->arena, "%s", name);
        capital[0] = (char)(capital[0] - 'a' + 'A');
        name = capital;
    }
    return safe(g, name);
}

static const char *class_name(Gen *g, const Class *klass) { return record_name(g, klass->record); }

/* A local of the current function hides a field with the same name. */
static bool hidden_by_local(const Gen *g, const char *name) {
    if (!g->function) return false;
    for (const VarDecl *v = g->function->params; v; v = v->next)
        if (v->symbol && strcmp(v->symbol->c_name, name) == 0) return true;
    for (const VarDecl *v = g->function->locals; v; v = v->next)
        if (v->symbol && strcmp(v->symbol->c_name, name) == 0) return true;
    return g->function->symbol && g->function->symbol->result && strcmp(g->function->symbol->result->c_name, name) == 0;
}

/* ---- what is passed how ---- */

static bool is_object(const Type *type) { return type->kind == TY_RECORD; }

/* A plain value that the subprogram changes for its caller. */
static bool given_back(const Gen *g, const Symbol *param) {
    return gives_back(g) && param->by_reference && !is_object(param->type);
}

static int given_back_count(const Gen *g, const Symbol *callee) {
    int n = 0;
    for (int i = 0; callee->params && i < callee->param_count; i++) n += given_back(g, callee->params[i]);
    return n;
}

static bool has_result(const Symbol *callee) { return callee->type && callee->type->kind != TY_VOID; }

/* The variable an expression is a part of. */
static const Symbol *root_of(const Expr *e) {
    while (e) {
        if (e->kind == EX_NAME) return e->is_call ? NULL : e->symbol;
        if ((e->kind == EX_APPLY || e->kind == EX_INDEX) && e->is_call) return NULL;
        if (e->kind == EX_UNARY && e->op != TOK_STAR && e->op != TOK_AT && e->op != TOK_AMP) return NULL;
        if (e->kind != EX_APPLY && e->kind != EX_INDEX && e->kind != EX_FIELD && e->kind != EX_UNARY) return NULL;
        e = e->base;
    }
    return NULL;
}

static bool writes_in_expr(const Gen *g, const Expr *e, const Symbol *sym, int depth);

/* Does the subprogram change (a part of) this variable? */
static bool writes(const Gen *g, const Stmt *s, const Symbol *sym, int depth) {
    for (; s; s = s->next) {
        if ((s->kind == ST_ASSIGN || s->kind == ST_FOR) && root_of(s->target) == sym) return true;
        if (s->kind == ST_READ)
            for (int i = 0; i < s->args.count; i++)
                if (root_of(s->args.items[i]) == sym) return true;
        if (writes_in_expr(g, s->target, sym, depth) || writes_in_expr(g, s->value, sym, depth) ||
            writes_in_expr(g, s->limit, sym, depth) || writes_in_expr(g, s->step, sym, depth))
            return true;
        for (int i = 0; i < s->args.count; i++)
            if (writes_in_expr(g, s->args.items[i], sym, depth)) return true;
        if (writes(g, s->body, sym, depth)) return true;
        for (const Stmt *o = s->otherwise; o; o = o->otherwise) {
            if (writes_in_expr(g, o->value, sym, depth) || writes(g, o->body, sym, depth)) return true;
        }
    }
    return false;
}

static bool writes_in_expr(const Gen *g, const Expr *e, const Symbol *sym, int depth) {
    if (!e) return false;
    if (e->is_call && e->symbol && e->symbol->params) {
        for (int i = 0; i < e->args.count && i < e->symbol->param_count; i++) {
            const Symbol *param = e->symbol->params[i];
            if (root_of(e->args.items[i]) != sym) continue;
            if (param->by_reference) return true;
            if (param->type->kind == TY_ARRAY) {
                const Function *callee = e->symbol->function;
                if (!callee || depth > 3 || writes(g, callee->body, param, depth + 1)) return true;
            }
        }
    }
    if (e->kind == EX_UNARY && e->op == TOK_AMP && root_of(e->base) == sym) return true;
    if (writes_in_expr(g, e->base, sym, depth) || writes_in_expr(g, e->right, sym, depth)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (writes_in_expr(g, e->args.items[i], sym, depth)) return true;
    return false;
}

static bool param_written(const Gen *g, const Function *f, const Symbol *param) {
    return !f || writes(g, f->body, param, 0);
}

/* ---- types ---- */

static const char *scalar_type(Gen *g, const Type *type) {
    static const char *const NAMES[][LANG_COUNT] = {
        /*            C      C++            Java       Rust      TS         JS   Python */
        [TY_INT] = {"int", "int", "int", "i32", "number", "", "int"},
        [TY_REAL] = {"float", "double", "double", "f64", "number", "", "float"},
        [TY_BOOL] = {"bool", "bool", "boolean", "bool", "boolean", "", "bool"},
        [TY_CHAR] = {"char", "char", "char", "char", "string", "", "str"},
        [TY_STRING] = {"", "std::string", "String", "String", "string", "", "str"},
        [TY_VOID] = {"void", "void", "void", "()", "void", "", "None"},
    };
    switch (type->kind) {
        case TY_RECORD:
            if (RUST && type->record->klass && type->record->klass->is_interface)
                return arena_printf(g->arena, "dyn %s", record_name(g, type->record));
            return record_name(g, type->record);
        case TY_POINTER: {
            if (type->base->kind == TY_VOID) /* whoever sent a message */
                return CPP ? "Component *" : RUST ? "*mut dyn Component" : TS ? "Component | null" : PY ? "Component | None" : "Component";
            const char *base = type_text(g, type->base);
            if (CPP) return arena_printf(g->arena, "%s%s*", base, base[strlen(base) - 1] == '*' ? "" : " ");
            if (RUST) return arena_printf(g->arena, "*mut %s", base);
            if (TS) return arena_printf(g->arena, "%s | null", base);
            if (PY) return arena_printf(g->arena, "%s | None", base);
            return base;
        }
        case TY_ERROR: return NAMES[TY_INT][g->lang];
        default: return NAMES[type->kind][g->lang];
    }
}

/* An array of `count` dimensions of this. */
static const char *array_of(Gen *g, const char *element, int count) {
    for (int i = 0; i < count; i++) {
        if (CPP) element = arena_printf(g->arena, "std::vector<%s>", element);
        else if (RUST) element = arena_printf(g->arena, "Vec<%s>", element);
        else if (PY) element = arena_printf(g->arena, "list[%s]", element);
        else element = arena_printf(g->arena, strchr(element, '|') ? "(%s)[]" : "%s[]", element);
    }
    return element;
}

static const char *type_text(Gen *g, const Type *type) {
    if (type->kind == TY_ARRAY) return array_of(g, scalar_type(g, type->base), type->dims.count);
    return scalar_type(g, type);
}

/* ---- sizes and indices ---- */

static bool constant_lo(const Gen *g, const Dim *dim, long long *lo) {
    *lo = g->config->index_base;
    return !dim->lo || const_int(dim->lo, lo);
}

static const char *saved_lo(Gen *g, const Symbol *sym, int dim) {
    return arena_printf(g->arena, "lo_%s_%d", sym->c_name, dim + 1);
}

/* e + delta, folded when e ends in a constant. */
static void shifted(Gen *g, const Expr *e, long long delta, int min) {
    long long n;
    const Type *whole = e->type; /* indices and sizes are integers */
    if (const_int(e, &n) && e->kind != EX_NAME) {
        putf(g, "%lld", n + delta);
        return;
    }
    if (delta == 0) {
        value(g, e, whole, min);
        return;
    }
    if (min > P_ADD) put(g, "(");
    if (e->kind == EX_BINARY && (e->op == TOK_PLUS || e->op == TOK_MINUS) && e->type->kind == TY_INT &&
        const_int(e->right, &n) && n >= 0 && n < 1000000) {
        long long rest = (e->op == TOK_PLUS ? n : -n) + delta;
        value(g, e->base, whole, rest ? P_ADD : P_LOWEST);
        if (rest > 0) putf(g, " + %lld", rest);
        if (rest < 0) putf(g, " - %lld", -rest);
    } else {
        value(g, e, whole, P_ADD);
        putf(g, delta < 0 ? " - %lld" : " + %lld", delta < 0 ? -delta : delta);
    }
    if (min > P_ADD) put(g, ")");
}

/* How many elements a dimension has. */
static void dim_size(Gen *g, const Dim *dim, int min) {
    long long lo, hi;
    bool lo_known = constant_lo(g, dim, &lo);
    if (!dim->hi) {
        put(g, "0");
    } else if (!dim->lo) {
        shifted(g, dim->hi, 0, min);
    } else if (lo_known && const_int(dim->hi, &hi)) {
        putf(g, "%lld", hi - lo + 1);
    } else if (lo_known) {
        shifted(g, dim->hi, 1 - lo, min);
    } else {
        if (min > P_ADD) put(g, "(");
        expr(g, dim->hi, P_ADD);
        put(g, " - ");
        expr(g, dim->lo, P_MUL);
        put(g, " + 1");
        if (min > P_ADD) put(g, ")");
    }
}

/* The first value of a variable of this type; of an array, from dimension `dim`. */
static void zero(Gen *g, const Type *type, int dim) {
    if (type->kind == TY_ARRAY && dim < type->dims.count) {
        const Dim *d = &type->dims.items[dim];
        bool last = dim == type->dims.count - 1;
        bool plain = last && type->base->kind != TY_RECORD && type->base->kind != TY_STRING;
        const char *inner = array_of(g, scalar_type(g, type->base), type->dims.count - dim - 1);
        if (CPP) {
            putf(g, "std::vector<%s>(", inner);
            dim_size(g, d, P_LOWEST);
            if (!last) {
                put(g, ", ");
                zero(g, type, dim + 1);
            }
            put(g, ")");
        } else if (RUST) {
            put(g, "vec![");
            zero(g, type, dim + 1);
            put(g, "; ");
            long long n;
            if (d->hi && const_int(d->hi, &n) && (!d->lo || const_int(d->lo, &n))) {
                dim_size(g, d, P_LOWEST);
            } else {
                dim_size(g, d, P_UNARY);
                put(g, " as usize");
            }
            put(g, "]");
        } else if (JAVA && type->base->kind != TY_RECORD && type->base->kind != TY_STRING) {
            putf(g, "new %s", scalar_type(g, type->base));
            for (int i = dim; i < type->dims.count; i++) {
                put(g, "[");
                dim_size(g, &type->dims.items[i], P_LOWEST);
                put(g, "]");
            }
        } else if (JAVA) {
            put(g, "java.util.stream.Stream.generate(() -> ");
            zero(g, type, dim + 1);
            put(g, ").limit(");
            dim_size(g, d, P_LOWEST);
            putf(g, ").toArray(%s[]::new)", inner);
        } else if (PY && plain) {
            put(g, "[");
            zero(g, type, dim + 1);
            put(g, "] * ");
            dim_size(g, d, P_MUL + 1);
        } else if (PY) {
            put(g, "[");
            zero(g, type, dim + 1);
            put(g, " for _ in range(");
            dim_size(g, d, P_LOWEST);
            put(g, ")]");
        } else if (plain) {
            put(g, "new Array(");
            dim_size(g, d, P_LOWEST);
            put(g, ").fill(");
            zero(g, type, dim + 1);
            put(g, ")");
        } else {
            put(g, "Array.from({ length: ");
            dim_size(g, d, P_LOWEST);
            put(g, " }, () => ");
            zero(g, type, dim + 1);
            put(g, ")");
        }
        return;
    }
    if (type->kind == TY_ARRAY) type = type->base;
    switch (type->kind) {
        case TY_REAL: put(g, "0.0"); break;
        case TY_BOOL: put(g, PY ? "False" : "false"); break;
        case TY_CHAR: put(g, JS || PY ? "\" \"" : "' '"); break;
        case TY_STRING: put(g, CPP ? "std::string()" : RUST ? "String::new()" : "\"\""); break;
        case TY_POINTER: put(g, CPP ? "nullptr" : RUST ? "std::ptr::null_mut()" : PY ? "None" : "null"); break;
        case TY_RECORD:
            putf(g, CPP ? "%s()" : RUST ? "%s::new()" : PY ? "%s()" : "new %s()", record_name(g, type->record));
            break;
        default: put(g, "0");
    }
}

/* ---- conversions ---- */

static const char *null_text(const Gen *g) {
    return g->lang == LANG_CPP ? "nullptr" : g->lang == LANG_RUST ? "std::ptr::null_mut()" : g->lang == LANG_PYTHON ? "None" : "null";
}

static bool is_zero_literal(const Expr *e) {
    long long n;
    return e->kind == EX_INT && const_int(e, &n) && n == 0;
}

static bool is_place(const Expr *e) {
    switch (e->kind) {
        case EX_NAME: return !e->is_call;
        case EX_APPLY: case EX_INDEX: return !e->is_call;
        case EX_FIELD: return true;
        case EX_UNARY: return e->op == TOK_STAR;
        default: return false;
    }
}

static void note_copy(Gen *g, const Record *record) {
    g->needs |= N_COPY;
    for (int i = 0; i < g->copied_count; i++)
        if (g->copied[i] == record) return;
    if (g->copied_count < 64) g->copied[g->copied_count++] = record;
}

/* The expression as a value of the wanted type: with what the language asks
 * to turn one kind of value into another, and a copy where the language would
 * share the object. */
static void value(Gen *g, const Expr *e, const Type *want, int min) {
    TypeKind from = e->type->kind, to = want ? want->kind : from;
    if (to == TY_ERROR || from == TY_ERROR) to = from;
    if (to == TY_POINTER && is_zero_literal(e)) {
        put(g, null_text(g));
        return;
    }
    if (to == TY_POINTER && from == TY_ARRAY) { /* the address of the first element */
        expr(g, e, P_POSTFIX);
        put(g, CPP ? ".data()" : ".as_mut_ptr()");
        return;
    }
    if (to == TY_POINTER && from == TY_POINTER && e->type->base->kind == TY_VOID && want->base->kind == TY_RECORD) {
        const char *name = record_name(g, want->base->record); /* the sender, as what it is known to be */
        if (CPP) {
            putf(g, "static_cast<%s *>(", name);
            expr(g, e, P_LOWEST);
            put(g, ")");
        } else if (JAVA) {
            if (min > P_UNARY) put(g, "(");
            putf(g, "(%s) ", name);
            expr(g, e, P_UNARY);
            if (min > P_UNARY) put(g, ")");
        } else if (TS) {
            if (min > P_REL) put(g, "(");
            expr(g, e, P_REL + 1);
            putf(g, " as %s", name);
            if (min > P_REL) put(g, ")");
        } else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            putf(g, " as *mut %s", name);
            if (min > P_UNARY) put(g, ")");
        } else {
            expr(g, e, min);
        }
        return;
    }
    if (from == to || to == TY_VOID || to == TY_ARRAY || from == TY_ARRAY || to == TY_POINTER || from == TY_POINTER ||
        to == TY_RECORD) {
        bool place = is_place(e) && !(e->kind == EX_UNARY && refs(g));
        if (from == TY_BOOL && to == TY_BOOL) {
            expr(g, e, min);
        } else if (from == TY_RECORD && place && RUST) {
            expr(g, e, P_POSTFIX);
            put(g, ".clone()");
        } else if (from == TY_RECORD && is_place(e) && PY) {
            note_copy(g, e->type->record);
            put(g, "copy.deepcopy(");
            expr(g, e, P_LOWEST);
            put(g, ")");
        } else if (from == TY_RECORD && is_place(e) && refs(g)) {
            note_copy(g, e->type->record);
            expr(g, e, P_POSTFIX);
            put(g, ".copy()");
        } else if (from == TY_STRING && RUST && (place || e->kind == EX_STRING)) {
            if (e->kind == EX_STRING) {
                put(g, "String::from(");
                expr(g, e, P_LOWEST);
                put(g, ")");
            } else {
                expr(g, e, P_POSTFIX);
                put(g, ".clone()");
            }
        } else {
            expr(g, e, min);
        }
        return;
    }
    if (to == TY_BOOL) { /* a number is true when it is not zero */
        if (min > P_EQ) put(g, "(");
        expr(g, e, P_EQ + 1);
        put(g, from == TY_REAL ? " != 0.0" : from == TY_CHAR && (JS || PY) ? " != \"\\0\"" : from == TY_CHAR ? " != '\\0'" : " != 0");
        if (min > P_EQ) put(g, ")");
        return;
    }
    if (to == TY_STRING || from == TY_STRING) { /* handled by who joins texts */
        expr(g, e, min);
        return;
    }
    /* through integers: bool -> int, char -> int, real -> int, and back */
    bool to_real = to == TY_REAL, from_real = from == TY_REAL;
    if (from == TY_BOOL) {
        if (CPP) expr(g, e, min);
        else if (JAVA) {
            put(g, "(");
            expr(g, e, P_OR + 1);
            put(g, to_real ? " ? 1.0 : 0.0)" : " ? 1 : 0)");
        } else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            put(g, to_real ? " as i32 as f64" : " as i32");
            if (min > P_UNARY) put(g, ")");
        } else {
            put(g, PY ? "int(" : "Number(");
            expr(g, e, P_LOWEST);
            put(g, ")");
        }
        return;
    }
    if (from == TY_CHAR && (to == TY_INT || to_real)) {
        if (CPP || JAVA) expr(g, e, min);
        else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            put(g, to_real ? " as i32 as f64" : " as i32");
            if (min > P_UNARY) put(g, ")");
        } else if (PY) {
            put(g, "ord(");
            expr(g, e, P_LOWEST);
            put(g, ")");
        } else {
            expr(g, e, P_POSTFIX);
            put(g, ".charCodeAt(0)");
        }
        return;
    }
    if (to == TY_CHAR) {
        if (CPP || JAVA) {
            if (min > P_UNARY) put(g, "(");
            put(g, CPP ? "(char)" : "(char) ");
            expr(g, e, P_UNARY);
            if (min > P_UNARY) put(g, ")");
        } else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            put(g, " as u8 as char");
            if (min > P_UNARY) put(g, ")");
        } else {
            put(g, PY ? "chr(" : "String.fromCharCode(");
            value(g, e, &(Type){.kind = TY_INT}, P_LOWEST);
            put(g, ")");
        }
        return;
    }
    if (to_real && !from_real) {
        long long n;
        if (RUST && e->kind == EX_INT && const_int(e, &n)) {
            putf(g, "%lld.0", n);
        } else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            put(g, " as f64");
            if (min > P_UNARY) put(g, ")");
        } else {
            expr(g, e, min);
        }
        return;
    }
    if (from_real && to == TY_INT) { /* the decimals are dropped */
        if (CPP || JAVA) {
            if (min > P_UNARY) put(g, "(");
            put(g, CPP ? "(int)" : "(int) ");
            expr(g, e, P_UNARY);
            if (min > P_UNARY) put(g, ")");
        } else if (RUST) {
            if (min > P_UNARY) put(g, "(");
            expr(g, e, P_UNARY);
            put(g, " as i32");
            if (min > P_UNARY) put(g, ")");
        } else {
            put(g, PY ? "int(" : "Math.trunc(");
            expr(g, e, P_LOWEST);
            put(g, ")");
        }
        return;
    }
    expr(g, e, min);
}

/* ---- expressions ---- */

static bool is_text_op(const Expr *e) {
    return e->kind == EX_BINARY && (e->base->type->kind == TY_STRING || e->right->type->kind == TY_STRING);
}

static bool is_comparison(TokenKind op) {
    return op == TOK_EQ || op == TOK_NE || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
}

static int binary_level(TokenKind op) {
    switch (op) {
        case TOK_OR: return P_OR;
        case TOK_AND: return P_AND;
        case TOK_PIPE: return P_BITOR;
        case TOK_CARET: return P_BITXOR;
        case TOK_AMP: return P_BITAND;
        case TOK_EQ: case TOK_NE: return P_EQ;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return P_REL;
        case TOK_SHL: case TOK_SHR: return P_SHIFT;
        case TOK_PLUS: case TOK_MINUS: case TOK_CONCAT: return P_ADD;
        default: return P_MUL;
    }
}

/* How tightly what will be written for this expression holds together. */
static int level_of(const Gen *g, const Expr *e) {
    switch (e->kind) {
        case EX_UNARY:
            if (e->op == TOK_AT && e->base->symbol && e->base->symbol->by_reference && e->base->kind == EX_NAME)
                return g->lang == LANG_CPP ? P_UNARY : g->lang == LANG_RUST ? P_POSTFIX : level_of(g, e->base);
            if (e->op == TOK_AT) return level_of(g, e->base);
            if (e->op == TOK_STAR && g->lang == LANG_TS) return P_POSTFIX;
            if ((e->op == TOK_STAR || e->op == TOK_AMP) && refs(g)) return level_of(g, e->base);
            if (e->op == TOK_AMP && g->lang == LANG_RUST) return P_POSTFIX;
            if (e->op == TOK_NOT && g->lang == LANG_PYTHON) return P_AND;
            return P_UNARY;
        case EX_BINARY:
            if (is_text_op(e)) {
                if (e->type->kind == TY_STRING) return g->lang == LANG_RUST ? P_POSTFIX : P_ADD;
                if (g->lang == LANG_JAVA) return e->op == TOK_EQ ? P_POSTFIX : e->op == TOK_NE ? P_UNARY : P_REL;
                return binary_level(e->op);
            }
            if (e->op == TOK_SLASH && e->type->kind != TY_REAL && (g->lang == LANG_PYTHON || g->lang == LANG_JS || g->lang == LANG_TS))
                return P_POSTFIX;
            if (e->op == TOK_MOD && g->lang == LANG_PYTHON) return P_POSTFIX;
            return binary_level(e->op);
        case EX_INDEX: case EX_APPLY:
            if (!e->is_call && e->base->type->kind == TY_POINTER && g->lang == LANG_RUST) return P_UNARY;
            return P_POSTFIX;
        default: return P_POSTFIX;
    }
}

/* Mixes that are legal but that nobody reads right get parentheses. */
static bool wants_parens(const Gen *g, const Expr *parent, const Expr *child) {
    if (child->kind == EX_UNARY) return child->op == TOK_NOT && binary_level(parent->op) >= P_BITOR;
    if (child->kind != EX_BINARY || level_of(g, child) == P_POSTFIX) return false;
    int outer = binary_level(parent->op), inner = binary_level(child->op);
    if (outer == inner) return outer == P_EQ || outer == P_REL;
    if (outer == P_OR) return inner == P_AND;
    return (outer >= P_BITOR && outer <= P_BITAND) || outer == P_SHIFT || ((outer == P_EQ || outer == P_REL) && (inner == P_REL || inner == P_EQ));
}

static void operand(Gen *g, const Expr *parent, const Expr *child, const Type *want, int min) {
    value(g, child, want, wants_parens(g, parent, child) ? P_POSTFIX : min);
}

static const char *state_text(Gen *g, const Symbol *sym) {
    const Class *klass = sym->type->states;
    bool inside = g->function && g->function->owner == klass;
    if (RUST) return arena_printf(g->arena, "%s_%s", class_name(g, klass), sym->name);
    if ((CPP || JAVA) && inside) return safe(g, sym->name);
    return arena_printf(g->arena, CPP ? "%s::%s" : "%s.%s", class_name(g, klass), safe(g, sym->name));
}

static void name(Gen *g, const Expr *e) {
    const Symbol *sym = e->symbol;
    switch (sym->kind) {
        case SYM_STATE: put(g, state_text(g, sym)); return;
        case SYM_NOW: put(g, RUST ? "(*SIM).now" : "sim.now"); return;
        case SYM_SENDER: put(g, RUST ? "sim_who((*SIM).sender)" : "sim.sender"); return;
        default: break;
    }
    if (is_self(e)) put(g, CPP ? "(*this)" : self_name(g));
    else put(g, variable_name(g, sym));
}

static void field(Gen *g, const Expr *e) {
    const Expr *base = e->base;
    const char *member = safe(g, e->symbol->c_name);
    if (is_self(base)) {
        if ((CPP || JAVA) && !hidden_by_local(g, e->symbol->c_name)) put(g, member);
        else putf(g, CPP ? "this->%s" : "%s.%s", CPP ? member : self_name(g), member);
        return;
    }
    bool pointer = base->type->kind == TY_POINTER;
    const Expr *object = base->kind == EX_UNARY && base->op == TOK_STAR ? base->base : base;
    bool through = pointer || object != base;
    if (through && CPP) {
        expr(g, object, P_POSTFIX);
        put(g, "->");
    } else if (through && RUST) {
        put(g, "(*");
        expr(g, object, P_UNARY);
        put(g, ").");
    } else {
        expr(g, object, P_POSTFIX);
        put(g, through && TS ? "!." : ".");
    }
    put(g, member);
}

static const Symbol *indexed_symbol(const Expr *base, int *skipped) {
    *skipped = 0;
    while ((base->kind == EX_INDEX || base->kind == EX_APPLY) && !base->is_call && base->base->type->kind == TY_ARRAY) {
        *skipped += base->args.count;
        base = base->base;
    }
    return base->kind == EX_NAME ? base->symbol : NULL;
}

static void one_index(Gen *g, const Expr *index, long long lo, const char *saved) {
    long long n;
    put(g, "[");
    if (saved) {
        if (RUST) put(g, "(");
        expr(g, index, P_ADD);
        putf(g, " - %s", saved);
        if (RUST) put(g, ") as usize");
    } else if (RUST && !(const_int(index, &n) && index->kind != EX_NAME)) {
        shifted(g, index, -lo, P_UNARY);
        put(g, " as usize");
    } else {
        shifted(g, index, -lo, P_LOWEST);
    }
    put(g, "]");
}

static void index_expr(Gen *g, const Expr *e) {
    const Type *type = e->base->type;
    if (type->kind == TY_POINTER) { /* C++ and Rust only */
        if (RUST) {
            put(g, "*");
            expr(g, e->base, P_POSTFIX);
            put(g, ".add(");
            shifted(g, e->args.items[0], -g->config->index_base, P_UNARY);
            put(g, " as usize)");
        } else {
            expr(g, e->base, P_POSTFIX);
            one_index(g, e->args.items[0], g->config->index_base, NULL);
        }
        return;
    }
    expr(g, e->base, P_POSTFIX);
    int skipped = 0;
    const Symbol *sym = indexed_symbol(e->base, &skipped);
    for (int i = 0; i < e->args.count; i++) {
        const Dim *dim = &type->dims.items[i];
        long long lo;
        if (constant_lo(g, dim, &lo)) one_index(g, e->args.items[i], lo, NULL);
        else one_index(g, e->args.items[i], 0, sym ? saved_lo(g, sym, skipped + i) : "0");
    }
}

static void number(Gen *g, const Expr *e) {
    const char *text = e->text;
    long long n;
    if (e->kind == EX_INT) {
        bool plain = text[0] != '0' || !text[1] || text[1] == 'x' || text[1] == 'X';
        if (!plain && const_int(e, &n)) putf(g, "%lld", n); /* octal and binary are not written the same everywhere */
        else put(g, text);
        return;
    }
    size_t length = strlen(text);
    put(g, text);
    if (length && text[length - 1] == '.') put(g, "0");
}

static void unary(Gen *g, const Expr *e) {
    const Expr *inner = e->base;
    switch (e->op) {
        case TOK_AT:
            if (CPP && inner->symbol && inner->symbol->by_reference) {
                put(g, "&");
                expr(g, inner, P_UNARY);
            } else if (RUST && inner->symbol && inner->symbol->by_reference && inner->kind == EX_NAME) {
                put(g, "std::ptr::addr_of_mut!(");
                expr(g, inner, P_LOWEST);
                put(g, ")");
            } else {
                expr(g, inner, P_LOWEST);
            }
            return;
        case TOK_AMP:
            if (CPP) {
                put(g, "&");
                expr(g, inner, P_UNARY);
            } else if (RUST) {
                put(g, "std::ptr::addr_of_mut!(");
                expr(g, inner, P_LOWEST);
                put(g, ")");
            } else {
                expr(g, inner, P_LOWEST);
            }
            return;
        case TOK_STAR:
            if (TS) { /* it is not null: the pseudocode does not ask */
                expr(g, inner, P_POSTFIX);
                put(g, "!");
            } else if (refs(g)) {
                expr(g, inner, P_LOWEST);
            } else {
                put(g, "*");
                expr(g, inner, P_UNARY);
            }
            return;
        case TOK_NOT:
            put(g, PY ? "not " : "!");
            value(g, inner, &(Type){.kind = TY_BOOL}, PY ? P_AND + 1 : P_UNARY);
            return;
        case TOK_TILDE:
            put(g, RUST ? "!" : "~");
            value(g, inner, &(Type){.kind = TY_INT}, P_UNARY);
            return;
        default: break;
    }
    put(g, e->op == TOK_MINUS ? "-" : "+");
    bool sign = inner->kind == EX_UNARY && (inner->op == TOK_MINUS || inner->op == TOK_PLUS);
    const Type *want = e->type->kind == TY_REAL ? e->type : &(Type){.kind = TY_INT};
    value(g, inner, want, sign ? P_POSTFIX : P_UNARY);
}

/* A part of a text that is being put together. */
static void as_text(Gen *g, const Expr *e, int min) {
    TypeKind kind = e->type->kind;
    if (kind == TY_STRING) {
        expr(g, e, min);
    } else if (kind == TY_REAL) {
        g->needs |= N_TEXT;
        put(g, "text(");
        expr(g, e, P_LOWEST);
        put(g, ")");
    } else if (kind == TY_CHAR && CPP) {
        put(g, "std::string(1, ");
        expr(g, e, P_LOWEST);
        put(g, ")");
    } else if (PY || CPP) {
        put(g, PY ? "str(" : "std::to_string(");
        value(g, e, kind == TY_BOOL ? &(Type){.kind = TY_INT} : e->type, P_LOWEST);
        put(g, ")");
    } else {
        value(g, e, kind == TY_BOOL ? &(Type){.kind = TY_INT} : e->type, min);
    }
}

static const char *operator_text(const Gen *g, TokenKind op) {
    bool py = g->lang == LANG_PYTHON, js = g->lang == LANG_JS || g->lang == LANG_TS;
    switch (op) {
        case TOK_OR: return py ? "or" : "||";
        case TOK_AND: return py ? "and" : "&&";
        case TOK_PIPE: return "|";
        case TOK_CARET: return "^";
        case TOK_AMP: return "&";
        case TOK_EQ: return js ? "===" : "==";
        case TOK_NE: return js ? "!==" : "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_PLUS: case TOK_CONCAT: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: case TOK_RDIV: return "/";
        default: return "%";
    }
}

static void text_op(Gen *g, const Expr *e) {
    const Expr *a = e->base, *b = e->right;
    if (e->type->kind == TY_STRING) { /* joined */
        if (RUST) {
            put(g, "format!(\"{}{}\", ");
            as_text(g, a, P_LOWEST);
            put(g, ", ");
            as_text(g, b, P_LOWEST);
            put(g, ")");
        } else {
            as_text(g, a, P_ADD);
            put(g, " + ");
            as_text(g, b, P_ADD + 1);
        }
        return;
    }
    if (JAVA) {
        if (e->op == TOK_NE) put(g, "!");
        expr(g, a, P_POSTFIX);
        put(g, e->op == TOK_EQ || e->op == TOK_NE ? ".equals(" : ".compareTo(");
        expr(g, b, P_LOWEST);
        put(g, ")");
        if (e->op != TOK_EQ && e->op != TOK_NE) putf(g, " %s 0", operator_text(g, e->op));
        return;
    }
    int level = binary_level(e->op);
    expr(g, a, RUST && a->kind != EX_STRING ? P_POSTFIX : level + 1);
    if (RUST && a->kind != EX_STRING) put(g, ".as_str()");
    putf(g, " %s ", operator_text(g, e->op));
    expr(g, b, RUST && b->kind != EX_STRING ? P_POSTFIX : level + 1);
    if (RUST && b->kind != EX_STRING) put(g, ".as_str()");
}

static void binary(Gen *g, const Expr *e) {
    const Expr *a = e->base, *b = e->right;
    TokenKind op = e->op;
    int level = binary_level(op);
    Type whole = {.kind = TY_INT}, real = {.kind = TY_REAL}, truth = {.kind = TY_BOOL};
    if (is_text_op(e)) {
        text_op(g, e);
        return;
    }
    if (op == TOK_AND || op == TOK_OR) {
        operand(g, e, a, &truth, level);
        putf(g, " %s ", operator_text(g, op));
        operand(g, e, b, &truth, level + 1);
        return;
    }
    if (is_comparison(op)) {
        TypeKind ka = a->type->kind, kb = b->type->kind;
        const Type *common = ka == TY_REAL || kb == TY_REAL ? &real : ka == kb ? a->type : &whole;
        if (ka == TY_POINTER || kb == TY_POINTER) {
            const Expr *pointer = ka == TY_POINTER ? a : b, *other = pointer == a ? b : a;
            bool null = is_zero_literal(other);
            if (PY && null) {
                expr(g, pointer, level + 1);
                put(g, op == TOK_EQ ? " is None" : " is not None");
                return;
            }
            if (RUST && null) {
                if (op != TOK_EQ) put(g, "!");
                expr(g, pointer, P_POSTFIX);
                put(g, ".is_null()");
                return;
            }
            common = pointer->type;
        }
        operand(g, e, a, common, level + 1);
        putf(g, " %s ", operator_text(g, op));
        operand(g, e, b, common, level + 1);
        return;
    }
    if (op == TOK_RDIV) {
        if (a->type->kind != TY_REAL && b->type->kind != TY_REAL && (CPP || JAVA)) {
            put(g, CPP ? "(double)" : "(double) ");
            value(g, a, &whole, P_UNARY);
        } else {
            operand(g, e, a, &real, level);
        }
        put(g, " / ");
        operand(g, e, b, RUST || !(CPP || JAVA) ? &real : b->type, level + 1);
        return;
    }
    bool is_real = e->type->kind == TY_REAL;
    bool pointer = e->type->kind == TY_POINTER; /* p + n */
    const Type *want = is_real ? &real : &whole;
    if (pointer && RUST) {
        const Expr *p = a->type->kind == TY_POINTER ? a : b, *n = p == a ? b : a;
        expr(g, p, P_POSTFIX);
        put(g, op == TOK_MINUS ? ".sub(" : ".add(");
        value(g, n, &whole, P_UNARY);
        put(g, " as usize)");
        return;
    }
    if (op == TOK_SLASH && !is_real && (PY || JS)) { /* the quotient without the remainder */
        put(g, PY ? "int(" : "Math.trunc(");
        value(g, a, &whole, P_MUL);
        put(g, " / ");
        value(g, b, &whole, P_MUL + 1);
        put(g, ")");
        return;
    }
    if (op == TOK_MOD && PY) { /* in Python % follows the sign of the divisor */
        long long n;
        bool positive = const_int(b, &n) && n > 0 && a->kind == EX_NAME && a->symbol && a->symbol->indexed;
        if (!positive) {
            g->needs |= N_MOD;
            put(g, "mod(");
            value(g, a, &whole, P_LOWEST);
            put(g, ", ");
            value(g, b, &whole, P_LOWEST);
            put(g, ")");
            return;
        }
    }
    operand(g, e, a, pointer ? a->type : want, level);
    putf(g, " %s ", operator_text(g, op));
    operand(g, e, b, pointer ? b->type : want, level + 1);
}

/* ---- calls ---- */

/* The variable that an argument by reference stands for: of &x and @x, x; of
 * @p when p is a pointer, what it points to. */
static const Expr *place_of(Gen *g, const Expr *arg) {
    if (arg->kind != EX_UNARY || (arg->op != TOK_AMP && arg->op != TOK_AT)) return arg;
    const Expr *inner = arg->base;
    bool pointer = arg->op == TOK_AT && inner->type->kind == TY_POINTER && !(inner->symbol && inner->symbol->by_reference && inner->kind == EX_NAME);
    if (!pointer) return inner;
    Expr *star = ARENA_NEW(g->arena, Expr);
    star->kind = EX_UNARY;
    star->op = TOK_STAR;
    star->loc = arg->loc;
    star->base = (Expr *)inner;
    star->type = inner->type->base;
    return star;
}

static bool is_reference_here(const Gen *g, const Expr *e) {
    if (is_self(e)) return true;
    return e->kind == EX_NAME && e->symbol && e->symbol->kind == SYM_PARAM && !e->is_call &&
           (e->symbol->by_reference || e->symbol->type->kind == TY_ARRAY) && g->function;
}

static void argument(Gen *g, const Symbol *callee, const Symbol *param, const Expr *arg, bool shared_with_changed) {
    if (!param) {
        expr(g, arg, P_LOWEST);
        return;
    }
    if (param->type->kind == TY_ARRAY) {
        bool changed = param_written(g, callee->function, param);
        if (RUST && !is_reference_here(g, arg)) put(g, changed ? "&mut " : "&");
        expr(g, arg, RUST ? P_UNARY : P_LOWEST);
        if (RUST && !changed && shared_with_changed) put(g, ".clone()"); /* Rust: nobody reads what another changes */
        return;
    }
    if (param->by_reference && (CPP || is_object(param->type))) {
        const Expr *place = place_of(g, arg);
        if (RUST && place->kind == EX_UNARY && place->op == TOK_STAR) {
            put(g, "&mut ");
            expr(g, place, P_UNARY);
        } else if (RUST && !is_reference_here(g, place)) {
            put(g, "&mut ");
            expr(g, place, P_UNARY);
        } else {
            expr(g, place, P_LOWEST);
        }
        return;
    }
    if (param->by_reference) { /* given back: only its value goes in */
        value(g, place_of(g, arg), param->type, P_LOWEST);
        return;
    }
    value(g, arg, param->type, P_LOWEST);
}

static void arguments(Gen *g, const Expr *e, int first) {
    const Symbol *callee = e->symbol;
    bool any = false;
    for (int i = first; i < e->args.count; i++) {
        const Symbol *param = callee->params ? callee->params[i] : NULL;
        if (param && given_back(g, param) && param->mode == MODE_OUT) continue;
        if (any) put(g, ", ");
        any = true;
        bool shared = false; /* the same array given to a parameter that changes it */
        for (int k = 0; RUST && param && param->type->kind == TY_ARRAY && callee->function && k < e->args.count; k++) {
            const Symbol *other = callee->params[k];
            shared |= k != i && other->type->kind == TY_ARRAY && root_of(e->args.items[k]) == root_of(e->args.items[i]) &&
                      root_of(e->args.items[i]) && param_written(g, callee->function, other);
        }
        argument(g, callee, param, e->args.items[i], shared);
    }
}

/* Rust: x.sqrt(); a number alone does not say of which kind it is. */
static void rust_receiver(Gen *g, const Expr *a, const Type *want) {
    const Expr *plain = a->kind == EX_UNARY && (a->op == TOK_MINUS || a->op == TOK_PLUS) ? a->base : a;
    if (plain->kind == EX_INT || plain->kind == EX_FLOAT) {
        put(g, "(");
        value(g, a, want, P_UNARY);
        put(g, want->kind == TY_REAL ? " as f64)" : " as i32)");
    } else {
        value(g, a, want, P_POSTFIX);
    }
}

static void builtin(Gen *g, const Expr *e) {
    const char *name = e->symbol->builtin;
    const Expr *a = e->args.count ? e->args.items[0] : NULL, *b = e->args.count > 1 ? e->args.items[1] : NULL;
    Type whole = {.kind = TY_INT}, real = {.kind = TY_REAL};
    if (strcmp(name, "random") == 0 || strcmp(name, "random_below") == 0) {
        g->needs |= N_RANDOM;
        bool below = name[6] == '_';
        put(g, "random(");
        if (below) put(g, "0, ");
        value(g, a, &whole, below ? P_ADD : P_LOWEST);
        if (below) put(g, " - 1");
        else {
            put(g, ", ");
            value(g, b, &whole, P_LOWEST);
        }
        put(g, ")");
        return;
    }
    if (strcmp(name, "length") == 0) {
        if (PY) {
            put(g, "len(");
            expr(g, a, P_LOWEST);
            put(g, ")");
        } else if (CPP) {
            put(g, "(int)");
            expr(g, a, P_POSTFIX);
            put(g, ".size()");
        } else {
            expr(g, a, P_POSTFIX);
            put(g, RUST ? ".chars().count() as i32" : JAVA ? ".length()" : ".length");
        }
        return;
    }
    if (strcmp(name, "upper") == 0 || strcmp(name, "lower") == 0) {
        bool up = name[0] == 'u';
        if (CPP) {
            g->needs |= N_CASE;
            put(g, up ? "upper(" : "lower(");
            expr(g, a, P_LOWEST);
            put(g, ")");
        } else {
            expr(g, a, P_POSTFIX);
            put(g, PY ? (up ? ".upper()" : ".lower()") : RUST ? (up ? ".to_uppercase()" : ".to_lowercase()")
                      : (up ? ".toUpperCase()" : ".toLowerCase()"));
        }
        return;
    }
    if (strcmp(name, "substring") == 0) { /* from one position to another, both included */
        const Expr *from = e->args.items[1], *to = e->args.items[2];
        long long base = g->config->index_base;
        if (PY) {
            expr(g, a, P_POSTFIX);
            put(g, "[");
            shifted(g, from, -base, P_LOWEST);
            put(g, ":");
            shifted(g, to, 1 - base, P_LOWEST);
            put(g, "]");
        } else if (CPP || RUST) {
            g->needs |= N_CASE;
            put(g, "substring(");
            if (RUST) put(g, "&");
            expr(g, a, RUST ? P_UNARY : P_LOWEST);
            put(g, ", ");
            shifted(g, from, -base, P_LOWEST);
            put(g, ", ");
            shifted(g, to, 1 - base, P_LOWEST);
            put(g, ")");
        } else {
            expr(g, a, P_POSTFIX);
            put(g, ".substring(");
            shifted(g, from, -base, P_LOWEST);
            put(g, ", ");
            shifted(g, to, 1 - base, P_LOWEST);
            put(g, ")");
        }
        return;
    }
    if (strcmp(name, "concat") == 0) {
        if (RUST) {
            put(g, "format!(\"{}{}\", ");
            expr(g, a, P_LOWEST);
            put(g, ", ");
            expr(g, b, P_LOWEST);
            put(g, ")");
        } else {
            put(g, "(");
            expr(g, a, P_ADD);
            put(g, " + ");
            expr(g, b, P_ADD + 1);
            put(g, ")");
        }
        return;
    }
    bool whole_abs = strcmp(name, "abs") == 0 && e->type->kind == TY_INT;
    if (strcmp(name, "trunc") == 0 || strcmp(name, "round") == 0) {
        bool round = name[0] == 'r';
        if (CPP) {
            g->needs |= N_MATH;
            putf(g, "(int)std::%s(", name);
            value(g, a, &real, P_LOWEST);
            put(g, ")");
        } else if (JAVA) {
            put(g, round ? "(int) Math.round(" : "(int) (");
            value(g, a, &real, P_LOWEST);
            put(g, ")");
        } else if (RUST) {
            rust_receiver(g, a, &real);
            putf(g, ".%s() as i32", name);
        } else if (PY) {
            g->needs |= round ? N_MATH : 0;
            put(g, round ? "math.floor(" : "int(");
            value(g, a, &real, round ? P_ADD : P_LOWEST);
            put(g, round ? " + 0.5)" : ")");
        } else {
            putf(g, "Math.%s(", name);
            value(g, a, &real, P_LOWEST);
            put(g, ")");
        }
        return;
    }
    if (strcmp(name, "fabs") == 0) name = "abs";
    if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) { /* of two values of the same kind */
        const Type *kind = e->type->kind == TY_REAL ? &real : &whole;
        if (RUST) {
            rust_receiver(g, a, kind);
            putf(g, ".%s(", name);
        } else {
            putf(g, PY ? "%s(" : CPP ? "std::%s(" : "Math.%s(", name);
            if (CPP) g->needs |= N_MATH;
            value(g, a, kind, P_LOWEST);
            put(g, ", ");
        }
        value(g, b, kind, P_LOWEST);
        put(g, ")");
        return;
    }
    const Type *want = whole_abs ? &whole : &real;
    if (RUST) { /* x.sqrt() */
        static const char *const NAMES[][2] = {{"log", "ln"}, {"pow", "powf"}};
        for (int i = 0; i < 2; i++)
            if (strcmp(name, NAMES[i][0]) == 0) name = NAMES[i][1];
        rust_receiver(g, a, want);
        putf(g, ".%s(", name);
        if (b) value(g, b, &real, P_LOWEST);
        put(g, ")");
        return;
    }
    if (PY && strcmp(name, "abs") == 0) put(g, "abs(");
    else if (PY) {
        g->needs |= N_MATH;
        putf(g, "math.%s(", name);
    } else if (CPP) {
        g->needs |= N_MATH;
        putf(g, "std::%s(", name);
    } else {
        putf(g, "Math.%s(", name);
    }
    value(g, a, want, P_LOWEST);
    if (b) {
        put(g, ", ");
        value(g, b, &real, P_LOWEST);
    }
    put(g, ")");
}

/* The call as the language writes it; what it gives back is dealt with by
 * the statement it is in. */
static void call_text(Gen *g, const Expr *e) {
    const Symbol *callee = e->symbol;
    if (callee->builtin) {
        builtin(g, e);
        return;
    }
    const Function *f = callee->function;
    if (!f) { /* not in the file */
        putf(g, "%s(", safe(g, callee->c_name));
        arguments(g, e, 0);
        put(g, ")");
        return;
    }
    if (!e->is_method || !f->owner) {
        putf(g, "%s(", function_name(g, f));
        arguments(g, e, 0);
        put(g, ")");
        return;
    }
    const Expr *receiver = e->args.items[0];
    const char *method = function_name(g, f);
    if (e->is_static) {
        if (CPP) putf(g, "%s::%s(", class_name(g, f->owner), method);
        else if (PY) putf(g, "%s.%s(self%s", class_name(g, f->owner), method, e->args.count > 1 ? ", " : "");
        else putf(g, "super.%s(", method);
    } else if (is_self(receiver)) {
        if (CPP || JAVA) putf(g, "%s(", method);
        else putf(g, "%s.%s(", self_name(g), method);
    } else {
        const Expr *object = receiver->kind == EX_UNARY && receiver->op == TOK_STAR ? receiver->base : receiver;
        bool through = object != receiver || receiver->type->kind == TY_POINTER;
        if (through && RUST) {
            put(g, "(*");
            expr(g, object, P_UNARY);
            put(g, ")");
        } else {
            expr(g, object, P_POSTFIX);
        }
        putf(g, through && CPP ? "->%s(" : through && TS ? "!.%s(" : ".%s(", method);
    }
    arguments(g, e, 1);
    put(g, ")");
}

static const char *temp_for(const Gen *g, const Expr *e) {
    for (int i = 0; i < g->temp_count; i++)
        if (g->temps[i].call == e) return g->temps[i].name;
    return NULL;
}

static void call(Gen *g, const Expr *e) {
    const char *temp = temp_for(g, e);
    if (!temp) {
        call_text(g, e);
        return;
    }
    /* done before the statement: here is what it gave */
    if (JAVA) putf(g, "%s.result()", temp);
    else if (RUST) putf(g, "%s.0", temp);
    else put(g, temp);
}

static void quoted(Gen *g, const Expr *e) {
    bool one = e->kind == EX_CHAR;
    char quote = one && (CPP || JAVA || RUST) ? '\'' : '"';
    putf(g, "%c", quote);
    for (const char *c = e->text; *c; c++) {
        if (*c == '"' && quote == '"' && (c == e->text || c[-1] != '\\')) put(g, "\\\"");
        else if (*c == '\'' && quote == '"' && c > e->text && c[-1] == '\\') g->out->data[g->out->length - 1] = '\'';
        else putf(g, "%c", *c);
    }
    putf(g, "%c", quote);
}

static void expr(Gen *g, const Expr *e, int min) {
    bool parens = level_of(g, e) < min;
    if (parens) put(g, "(");
    switch (e->kind) {
        case EX_INT: case EX_FLOAT: number(g, e); break;
        case EX_BOOL: put(g, PY ? (e->op == TOK_TRUE ? "True" : "False") : e->op == TOK_TRUE ? "true" : "false"); break;
        case EX_STRING: case EX_CHAR: quoted(g, e); break;
        case EX_NAME:
            if (e->is_call) call(g, e);
            else name(g, e);
            break;
        case EX_APPLY:
            if (e->is_call) call(g, e);
            else index_expr(g, e);
            break;
        case EX_INDEX: index_expr(g, e); break;
        case EX_FIELD: field(g, e); break;
        case EX_UNARY: unary(g, e); break;
        case EX_BINARY: binary(g, e); break;
    }
    if (parens) put(g, ")");
}

/* ---- statements ---- */

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

/* A first index that is not a constant is kept: what it is made of may change. */
static void save_bounds(Gen *g, const Symbol *sym) {
    for (int i = 0; sym->type->kind == TY_ARRAY && sym->indexed && i < sym->type->dims.count; i++) {
        const Dim *dim = &sym->type->dims.items[i];
        long long lo;
        if (constant_lo(g, dim, &lo)) continue;
        indent(g);
        putf(g, CPP ? "const int %s = " : JAVA ? "final int %s = " : RUST ? "let %s: i32 = " : PY ? "%s = " : "const %s = ",
             saved_lo(g, sym, i));
        expr(g, dim->lo, P_LOWEST);
        end(g);
    }
}

/* Java and Python do not let a variable of an inner block have the name of
 * another of the same subprogram: it gets its own. */
static void name_block_variable(Gen *g, const Symbol *sym) {
    bool taken = hidden_by_local(g, sym->c_name);
    for (int i = 0; i < g->seen_count; i++) taken |= strcmp(g->seen[i], sym->c_name) == 0;
    if (g->seen_count < 128) g->seen[g->seen_count++] = sym->c_name;
    if (!taken || !(JAVA || PY) || g->rename_count >= 64) return;
    g->renames[g->rename_count].symbol = sym;
    g->renames[g->rename_count].name = arena_printf(g->arena, "%s_%d", sym->c_name, g->rename_count + 2);
    g->rename_count++;
}

static void declare(Gen *g, const Symbol *sym, const char *storage) {
    const char *name = sym->is_global ? safe(g, sym->c_name) : variable_name(g, sym), *type = type_text(g, sym->type);
    indent(g);
    put(g, storage);
    if (CPP && sym->type->kind == TY_ARRAY) {
        putf(g, "auto %s = ", name);
        zero(g, sym->type, 0);
    } else if (CPP && sym->type->kind == TY_RECORD) {
        putf(g, "%s %s", type, name);
    } else if (CPP || JAVA) {
        putf(g, "%s%s%s = ", type, type[strlen(type) - 1] == '*' ? "" : " ", name);
        zero(g, sym->type, 0);
    } else if (RUST) {
        putf(g, "let mut %s: %s = ", name, type);
        zero(g, sym->type, 0);
    } else if (PY) {
        putf(g, "%s = ", name);
        zero(g, sym->type, 0);
    } else {
        putf(g, TS ? "let %s: %s = " : "let %s%s = ", name, TS ? type : "");
        zero(g, sym->type, 0);
    }
    end(g);
    save_bounds(g, sym);
}

static void block(Gen *g, const Stmt *body) {
    size_t before = g->out->length;
    open(g);
    size_t inside = g->out->length;
    statements(g, body);
    if (PY && g->out->length == inside) {
        indent(g);
        put(g, "pass\n");
    }
    close(g);
    (void)before;
}

static void condition(Gen *g, const Expr *e) {
    bool parens = CPP || JAVA || JS;
    if (parens) put(g, "(");
    value(g, e, &(Type){.kind = TY_BOOL}, P_LOWEST);
    if (parens) put(g, ")");
}

/* ---- values given back ---- */

static const char *result_record(Gen *g, const Symbol *callee) {
    const char *name = callee->function && callee->function->owner
                     ? arena_printf(g->arena, "%s_%s", class_name(g, callee->function->owner), callee->function->name) : callee->c_name;
    char *text = (char *)arena_printf(g->arena, "%sResult", name);
    if (text[0] >= 'a' && text[0] <= 'z') text[0] = (char)(text[0] - 'a' + 'A');
    return text;
}

/* The type of everything a subprogram gives: its result, and the parameters
 * that are given back. */
static const char *gives_type(Gen *g, const Symbol *callee) {
    int n = given_back_count(g, callee);
    bool has = has_result(callee);
    if (n == 0) return type_text(g, callee->type);
    if (n == 1 && !has) {
        for (int i = 0; i < callee->param_count; i++)
            if (given_back(g, callee->params[i])) return type_text(g, callee->params[i]->type);
    }
    if (JAVA) return result_record(g, callee);
    StrBuf text = {0};
    strbuf_puts(&text, RUST ? "(" : TS ? "[" : "tuple[");
    bool any = false;
    if (has) {
        strbuf_puts(&text, type_text(g, callee->type));
        any = true;
    }
    for (int i = 0; i < callee->param_count; i++) {
        if (!given_back(g, callee->params[i])) continue;
        strbuf_printf(&text, "%s%s", any ? ", " : "", type_text(g, callee->params[i]->type));
        any = true;
    }
    strbuf_puts(&text, RUST ? ")" : "]");
    const char *kept = arena_printf(g->arena, "%s", text.data);
    strbuf_free(&text);
    return kept;
}

/* return, with all that the caller gets. */
static void give(Gen *g, const Expr *explicit) {
    const Symbol *me = g->function->symbol;
    int n = given_back_count(g, me);
    bool has = has_result(me);
    int total = n + (has ? 1 : 0);
    indent(g);
    if (total == 0) {
        put(g, "return");
        end(g);
        return;
    }
    put(g, "return ");
    if (total > 1) {
        if (JAVA) putf(g, "new %s(", result_record(g, me));
        else if (RUST) put(g, "(");
        else if (JS) put(g, "[");
    }
    bool any = false;
    if (has) {
        if (explicit) value(g, explicit, me->type, P_LOWEST);
        else if (RUST && (me->type->kind == TY_STRING || me->type->kind == TY_RECORD) && total > 1)
            putf(g, "%s.clone()", safe(g, me->result->c_name));
        else put(g, me->result ? safe(g, me->result->c_name) : "0");
        any = true;
    }
    for (int i = 0; i < me->param_count; i++) {
        if (!given_back(g, me->params[i])) continue;
        putf(g, "%s%s", any ? ", " : "", safe(g, me->params[i]->c_name));
        if (RUST && me->params[i]->type->kind == TY_STRING) put(g, ".clone()");
        any = true;
    }
    if (total > 1) put(g, JAVA || RUST ? ")" : JS ? "]" : "");
    end(g);
}

/* A call whose callee gives values back: a statement of its own. With `into`
 * the result goes there; with `temp`, into a new variable of that name. */
static void call_giving(Gen *g, const Expr *e, const Expr *into, const char *temp) {
    const Symbol *callee = e->symbol;
    int n = given_back_count(g, callee);
    bool has = has_result(callee);
    int total = n + (has ? 1 : 0);
    const Expr *targets[32];
    const char *names[32];
    int count = 0;
    for (int i = 0; i < callee->param_count && count < 32; i++) {
        if (!given_back(g, callee->params[i])) continue;
        names[count] = safe(g, callee->params[i]->c_name);
        targets[count++] = place_of(g, e->args.items[i]);
    }
    indent(g);
    if (total == 1) { /* a procedure with one such parameter */
        expr(g, targets[0], P_LOWEST);
        put(g, " = ");
        call_text(g, e);
        end(g);
        return;
    }
    if (JAVA || (RUST && temp)) { /* one value: its parts are taken after */
        const char *all = temp ? temp : arena_printf(g->arena, "t%d", ++g->serial);
        putf(g, JAVA ? "var %s = " : "let %s = ", all);
        call_text(g, e);
        end(g);
        if (into) {
            indent(g);
            expr(g, into, P_LOWEST);
            putf(g, JAVA ? " = %s.result()" : " = %s.0", all);
            end(g);
        }
        for (int i = 0; i < count; i++) {
            indent(g);
            expr(g, targets[i], P_LOWEST);
            if (JAVA) putf(g, " = %s.%s()", all, names[i]);
            else putf(g, " = %s.%d", all, i + (has ? 1 : 0));
            end(g);
        }
        return;
    }
    if (temp && JS) {
        putf(g, TS ? "let %s: %s;\n" : "let %s%s;\n", temp, TS ? type_text(g, callee->type) : "");
        indent(g);
    }
    put(g, RUST ? "(" : JS ? "[" : "");
    bool any = false;
    if (has) {
        if (temp) put(g, temp);
        else if (into) expr(g, into, P_LOWEST);
        else put(g, JS ? "" : "_");
        any = true;
    }
    for (int i = 0; i < count; i++) {
        put(g, any ? ", " : "");
        expr(g, targets[i], P_LOWEST);
        any = true;
    }
    put(g, RUST ? ") = " : JS ? "] = " : " = ");
    call_text(g, e);
    end(g);
}

static bool gives_back_call(const Gen *g, const Expr *e) {
    return e && e->is_call && e->symbol && e->symbol->function && gives_back(g) && given_back_count(g, e->symbol) > 0;
}

/* The calls inside e that give values back are done first, in the order in
 * which they would have been reached. */
static void hoist(Gen *g, const Expr *e, bool whole) {
    if (!e) return;
    hoist(g, e->base, true);
    hoist(g, e->right, true);
    for (int i = 0; i < e->args.count; i++) hoist(g, e->args.items[i], true);
    if (!whole || !gives_back_call(g, e) || g->temp_count >= 32) return;
    const char *temp = arena_printf(g->arena, "t%d", ++g->serial);
    call_giving(g, e, NULL, temp);
    g->temps[g->temp_count].call = e;
    g->temps[g->temp_count++].name = temp;
}

/* ---- each statement ---- */

static bool same_kind(const Type *a, const Type *b) { return a->kind == b->kind; }

static void assign(Gen *g, const Stmt *s) {
    const Expr *target = s->target, *v = s->value;
    if (gives_back_call(g, v) && same_kind(target->type, v->type) && !temp_for(g, v)) {
        g->out->length -= (size_t)g->indent * 4; /* it writes its own line */
        g->out->data[g->out->length] = '\0';
        call_giving(g, v, target, NULL);
        return;
    }
    /* an object that somebody else also holds is changed, not replaced */
    bool held = target->type->kind == TY_RECORD && refs(g) &&
                ((target->kind == EX_UNARY && target->op == TOK_STAR) ||
                 (target->kind == EX_NAME && target->symbol && target->symbol->by_reference));
    if (held) {
        note_copy(g, target->type->record);
        if (PY) {
            expr(g, target, P_POSTFIX);
            put(g, ".__dict__.update(copy.deepcopy(");
            expr(g, v, P_LOWEST);
            put(g, ").__dict__)");
        } else {
            expr(g, target, P_POSTFIX);
            put(g, ".set(");
            expr(g, v, P_LOWEST);
            put(g, ")");
        }
        end(g);
        return;
    }
    if (RUST && target->type->kind == TY_RECORD && target->kind == EX_NAME && target->symbol && target->symbol->by_reference &&
        !is_self(target))
        put(g, "*"); /* what the reference leads to */
    expr(g, target, P_LOWEST);
    put(g, " = ");
    value(g, v, target->type, P_LOWEST);
    end(g);
}

static bool mentions(const Expr *e, const Symbol *sym) {
    if (!e) return false;
    if (e->kind == EX_NAME && e->symbol == sym) return true;
    if (mentions(e->base, sym) || mentions(e->right, sym)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (mentions(e->args.items[i], sym)) return true;
    return false;
}

static bool has_call(const Expr *e) {
    if (!e) return false;
    if (e->is_call) return true;
    if (has_call(e->base) || has_call(e->right)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (has_call(e->args.items[i])) return true;
    return false;
}

/* Is the variable used anywhere but in the loops that count with it? */
static bool used_outside(const Stmt *s, const Symbol *sym, bool inside) {
    for (; s; s = s->next) {
        bool mine = s->kind == ST_FOR && s->target->kind == EX_NAME && s->target->symbol == sym;
        if (mine && (mentions(s->value, sym) || mentions(s->limit, sym) || mentions(s->step, sym)) && !inside) return true;
        if (!mine && !inside) {
            if (mentions(s->target, sym) || mentions(s->value, sym) || mentions(s->limit, sym) || mentions(s->step, sym)) return true;
            for (int i = 0; i < s->args.count; i++)
                if (mentions(s->args.items[i], sym)) return true;
        }
        if (used_outside(s->body, sym, inside || mine)) return true;
        for (const Stmt *o = s->otherwise; o; o = o->otherwise) {
            if (!inside && mentions(o->value, sym)) return true;
            if (used_outside(o->body, sym, inside)) return true;
        }
    }
    return false;
}

static bool names_written(const Gen *g, const Expr *e, const Stmt *body) {
    if (!e) return false;
    if (e->kind == EX_NAME && e->symbol && !e->is_call && writes(g, body, e->symbol, 0)) return true;
    if (names_written(g, e->base, body) || names_written(g, e->right, body)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (names_written(g, e->args.items[i], body)) return true;
    return false;
}

/* Can the loop be written as the language likes it, counting on its own? */
static bool counts_simply(const Gen *g, const Stmt *s, long long *step) {
    *step = 1;
    if (s->step && !const_int(s->step, step)) return false;
    if (*step == 0 || s->target->kind != EX_NAME || !s->target->symbol) return false;
    const Symbol *sym = s->target->symbol;
    if (sym->type->kind != TY_INT || sym->is_global || sym->kind != SYM_VAR) return false;
    if (s->value->type->kind == TY_REAL || s->limit->type->kind == TY_REAL) return false;
    if (g->lang == LANG_RUST && *step < -1) return false;
    if (has_call(s->limit) || writes(g, s->body, sym, 0) || names_written(g, s->limit, s->body)) return false;
    return g->function && !used_outside(g->function->body, sym, false);
}

static void for_loop(Gen *g, const Stmt *s) {
    long long step = 1;
    bool known = !s->step || const_int(s->step, &step);
    bool negative = known ? step < 0
                          : s->step->kind == EX_UNARY && s->step->op == TOK_MINUS && s->step->base->kind == EX_FLOAT;
    bool sign_known = known || negative || s->step->kind == EX_FLOAT;
    const Type *type = s->target->type;
    Type whole = {.kind = TY_INT};

    if ((PY || RUST) && counts_simply(g, s, &step)) {
        putf(g, "for %s in ", safe(g, s->target->symbol->c_name));
        if (PY) {
            put(g, "range(");
            value(g, s->value, &whole, P_LOWEST);
            put(g, ", ");
            shifted(g, s->limit, step > 0 ? 1 : -1, P_LOWEST);
            if (step != 1) putf(g, ", %lld", step);
            put(g, ")");
        } else if (step == -1) {
            put(g, "(");
            value(g, s->limit, &whole, P_ADD);
            put(g, "..=");
            value(g, s->value, &whole, P_ADD);
            put(g, ").rev()");
        } else {
            if (step != 1) put(g, "(");
            value(g, s->value, &whole, P_ADD);
            put(g, "..=");
            value(g, s->limit, &whole, P_ADD);
            if (step != 1) putf(g, ").step_by(%lld)", step);
        }
        block(g, s->body);
        if (!PY) put(g, "\n");
        return;
    }
    if (PY || RUST) { /* as a while: the variable keeps its meaning after the loop */
        expr(g, s->target, P_LOWEST);
        put(g, " = ");
        value(g, s->value, type, P_LOWEST);
        end(g);
        indent(g);
        put(g, "while ");
        if (!sign_known) {
            put(g, PY ? "(" : "if ");
            if (PY) {
                expr(g, s->target, P_REL + 1);
                put(g, " <= ");
                value(g, s->limit, type, P_REL + 1);
                put(g, " if ");
                expr(g, s->step, P_REL + 1);
                put(g, " > 0 else ");
            } else {
                expr(g, s->step, P_REL + 1);
                put(g, s->step->type->kind == TY_REAL ? " > 0.0 { " : " > 0 { ");
                expr(g, s->target, P_REL + 1);
                put(g, " <= ");
                value(g, s->limit, type, P_REL + 1);
                put(g, " } else { ");
            }
        }
        expr(g, s->target, P_REL + 1);
        put(g, !sign_known || negative ? " >= " : " <= ");
        value(g, s->limit, type, P_REL + 1);
        if (!sign_known) put(g, PY ? ")" : " }");
        open(g);
        statements(g, s->body);
        indent(g);
        expr(g, s->target, P_LOWEST);
        if (!s->step) {
            put(g, type->kind == TY_REAL ? " += 1.0" : " += 1");
        } else {
            put(g, " += ");
            value(g, s->step, type, P_LOWEST);
        }
        end(g);
        close(g);
        if (!PY) put(g, "\n");
        return;
    }
    put(g, "for (");
    expr(g, s->target, P_LOWEST);
    put(g, " = ");
    value(g, s->value, type, P_LOWEST);
    put(g, "; ");
    if (!sign_known) {
        expr(g, s->step, P_REL + 1);
        put(g, " > 0 ? ");
        expr(g, s->target, P_REL + 1);
        put(g, " <= ");
        value(g, s->limit, type, P_REL + 1);
        put(g, " : ");
    }
    expr(g, s->target, P_REL + 1);
    put(g, !sign_known || negative ? " >= " : " <= ");
    value(g, s->limit, type, P_REL + 1);
    put(g, "; ");
    if (known && (step == 1 || step == -1)) {
        expr(g, s->target, P_POSTFIX);
        put(g, step == 1 ? "++" : "--");
    } else if (known) {
        expr(g, s->target, P_LOWEST);
        putf(g, step > 0 ? " += %lld" : " -= %lld", step > 0 ? step : -step);
    } else {
        expr(g, s->target, P_LOWEST);
        put(g, " += ");
        value(g, s->step, type, P_LOWEST);
    }
    put(g, ")");
    block(g, s->body);
    put(g, "\n");
}

static const char *const TRUTH_WORDS[][2] = {{"1", "0"}, {"VERDADERO", "FALSO"}, {"VERDADEIRO", "FALSO"},
                                             {"verdadeiro", "falso"}, {"TRUE", "FALSE"}};

/* One value of a print, as the text it becomes. `joined`: numbers as short as
 * possible and truths as words, as the other pseudocodes print. */
static void printed(Gen *g, const Expr *e, bool joined, int min) {
    TypeKind kind = e->type->kind;
    Type whole = {.kind = TY_INT};
    if (kind == TY_BOOL && joined) {
        const char *yes = TRUTH_WORDS[g->config->dialect][0], *no = TRUTH_WORDS[g->config->dialect][1];
        put(g, "(");
        if (PY) {
            putf(g, "\"%s\" if ", yes);
            expr(g, e, P_OR + 1);
            putf(g, " else \"%s\"", no);
        } else if (RUST) {
            put(g, "if ");
            expr(g, e, P_LOWEST);
            putf(g, " { \"%s\" } else { \"%s\" }", yes, no);
        } else {
            expr(g, e, P_OR + 1);
            putf(g, " ? \"%s\" : \"%s\"", yes, no);
        }
        put(g, ")");
    } else if (kind == TY_BOOL) {
        value(g, e, &whole, min);
    } else if (kind == TY_REAL && joined) {
        g->needs |= N_TEXT;
        put(g, "text(");
        expr(g, e, P_LOWEST);
        put(g, ")");
    } else if (kind == TY_REAL && JS) {
        expr(g, e, P_POSTFIX);
        put(g, ".toFixed(6)");
    } else {
        expr(g, e, min);
    }
}

static void print(Gen *g, const Stmt *s) {
    int n = s->args.count;
    bool joined = s->joined, reals = false;
    for (int i = 0; i < n; i++) reals |= s->args.items[i]->type->kind == TY_REAL;
    const char *between = joined ? "" : " ";
    if (CPP) {
        if (reals && !joined) g->needs |= N_FIXED;
        put(g, "std::cout");
        for (int i = 0; i < n; i++) {
            if (i && !joined) put(g, " << \" \"");
            put(g, " << ");
            printed(g, s->args.items[i], joined, P_SHIFT + 1);
        }
        if (!s->same_line) put(g, " << \"\\n\"");
        if (n == 0 && s->same_line) put(g, " << \"\"");
    } else if (JAVA) {
        const Expr *only = n == 1 ? s->args.items[0] : NULL;
        if (n == 0) {
            put(g, s->same_line ? "System.out.print(\"\")" : "System.out.println()");
        } else if (joined || (only && only->type->kind != TY_REAL && only->type->kind != TY_BOOL)) {
            put(g, s->same_line ? "System.out.print(" : "System.out.println(");
            if (n > 1) put(g, "\"\"");
            for (int i = 0; i < n; i++) {
                if (n > 1) put(g, " + ");
                printed(g, s->args.items[i], joined, n > 1 ? P_ADD + 1 : P_LOWEST);
            }
            put(g, ")");
        } else {
            if (reals) g->needs |= N_LOCALE;
            put(g, "System.out.printf(\"");
            for (int i = 0; i < n; i++) {
                TypeKind kind = s->args.items[i]->type->kind;
                putf(g, "%s%s", i ? " " : "", kind == TY_REAL ? "%f" : kind == TY_CHAR ? "%c" : kind == TY_STRING ? "%s" : "%d");
            }
            put(g, s->same_line ? "\"" : "%n\"");
            for (int i = 0; i < n; i++) {
                put(g, ", ");
                printed(g, s->args.items[i], false, P_LOWEST);
            }
            put(g, ")");
        }
    } else if (RUST) {
        put(g, s->same_line ? "print!(\"" : "println!(\"");
        for (int i = 0; i < n; i++)
            putf(g, "%s%s", i ? between : "", s->args.items[i]->type->kind == TY_REAL && !joined ? "{:.6}" : "{}");
        put(g, "\"");
        for (int i = 0; i < n; i++) {
            put(g, ", ");
            printed(g, s->args.items[i], joined, P_LOWEST);
        }
        put(g, ")");
    } else if (PY) {
        put(g, "print(");
        if (reals && !joined) { /* "%f": six decimals, as the pseudocode prints */
            put(g, "\"");
            for (int i = 0; i < n; i++) {
                TypeKind kind = s->args.items[i]->type->kind;
                putf(g, "%s%s", i ? " " : "", kind == TY_REAL ? "%f" : kind == TY_INT || kind == TY_BOOL ? "%d" : "%s");
            }
            put(g, n == 1 ? "\" % " : "\" % (");
            for (int i = 0; i < n; i++) {
                if (i) put(g, ", ");
                printed(g, s->args.items[i], false, n == 1 ? P_MUL + 1 : P_LOWEST);
            }
            if (n > 1) put(g, ")");
        } else {
            for (int i = 0; i < n; i++) {
                if (i) put(g, ", ");
                printed(g, s->args.items[i], joined, P_LOWEST);
            }
            if (joined && n > 1) put(g, ", sep=\"\"");
        }
        if (s->same_line) put(g, n ? ", end=\"\"" : "end=\"\"");
        put(g, ")");
    } else {
        if (s->same_line) g->needs |= N_PROCESS;
        put(g, s->same_line ? "process.stdout.write(`" : "console.log(`");
        for (int i = 0; i < n; i++) {
            const Expr *e = s->args.items[i];
            if (i) put(g, between);
            if (e->kind == EX_STRING) {
                for (const char *c = e->text; *c; c++) {
                    if (*c == '`' || (*c == '$' && c[1] == '{')) put(g, "\\");
                    putf(g, "%c", *c);
                }
            } else {
                put(g, "${");
                printed(g, e, joined, P_LOWEST);
                put(g, "}");
            }
        }
        put(g, "`)");
    }
    end(g);
}

static void read(Gen *g, const Stmt *s) {
    g->needs |= N_INPUT;
    for (int i = 0; i < s->args.count; i++) {
        const Expr *e = s->args.items[i];
        TypeKind kind = e->type->kind;
        if (i) indent(g);
        if (CPP) {
            put(g, "read(");
            expr(g, e, P_LOWEST);
            putf(g, ", %d)", s->loc.line);
        } else {
            static const char *const WORDS[] = {"int", "real", "bool", "char", "text"};
            static const char *const CAMEL[] = {"Int", "Real", "Bool", "Char", "Text"};
            int which = kind == TY_REAL ? 1 : kind == TY_BOOL ? 2 : kind == TY_CHAR ? 3 : kind == TY_STRING ? 4 : 0;
            expr(g, e, P_LOWEST);
            putf(g, JAVA || JS ? " = read%s(%d)" : " = read_%s(%d)", JAVA || JS ? CAMEL[which] : WORDS[which], s->loc.line);
        }
        end(g);
    }
}

/* Whatever happens, the statements end with a return: nothing can follow. */
static bool ends_with_return(const Stmt *body) {
    while (body && body->next) body = body->next;
    if (!body) return false;
    if (body->kind == ST_RETURN) return true;
    if (body->kind == ST_BLOCK) return ends_with_return(body->body);
    if (body->kind != ST_IF) return false;
    const Stmt *branch = body;
    for (; branch; branch = branch->otherwise) {
        if (!ends_with_return(branch->body)) return false;
        if (branch->kind != ST_IF) return true; /* the else */
    }
    return false;
}

static void system_statement(Gen *g, const Stmt *s);

static void one_statement(Gen *g, const Stmt *s) {
    if (s->kind == ST_VAR) {
        for (const VarDecl *v = s->vars; v; v = v->next) {
            name_block_variable(g, v->symbol);
            declare(g, v->symbol, "");
        }
        return;
    }
    if (s->kind == ST_BLOCK && PY) {
        statements(g, s->body);
        return;
    }
    if (gives_back(g)) { /* what gives values back is called first */
        switch (s->kind) {
            case ST_ASSIGN:
                hoist(g, s->target, true);
                hoist(g, s->value, !same_kind(s->target->type, s->value->type));
                break;
            case ST_CALL: hoist(g, s->value, false); break;
            case ST_RETURN: case ST_IF: case ST_CHECK: hoist(g, s->value, true); break;
            case ST_FOR: hoist(g, s->value, true); break;
            case ST_PRINT: case ST_SEND:
                for (int i = 0; i < s->args.count; i++) hoist(g, s->args.items[i], true);
                if (s->kind == ST_SEND && s->value)
                    for (int i = 0; i < s->value->args.count; i++) hoist(g, s->value->args.items[i], true);
                break;
            default: break;
        }
    }
    indent(g);
    switch (s->kind) {
        case ST_ASSIGN: assign(g, s); break;
        case ST_CALL:
            if (gives_back_call(g, s->value)) {
                g->out->length -= (size_t)g->indent * 4;
                g->out->data[g->out->length] = '\0';
                call_giving(g, s->value, NULL, NULL);
            } else {
                expr(g, s->value, P_LOWEST);
                end(g);
            }
            break;
        case ST_IF:
            for (const Stmt *branch = s; branch; branch = branch->otherwise) {
                if (branch != s) {
                    if (g->map) map_lines(g, branch, line_count(g), line_count(g));
                    if (PY) indent(g);
                    put(g, PY ? (branch->kind == ST_IF ? "elif " : "else") : branch->kind == ST_IF ? " else if " : " else");
                } else {
                    put(g, "if ");
                }
                if (branch->kind == ST_IF) condition(g, branch->value);
                block(g, branch->body);
            }
            if (!PY) put(g, "\n");
            break;
        case ST_FOR: for_loop(g, s); break;
        case ST_WHILE:
            put(g, "while ");
            condition(g, s->value);
            block(g, s->body);
            if (!PY) put(g, "\n");
            break;
        case ST_REPEAT:
            if (PY || RUST) {
                put(g, PY ? "while True" : "loop");
                open(g);
                statements(g, s->body);
                indent(g);
                put(g, "if ");
                condition(g, s->value);
                put(g, PY ? ":\n" : " {\n");
                g->indent++;
                indent(g);
                put(g, "break");
                end(g);
                g->indent--;
                if (!PY) {
                    indent(g);
                    put(g, "}\n");
                }
                close(g);
                if (!PY) put(g, "\n");
            } else {
                put(g, "do");
                block(g, s->body);
                put(g, " while (!");
                value(g, s->value, &(Type){.kind = TY_BOOL}, P_POSTFIX);
                put(g, ");\n");
            }
            break;
        case ST_RETURN:
            g->out->length -= (size_t)g->indent * 4;
            g->out->data[g->out->length] = '\0';
            if (gives_back(g) && (given_back_count(g, g->function->symbol) || (!s->value && g->function->symbol->result))) {
                give(g, s->value);
            } else {
                indent(g);
                if (s->value) {
                    put(g, "return ");
                    value(g, s->value, g->function->symbol->type, P_LOWEST);
                } else if (g->function->symbol->result) {
                    putf(g, "return %s", safe(g, g->function->symbol->result->c_name));
                } else {
                    put(g, "return");
                }
                end(g);
            }
            break;
        case ST_PRINT: print(g, s); break;
        case ST_READ: read(g, s); break;
        case ST_BLOCK:
            put(g, "{\n");
            g->indent++;
            statements(g, s->body);
            g->indent--;
            indent(g);
            put(g, "}\n");
            break;
        case ST_TODO: put(g, PY ? "pass  # ...\n" : "// ...\n"); break;
        default: system_statement(g, s); break;
    }
}

static void statement(Gen *g, const Stmt *s) {
    int first = g->map ? line_count(g) : 0;
    g->temp_count = 0;
    one_statement(g, s);
    if (g->map) map_lines(g, s, first, line_count(g) - 1);
}

static void statements(Gen *g, const Stmt *s) {
    for (; s; s = s->next) statement(g, s);
}

/* ---- subprograms ---- */

static bool uses_pointers_in(const Type *type) {
    while (type && type->kind == TY_ARRAY) type = type->base;
    return type && type->kind == TY_POINTER;
}

static bool uses_pointers(const Program *program) {
    for (const VarDecl *v = program->globals; v; v = v->next)
        if (v->symbol && uses_pointers_in(v->symbol->type)) return true;
    for (const Record *r = program->records; r; r = r->next)
        for (const VarDecl *v = r->fields; v; v = v->next)
            if (v->symbol && uses_pointers_in(v->symbol->type)) return true;
    for (const Function *f = program->functions; f; f = f->next) {
        for (const VarDecl *v = f->params; v; v = v->next)
            if (v->symbol && uses_pointers_in(v->symbol->type)) return true;
        for (const VarDecl *v = f->locals; v; v = v->next)
            if (v->symbol && uses_pointers_in(v->symbol->type)) return true;
        if (f->symbol && uses_pointers_in(f->symbol->type)) return true;
    }
    return false;
}

static void parameters(Gen *g, const Function *f) {
    const Symbol *sym = f->symbol;
    bool any = false;
    if (f->owner && (PY || RUST)) {
        put(g, PY ? "self" : "&mut self");
        any = true;
    }
    for (int i = f->owner ? 1 : 0; i < sym->param_count; i++) {
        const Symbol *p = sym->params[i];
        if (given_back(g, p) && p->mode == MODE_OUT) continue;
        const char *name = safe(g, p->c_name), *type = type_text(g, p->type);
        if (any) put(g, ", ");
        any = true;
        bool array = p->type->kind == TY_ARRAY;
        if (CPP) {
            bool star = type[strlen(type) - 1] == '*';
            putf(g, "%s%s%s%s", type, star ? "" : " ", array || p->by_reference ? "&" : "", name);
        } else if (JAVA) {
            putf(g, "%s %s", type, name);
        } else if (RUST && array) {
            const char *element = array_of(g, scalar_type(g, p->type->base), p->type->dims.count - 1);
            putf(g, "%s: %s[%s]", name, param_written(g, f, p) ? "&mut " : "&", element);
        } else if (RUST) {
            putf(g, p->by_reference && is_object(p->type) ? "%s: &mut %s" : "mut %s: %s", name, type);
        } else if (IS(LANG_JS)) {
            put(g, name);
        } else {
            putf(g, "%s: %s", name, type);
        }
    }
}

/* Whether some interface the class implements asks for this method. */
static const Class *asked_by_interface(const Gen *g, const Function *f) {
    for (const Class *k = g->program->classes; k && f->owner; k = k->next) {
        if (!k->is_interface || k == f->owner || !derives_from(f->owner, k)) continue;
        for (const Function *m = g->program->functions; m; m = m->next)
            if (m->owner == k && strcmp(m->name, f->name) == 0) return k;
    }
    return NULL;
}

static bool replaces_one(const Gen *g, const Function *f) {
    for (const BaseName *b = f->owner ? f->owner->bases : NULL; b; b = b->next)
        if (b->klass && find_method(g->program, b->klass, f->name, false)) return true;
    return false;
}

/* The first line of a subprogram, up to where its body begins. */
static void header(Gen *g, const Function *f, bool declaration) {
    const Symbol *sym = f->symbol;
    const char *name = function_name(g, f);
    const char *gives = gives_back(g) ? gives_type(g, sym) : type_text(g, sym->type);
    bool nothing = strcmp(gives, type_text(g, &(Type){.kind = TY_VOID})) == 0;
    if (CPP) {
        bool star = gives[strlen(gives) - 1] == '*';
        if (declaration && f->owner && f->is_virtual && !replaces_one(g, f)) put(g, "virtual ");
        if (f->owner && !declaration) putf(g, "%s%s%s::%s(", gives, star ? "" : " ", class_name(g, f->owner), name);
        else putf(g, "%s%s%s(", gives, star ? "" : " ", name);
        parameters(g, f);
        put(g, ")");
        if (declaration && f->owner && replaces_one(g, f)) put(g, " override");
        if (declaration && f->is_abstract) put(g, " = 0");
    } else if (JAVA) {
        bool in_interface = f->owner && f->owner->is_interface;
        if (!f->owner) put(g, "static ");
        else if (asked_by_interface(g, f) && !in_interface) put(g, "public ");
        if (f->is_abstract && !in_interface) put(g, "abstract ");
        putf(g, "%s %s(", gives, name);
        parameters(g, f);
        put(g, ")");
    } else if (RUST) {
        putf(g, "%sfn %s(", g->unsafe ? "unsafe " : "", name);
        parameters(g, f);
        put(g, ")");
        if (!nothing) putf(g, " -> %s", gives);
    } else if (PY) {
        putf(g, "def %s(", name);
        parameters(g, f);
        putf(g, ") -> %s", gives);
    } else {
        if (f->owner && f->is_abstract && TS && !f->owner->is_interface) put(g, "abstract ");
        putf(g, f->owner ? "%s(" : "function %s(", name);
        parameters(g, f);
        put(g, ")");
        if (TS) putf(g, ": %s", gives);
    }
}

static void collect_global(const Expr *e, const char **names, int *count) {
    if (!e || e->kind != EX_NAME || e->is_call || !e->symbol || !e->symbol->is_global || e->symbol->kind != SYM_VAR) return;
    for (int i = 0; i < *count; i++)
        if (strcmp(names[i], e->symbol->c_name) == 0) return;
    if (*count < 32) names[(*count)++] = e->symbol->c_name;
}

static void collect_given(Gen *g, const Expr *e, const char **names, int *count) {
    if (!e) return;
    if (gives_back_call(g, e))
        for (int i = 0; i < e->args.count && i < e->symbol->param_count; i++)
            if (given_back(g, e->symbol->params[i])) collect_global(place_of(g, e->args.items[i]), names, count);
    collect_given(g, e->base, names, count);
    collect_given(g, e->right, names, count);
    for (int i = 0; i < e->args.count; i++) collect_given(g, e->args.items[i], names, count);
}

/* Python: the globals a function gives a new value to must be named. */
static void collect_globals(Gen *g, const Stmt *s, const char **names, int *count) {
    for (; s; s = s->next) {
        if (s->kind == ST_ASSIGN || s->kind == ST_FOR) collect_global(s->target, names, count);
        if (s->kind == ST_READ)
            for (int i = 0; i < s->args.count; i++) collect_global(s->args.items[i], names, count);
        collect_given(g, s->value, names, count);
        collect_given(g, s->target, names, count);
        for (int i = 0; i < s->args.count; i++) collect_given(g, s->args.items[i], names, count);
        collect_globals(g, s->body, names, count);
        for (const Stmt *o = s->otherwise; o; o = o->otherwise) {
            collect_given(g, o->value, names, count);
            collect_globals(g, o->body, names, count);
        }
    }
}

static void body(Gen *g, const Function *f) {
    const Symbol *sym = f->symbol;
    size_t start = g->out->length;
    if (sym->result) declare(g, sym->result, "");
    for (int i = f->owner ? 1 : 0; i < sym->param_count; i++) {
        const Symbol *p = sym->params[i];
        save_bounds(g, p);
        if (given_back(g, p) && p->mode == MODE_OUT) declare(g, p, "");
        /* what the caller passed stays the caller's */
        if (refs(g) && p->type->kind == TY_RECORD && !p->by_reference && param_written(g, f, p)) {
            indent(g);
            note_copy(g, p->type->record);
            putf(g, PY ? "%s = copy.deepcopy(%s)" : "%s = %s.copy()", safe(g, p->c_name), safe(g, p->c_name));
            end(g);
        }
    }
    if (PY) {
        const char *names[32];
        int count = 0;
        collect_globals(g, f->body, names, &count);
        if (count) {
            indent(g);
            put(g, "global ");
            for (int i = 0; i < count; i++) putf(g, "%s%s", i ? ", " : "", safe(g, names[i]));
            put(g, "\n");
        }
    }
    for (const VarDecl *v = f->locals; v; v = v->next) declare(g, v->symbol, "");
    statements(g, f->body);
    if (!ends_with_return(f->body)) {
        if (gives_back(g) && (given_back_count(g, sym) || sym->result)) {
            give(g, NULL);
        } else if (sym->result) {
            indent(g);
            putf(g, "return %s", safe(g, sym->result->c_name));
            end(g);
        }
    }
    if (PY && g->out->length == start) {
        indent(g);
        put(g, "pass\n");
    }
}

/* Java: the values a subprogram gives back travel in a record. */
static void result_record_decl(Gen *g, const Function *f) {
    const Symbol *sym = f->symbol;
    int n = given_back_count(g, sym);
    if (!JAVA || n == 0 || (n == 1 && !has_result(sym))) return;
    indent(g);
    putf(g, "%srecord %s(", g->in_class ? "" : "", result_record(g, sym));
    bool any = false;
    if (has_result(sym)) {
        putf(g, "%s result", type_text(g, sym->type));
        any = true;
    }
    for (int i = 0; i < sym->param_count; i++) {
        if (!given_back(g, sym->params[i])) continue;
        putf(g, "%s%s %s", any ? ", " : "", type_text(g, sym->params[i]->type), safe(g, sym->params[i]->c_name));
        any = true;
    }
    put(g, ") {}\n\n");
}

static bool can_simulate(const Gen *g);

static bool sends_in(const Gen *g, const Expr *e, int depth);

/* Does it send or cancel messages, by itself or through what it calls? */
static bool sends(const Gen *g, const Stmt *s, int depth) {
    for (; s; s = s->next) {
        if (s->kind == ST_SEND || s->kind == ST_CANCEL) return true;
        if (sends_in(g, s->value, depth) || sends_in(g, s->target, depth)) return true;
        for (int i = 0; i < s->args.count; i++)
            if (sends_in(g, s->args.items[i], depth)) return true;
        if (sends(g, s->body, depth)) return true;
        for (const Stmt *o = s->otherwise; o; o = o->otherwise)
            if (sends_in(g, o->value, depth) || sends(g, o->body, depth)) return true;
    }
    return false;
}

static bool sends_in(const Gen *g, const Expr *e, int depth) {
    if (!e || depth > 4) return false;
    if (e->is_call && e->symbol && e->symbol->function && sends(g, e->symbol->function->body, depth + 1)) return true;
    if (sends_in(g, e->base, depth) || sends_in(g, e->right, depth)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (sends_in(g, e->args.items[i], depth)) return true;
    return false;
}

static void function(Gen *g, const Function *f) {
    g->function = f;
    g->serial = g->rename_count = g->seen_count = 0;
    if (f->is_abstract) {
        if (IS(LANG_JS) || CPP) return; /* nothing to say, or said in the class */
        if (PY && !f->owner->is_interface) {
            indent(g);
            put(g, "@abstractmethod\n");
        }
        indent(g);
        header(g, f, true);
        put(g, PY ? ": ...\n" : ";\n");
        if (!RUST && !TS) put(g, "\n");
        return;
    }
    result_record_decl(g, f);
    indent(g);
    header(g, f, false);
    open(g);
    /* a method of a component called from outside: what it sends comes from the component */
    bool acting = f->owner && f->owner->is_component && !f->is_handler && can_simulate(g) && sends(g, f->body, 0);
    if (acting && CPP) {
        indent(g);
        put(g, "Acting acting(this);\n");
    } else if (acting && RUST) {
        indent(g);
        put(g, "let _acting = Acting::new(self.sim_id);\n");
    } else if (acting) {
        indent(g);
        putf(g, JAVA ? "Component before = sim.current;\n" : PY ? "before = sim.current\n" : "const before = sim.current;\n");
        indent(g);
        putf(g, "sim.current = %s", self_name(g));
        end(g);
        indent(g);
        put(g, PY ? "try:\n" : "try {\n");
        g->indent++;
    }
    body(g, f);
    if (acting && !CPP && !RUST) {
        g->indent--;
        indent(g);
        put(g, PY ? "finally:\n" : "} finally {\n");
        g->indent++;
        indent(g);
        put(g, "sim.current = before");
        end(g);
        g->indent--;
        if (!PY) {
            indent(g);
            put(g, "}\n");
        }
    }
    close(g);
    put(g, PY ? "\n" : "\n\n");
    g->function = NULL;
}

/* ---- records and classes ---- */

static bool is_done(const Record **done, int count, const Record *record) {
    for (int i = 0; i < count; i++)
        if (done[i] == record) return true;
    return false;
}

static bool is_abstract(const Class *c) { return c && (c->is_abstract || c->pending); }

/* Reached along two ways by some class: C++ must then hold it once. */
static bool is_shared(const Gen *g, const Class *base) {
    for (const Class *c = g->program->classes; c; c = c->next) {
        int ways = 0;
        for (const BaseName *b = c->bases; b; b = b->next) ways += b->klass && derives_from(b->klass, base);
        if (ways > 1) return true;
    }
    return false;
}

/* this.f = o.f, with copies of what would otherwise be shared. */
static void copy_value(Gen *g, const Type *type, const char *source, int depth) {
    if (type->kind == TY_RECORD) {
        note_copy(g, type->record);
        putf(g, "%s.copy()", source);
    } else if (type->kind == TY_ARRAY) {
        Type inner = *type;
        inner.dims.items = type->dims.items + 1;
        inner.dims.count = type->dims.count - 1;
        const Type *element = inner.dims.count ? &inner : type->base;
        bool plain = element->kind != TY_RECORD && element->kind != TY_ARRAY;
        const char *item = arena_printf(g->arena, "e%d", depth);
        if (plain) {
            putf(g, JAVA ? "%s.clone()" : "%s.slice()", source);
        } else if (JAVA) {
            putf(g, "java.util.Arrays.stream(%s).map(%s -> ", source, item);
            copy_value(g, element, item, depth + 1);
            putf(g, ").toArray(%s[]::new)", array_of(g, scalar_type(g, type->base), type->dims.count - 1));
        } else {
            putf(g, "%s.map(%s => ", source, item);
            copy_value(g, element, item, depth + 1);
            put(g, ")");
        }
    } else {
        put(g, source);
    }
}

static void copy_methods(Gen *g, const Record *record) {
    const char *name = record_name(g, record);
    bool wanted = false;
    for (int i = 0; i < g->copied_count; i++) wanted |= g->copied[i] == record;
    if (!wanted || !(JAVA || JS)) return;
    indent(g);
    putf(g, JAVA ? "void set(%s o)" : TS ? "set(o: %s): void" : "set(o%s)", JAVA || TS ? name : "");
    open(g);
    for (const VarDecl *f = record->fields; f; f = f->next) {
        const char *field_name = safe(g, f->symbol->c_name);
        indent(g);
        putf(g, "this.%s = ", field_name);
        copy_value(g, f->symbol->type, arena_printf(g->arena, "o.%s", field_name), 0);
        put(g, ";\n");
    }
    close(g);
    put(g, "\n\n");
    indent(g);
    putf(g, JAVA ? "%s copy()" : TS ? "copy(): %s" : "copy(%s)", JAVA || TS ? name : "");
    open(g);
    indent(g);
    putf(g, JAVA ? "%s c = new %s();\n" : "const c = new %s%s();\n", name, JAVA ? name : "");
    indent(g);
    put(g, "c.set(this);\n");
    indent(g);
    put(g, "return c;\n");
    close(g);
    put(g, "\n");
}

static void field_decl(Gen *g, const VarDecl *f) {
    const Symbol *sym = f->symbol;
    const char *name = safe(g, sym->c_name), *type = type_text(g, sym->type);
    indent(g);
    if (CPP) {
        putf(g, "%s%s%s", type, type[strlen(type) - 1] == '*' ? "" : " ", name);
        if (sym->type->kind != TY_RECORD) {
            put(g, " = ");
            zero(g, sym->type, 0);
        }
        put(g, ";\n");
    } else if (JAVA) {
        putf(g, "%s %s = ", type, name);
        zero(g, sym->type, 0);
        put(g, ";\n");
    } else if (RUST) {
        putf(g, "%s: %s,\n", name, type);
    } else if (PY) {
        putf(g, "self.%s = ", name);
        zero(g, sym->type, 0);
        put(g, "\n");
    } else {
        putf(g, TS ? "%s: %s = " : "%s%s = ", name, TS ? type : "");
        zero(g, sym->type, 0);
        put(g, ";\n");
    }
}

static void component_parts(Gen *g, const Class *c, int part);

static void type_decl(Gen *g, const Record *record) {
    const Class *c = record->klass;
    const char *name = record_name(g, record);
    bool interface = c && c->is_interface;
    bool own_only = c && c->bases && !RUST;
    int fields = 0, bases = 0;
    for (const VarDecl *f = record->fields; f; f = f->next) fields += !(own_only && f->from);
    for (const BaseName *b = c ? c->bases : NULL; b; b = b->next) bases += b->klass != NULL;

    if (interface && IS(LANG_JS)) {
        putf(g, "// interface %s:", name);
        for (const Function *f = g->program->functions; f; f = f->next)
            if (f->owner == c) putf(g, " %s()", f->name);
        put(g, "\n\n");
        return;
    }
    if (RUST && interface) {
        putf(g, "trait %s", name);
        for (const BaseName *b = c->bases; b; b = b->next)
            if (b->klass) putf(g, "%s%s", b == c->bases ? ": " : " + ", class_name(g, b->klass));
        put(g, " {\n");
        g->indent = 1;
        for (const Function *f = g->program->functions; f; f = f->next)
            if (f->owner == c) function(g, f);
        g->indent = 0;
        put(g, "}\n\n");
        return;
    }
    /* the first line */
    if (CPP) {
        putf(g, "struct %s", name);
        bool any = false;
        if (c && c->is_component) {
            put(g, " : Component");
            any = true;
        }
        for (const BaseName *b = c ? c->bases : NULL; b; b = b->next) {
            if (!b->klass) continue;
            putf(g, "%s%s%s", any ? ", " : " : ", is_shared(g, b->klass) ? "virtual " : "", class_name(g, b->klass));
            any = true;
        }
        put(g, " {\n");
    } else if (JAVA || JS) {
        const char *kind = interface ? "interface" : "class";
        putf(g, "%s%s%s %s", JAVA ? "static " : "", is_abstract(c) && !interface && !IS(LANG_JS) ? "abstract " : "", kind, name);
        bool extends = false, implements = false;
        if (c && c->is_component) {
            put(g, " extends Component");
            extends = true;
        }
        for (const BaseName *b = c ? c->bases : NULL; b; b = b->next) {
            if (!b->klass || (b->klass->is_interface && !interface)) continue;
            putf(g, "%s%s", extends ? ", " : " extends ", class_name(g, b->klass));
            extends = true;
        }
        for (const BaseName *b = c && !interface && !IS(LANG_JS) ? c->bases : NULL; b; b = b->next) {
            if (!b->klass || !b->klass->is_interface) continue;
            putf(g, "%s%s", implements ? ", " : " implements ", class_name(g, b->klass));
            implements = true;
        }
        put(g, " {\n");
    } else if (PY) {
        bool abstract_root = is_abstract(c), any = false;
        for (const BaseName *b = c ? c->bases : NULL; b; b = b->next) abstract_root &= !(b->klass && is_abstract(b->klass));
        putf(g, "class %s", name);
        if (c && c->is_component) {
            put(g, "(Component");
            any = true;
        }
        for (const BaseName *b = c ? c->bases : NULL; b; b = b->next) {
            if (!b->klass) continue;
            putf(g, "%s%s", any ? ", " : "(", class_name(g, b->klass));
            any = true;
        }
        if (abstract_root) {
            g->needs |= N_ABC;
            put(g, any ? ", ABC" : "(ABC");
            any = true;
        }
        put(g, any ? "):\n" : ":\n");
    } else {
        if (c && c->is_component) component_parts(g, c, 2);
        putf(g, "#[derive(Clone)]\nstruct %s {\n", name);
        if (c && c->is_component) put(g, "    sim_id: i32,\n    sim_name: String,\n");
    }
    g->indent = 1;
    size_t inside = g->out->length;
    if (c && c->is_component && !RUST) component_parts(g, c, 0);

    /* its variables */
    if (PY) {
        if (fields || (c && c->is_component)) {
            put(g, "    def __init__(self):\n");
            g->indent = 2;
            if (bases || (c && c->is_component)) put(g, "        super().__init__()\n");
            for (const VarDecl *f = record->fields; f; f = f->next)
                if (!(own_only && f->from)) field_decl(g, f);
            g->indent = 1;
            put(g, "\n");
        }
    } else {
        for (const VarDecl *f = record->fields; f; f = f->next)
            if (!(own_only && f->from)) field_decl(g, f);
        if (CPP && c && (c->has_derived || is_abstract(c)) && !bases) putf(g, "    virtual ~%s() = default;\n", name);
    }
    if (RUST) {
        g->indent = 0;
        putf(g, "}\n\nimpl %s {\n    fn new() -> %s {\n        %s {", name, name, name);
        if (c && c->is_component) put(g, record->fields ? " sim_id: -1, sim_name: String::new()," : " sim_id: -1, sim_name: String::new() ");
        for (const VarDecl *f = record->fields; f; f = f->next) {
            putf(g, "%s%s: ", f == record->fields ? " " : ", ", safe(g, f->symbol->c_name));
            zero(g, f->symbol->type, 0);
        }
        put(g, record->fields || (c && c->is_component) ? " }\n    }\n" : "}\n    }\n");
        g->indent = 1;
    }

    /* what it can do */
    bool any = false;
    for (const Function *f = g->program->functions; c && f; f = f->next) {
        if (f->owner != c) continue;
        if (CPP) { /* only announced: written after everything is known */
            indent(g);
            g->function = f;
            header(g, f, true);
            put(g, ";\n");
            g->function = NULL;
            continue;
        }
        if (RUST && asked_by_interface(g, f)) continue;
        if (f->is_handler && !any && fields) put(g, "\n");
        if (!any && fields && !PY && !RUST) put(g, "\n");
        if (RUST) put(g, "\n");
        any = true;
        g->in_class = true;
        function(g, f);
        g->in_class = false;
    }
    if (c && c->is_component && !RUST) component_parts(g, c, 1);
    copy_methods(g, record);
    if (PY && g->out->length == inside) put(g, "    pass\n\n");
    g->indent = 0;
    /* blank lines between methods end up before the brace */
    while (!PY && g->out->length > 2 && g->out->data[g->out->length - 1] == '\n' && g->out->data[g->out->length - 2] == '\n') {
        g->out->length--;
        g->out->data[g->out->length] = '\0';
    }
    put(g, PY ? "\n" : CPP ? "};\n\n" : "}\n\n");

    if (RUST && c && c->is_component) component_parts(g, c, 3);
    /* Rust: what an interface asks for is written apart, for each */
    for (const Class *k = g->program->classes; RUST && c && k; k = k->next) {
        if (!k->is_interface || !derives_from(c, k)) continue;
        putf(g, "impl %s for %s {\n", class_name(g, k), name);
        g->indent = 1;
        for (const Function *m = g->program->functions; m; m = m->next) {
            if (m->owner != k) continue;
            const Function *mine = find_method(g->program, c, m->name, false);
            if (mine && !mine->is_abstract) function(g, mine);
        }
        g->indent = 0;
        put(g, "}\n\n");
    }
}

/* Each after what it is made of and what it extends. */
static void types_in_order(Gen *g, const Record *record, const Record **done, int *count) {
    if (is_done(done, *count, record) || *count >= 256) return;
    done[(*count)++] = record;
    for (const BaseName *b = record->klass ? record->klass->bases : NULL; b; b = b->next)
        if (b->klass) types_in_order(g, b->klass->record, done, count);
    for (const VarDecl *f = record->fields; f; f = f->next) {
        const Type *type = f->symbol ? f->symbol->type : NULL;
        while (type && type->kind == TY_ARRAY) type = type->base;
        if (type && type->kind == TY_RECORD) types_in_order(g, type->record, done, count);
    }
    type_decl(g, record);
}

/* ---- systems: components, messages, tests ---- */

static bool has_main_function(const Program *program) {
    for (const Function *f = program->functions; f; f = f->next)
        if (is_main(f)) return true;
    return false;
}

#include "emit_sim.inc"

/* ---- the whole program ---- */

static void globals(Gen *g) {
    const Program *p = g->program;
    if (!p->globals) return;
    if (RUST) {
        put(g, "struct Globals {\n");
        for (const VarDecl *v = p->globals; v; v = v->next)
            putf(g, "    %s: %s,\n", safe(g, v->symbol->c_name), type_text(g, v->symbol->type));
        put(g, "}\n\nimpl Globals {\n    fn new() -> Globals {\n        Globals {\n");
        for (const VarDecl *v = p->globals; v; v = v->next) {
            putf(g, "            %s: ", safe(g, v->symbol->c_name));
            zero(g, v->symbol->type, 0);
            put(g, ",\n");
        }
        put(g, "        }\n    }\n}\n\nstatic mut G: *mut Globals = std::ptr::null_mut();\n\n");
        return;
    }
    for (const VarDecl *v = p->globals; v; v = v->next) declare(g, v->symbol, JAVA ? "static " : "");
    put(g, "\n");
}

static void whole(Gen *g) {
    const Program *p = g->program;
    const Record *done[256];
    int count = 0;
    bool systems = has_components(p) || has_tests(p);
    if (CPP) {
        for (const Record *r = p->records; r; r = r->next) putf(g, "struct %s;\n", record_name(g, r));
        if (p->records) put(g, "\n");
    }
    if (systems) simulation(g, 0);
    for (const Record *r = p->records; r; r = r->next) types_in_order(g, r, done, &count);
    globals(g);
    if (CPP) { /* everything is announced before it is used */
        bool any = false;
        for (const Function *f = p->functions; f; f = f->next) {
            if (f->owner || !f->symbol) continue;
            g->function = f;
            header(g, f, true);
            put(g, ";\n");
            any = true;
        }
        g->function = NULL;
        if (any) put(g, "\n");
    }
    for (const Function *f = p->functions; f; f = f->next) {
        if (!f->symbol || (f->owner && !CPP)) continue;
        function(g, f);
        if (PY) put(g, "\n");
    }
    if (systems) {
        simulation(g, 1);
        return;
    }
    if (!has_main_function(p)) return;
    if (CPP) {
        put(g, "int main() {\n");
        if (g->needs & N_FIXED) put(g, "    std::cout << std::fixed;\n");
        put(g, "    run();\n    return 0;\n}\n");
    } else if (JAVA) {
        put(g, "public static void main(String[] args) {\n");
        if (g->needs & N_LOCALE) put(g, "    java.util.Locale.setDefault(java.util.Locale.ROOT);\n");
        put(g, "    run();\n}\n");
    } else if (RUST) {
        if (g->unsafe && p->globals) put(g, "fn main() {\n    unsafe {\n        G = Box::into_raw(Box::new(Globals::new()));\n        run();\n    }\n}\n");
        else if (g->unsafe) put(g, "fn main() {\n    unsafe { run() }\n}\n");
        else put(g, "fn main() {\n    run();\n}\n");
    } else if (PY) {
        put(g, "if __name__ == \"__main__\":\n    main()\n");
    } else {
        put(g, "main();\n");
    }
}

static Gen new_gen(const Program *program, const Config *config, Arena *arena, Lang lang, StrBuf *out) {
    Gen g = {.lang = lang, .program = program, .config = config, .arena = arena, .out = out};
    g.unsafe = lang == LANG_RUST && (program->globals || uses_pointers(program) || has_components(program) || has_tests(program));
    return g;
}

/* What is copied brings along what it is made of. */
static void close_copies(Gen *g) {
    for (int i = 0; i < g->copied_count; i++) {
        for (const VarDecl *f = g->copied[i]->fields; f; f = f->next) {
            const Type *type = f->symbol ? f->symbol->type : NULL;
            while (type && type->kind == TY_ARRAY) type = type->base;
            if (type && type->kind == TY_RECORD) note_copy(g, type->record);
        }
    }
}

void emit_program(const Program *program, const Config *config, Arena *arena, Lang lang, const char *name, StrBuf *out) {
    StrBuf text = {0};
    strbuf_puts(&text, "");
    Gen g = new_gen(program, config, arena, lang, &text);
    g.main_class = name && *name ? name : "Main";
    whole(&g); /* once to learn what is needed, once to write it */
    close_copies(&g);
    text.length = 0;
    text.data[0] = '\0';
    whole(&g);

    long long seed = 1;
    if (program->seed) const_int(program->seed, &seed);
    strbuf_puts(out, "");
    if (lang != LANG_JAVA) {
        emit_prelude(lang, g.needs, seed, out);
        strbuf_append(out, text.data, text.length);
    } else { /* everything lives in a class */
        StrBuf inner = {0};
        strbuf_puts(&inner, "");
        emit_prelude(lang, g.needs, seed, &inner);
        strbuf_append(&inner, text.data, text.length);
        strbuf_printf(out, "public class %s {\n", g.main_class);
        const char *line = inner.data;
        while (*line) {
            const char *stop = strchr(line, '\n');
            size_t length = stop ? (size_t)(stop - line) : strlen(line);
            if (length) strbuf_puts(out, "    ");
            strbuf_append(out, line, length);
            strbuf_puts(out, "\n");
            line += length + (stop ? 1 : 0);
        }
        while (out->length > 1 && out->data[out->length - 1] == '\n' && out->data[out->length - 2] == '\n') out->length--;
        out->data[out->length] = '\0';
        strbuf_puts(out, "}\n");
        strbuf_free(&inner);
    }
    while (out->length > 1 && out->data[out->length - 1] == '\n' && out->data[out->length - 2] == '\n') out->length--;
    out->data[out->length] = '\0';
    strbuf_free(&text);
}

void emit_function(const Program *program, const Config *config, Arena *arena, Lang lang,
                   const Function *function_, StrBuf *out, LineMap *map) {
    Gen g = new_gen(program, config, arena, lang, out);
    g.map = map;
    g.main_class = "Main";
    strbuf_puts(out, "");
    function(&g, function_);
}

void emit_record(const Program *program, const Config *config, Arena *arena, Lang lang, const Record *record, StrBuf *out) {
    Gen g = new_gen(program, config, arena, lang, out);
    g.main_class = "Main";
    strbuf_puts(out, "");
    type_decl(&g, record);
}
