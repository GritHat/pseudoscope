#include "pseudoscope/sema.h"
#include "pseudoscope/builtins.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct Scope {
    struct Scope *parent;
    Symbol *symbols;
} Scope;

typedef struct NamedRecord {
    const char *name;
    Record *record;
    struct NamedRecord *next;
} NamedRecord;

typedef struct UsedName {
    const char *name;
    struct UsedName *next;
} UsedName;

#define NAME_BUCKETS 256

typedef struct {
    Program *program;
    const Config *config;
    Arena *arena;
    Diag *diag;
    Scope *scope;
    Symbol *function; /* being analysed; NULL at top level */
    Scope **class_scopes; /* as program->classes */
    NamedRecord *records;
    Record **records_tail;
    UsedName *used[NAME_BUCKETS];
} Sema;

static Type T_ERROR = {.kind = TY_ERROR}, T_VOID = {.kind = TY_VOID},
            T_INT = {.kind = TY_INT}, T_REAL = {.kind = TY_REAL},
            T_BOOL = {.kind = TY_BOOL}, T_CHAR = {.kind = TY_CHAR},
            T_STRING = {.kind = TY_STRING},
            T_SOMEONE = {.kind = TY_POINTER, .base = &T_VOID};

static Type *check_expr(Sema *s, Expr *expr);
static void check_statements(Sema *s, Stmt *stmt);
static Type *resolve_decl_type(Sema *s, VarDecl *decl, TypeSpec *spec, bool dims_may_be_open);

/* ---- types ---- */

const char *type_name(Arena *arena, const Type *type) {
    switch (type->kind) {
        case TY_VOID: return "no value";
        case TY_INT: return "integer";
        case TY_REAL: return "real";
        case TY_BOOL: return "logical";
        case TY_CHAR: return "character";
        case TY_STRING: return "string";
        case TY_POINTER: return arena_printf(arena, "%s*", type_name(arena, type->base));
        case TY_ARRAY:
            return type->dims.count == 1
                ? arena_printf(arena, "array of %s", type_name(arena, type->base))
                : arena_printf(arena, "%dd array of %s", type->dims.count,
                               type_name(arena, type->base));
        case TY_RECORD: return type->record->name ? type->record->name : "record";
        case TY_ERROR: break;
    }
    return "unknown";
}

static bool is_error(const Type *t) { return t->kind == TY_ERROR; }
static bool is_integral(const Type *t) {
    return t->kind == TY_INT || t->kind == TY_BOOL || t->kind == TY_CHAR;
}
static bool is_number(const Type *t) { return is_integral(t) || t->kind == TY_REAL; }
static bool is_scalar(const Type *t) { return is_number(t) || t->kind == TY_POINTER; }

static Type *pointer_to(Sema *s, Type *base) {
    Type *type = ARENA_NEW(s->arena, Type);
    type->kind = TY_POINTER;
    type->base = base;
    return type;
}

static void check_concrete(Sema *s, const Type *type, Loc loc);

static bool same_type(const Type *a, const Type *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TY_POINTER: return same_type(a->base, b->base);
        case TY_ARRAY: return a->dims.count == b->dims.count && same_type(a->base, b->base);
        case TY_RECORD: return a->record == b->record;
        default: return true;
    }
}

static bool is_zero(const Expr *expr) {
    long long value;
    return expr->kind == EX_INT && const_int(expr, &value) && value == 0;
}

/* An object of a class is also one of the classes it extends. */
static bool is_a(const Type *type, const Type *base) {
    return type->kind == TY_RECORD && base->kind == TY_RECORD && type->record->klass && base->record->klass &&
           derives_from(type->record->klass, base->record->klass);
}

/* Can a value of this type be stored where `to` is wanted? */
static bool assignable(const Type *to, const Type *from, const Expr *value) {
    if (is_error(to) || is_error(from)) return true;
    if (is_number(to) && is_number(from)) return true;
    if (to->kind == TY_POINTER) {
        if (value && is_zero(value)) return true;
        if (from->kind == TY_STRING) return to->base->kind == TY_CHAR && (!value || value->kind == EX_STRING);
        if (from->kind == TY_ARRAY)
            return from->dims.count == 1 && same_type(to->base, from->base);
        if (from->kind == TY_POINTER && from->base->kind == TY_VOID) /* the sender */
            return to->base->kind == TY_RECORD && to->base->record->klass;
        return same_type(to, from) || (from->kind == TY_POINTER && is_a(from->base, to->base));
    }
    if (to->kind == TY_STRING) return from->kind == TY_STRING;
    return to->kind == TY_RECORD && same_type(to, from);
}

/* ---- classes ---- */

const Class *class_of(const Type *type) {
    if (type && type->kind == TY_POINTER) type = type->base;
    return type && type->kind == TY_RECORD ? type->record->klass : NULL;
}

bool has_components(const Program *program) {
    for (const Class *c = program->classes; c; c = c->next)
        if (c->is_component) return true;
    return false;
}

bool has_tests(const Program *program) {
    for (const Function *f = program->functions; f; f = f->next)
        if (f->is_test) return true;
    return false;
}

bool derives_from(const Class *klass, const Class *base) {
    if (klass == base) return true;
    for (const BaseName *b = klass->bases; b; b = b->next)
        if (b->klass && derives_from(b->klass, base)) return true;
    return false;
}

bool has_inheritance(const Program *program) {
    for (const Class *c = program->classes; c; c = c->next)
        if (c->bases || c->is_abstract) return true;
    return false;
}

/* With more than one class to inherit from, the method that wins is the one
 * of the class that extends the others; with none such, the first. */
const Function *find_method(const Program *program, const Class *klass, const char *name, bool handler) {
    for (const Function *f = program->functions; f; f = f->next)
        if (f->owner && (!klass || f->owner == klass) && f->is_handler == handler && strcmp(f->name, name) == 0)
            return f;
    const Function *found = NULL;
    for (const BaseName *b = klass ? klass->bases : NULL; b; b = b->next) {
        const Function *f = b->klass ? find_method(program, b->klass, name, handler) : NULL;
        if (f && (!found || (f != found && derives_from(f->owner, found->owner)))) found = f;
    }
    return found;
}

/* ---- constants ---- */

static bool parse_int(const char *text, long long *value) {
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) base = 16, text += 2;
    else if (text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) base = 8, text += 2;
    else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) base = 2, text += 2;
    else if (text[0] == '0') base = 8;
    errno = 0;
    *value = strtoll(text, NULL, base);
    return errno == 0;
}

bool const_int(const Expr *expr, long long *value) {
    long long a, b;
    switch (expr->kind) {
        case EX_INT:
            return parse_int(expr->text, value);
        case EX_NAME:
            if (!expr->symbol || expr->symbol->kind != SYM_STATE) return false;
            *value = expr->symbol->value;
            return true;
        case EX_UNARY:
            if (!const_int(expr->base, &a)) return false;
            if (expr->op == TOK_PLUS) { *value = a; return true; }
            if (expr->op == TOK_MINUS && a != LLONG_MIN) { *value = -a; return true; }
            return false;
        case EX_BINARY: {
            if (!const_int(expr->base, &a) || !const_int(expr->right, &b)) return false;
            const long long limit = 1LL << 31;
            if (a > limit || a < -limit || b > limit || b < -limit) return false;
            switch (expr->op) {
                case TOK_PLUS: *value = a + b; return true;
                case TOK_MINUS: *value = a - b; return true;
                case TOK_STAR: *value = a * b; return true;
                case TOK_SLASH: if (b == 0) return false; *value = a / b; return true;
                case TOK_MOD: if (b == 0) return false; *value = a % b; return true;
                default: return false;
            }
        }
        default:
            return false;
    }
}

/* ---- C names ---- */

static bool in_list(const char *name, const char *const *words, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (strcmp(name, words[i]) == 0) return true;
    return false;
}

/* Cannot be used for anything in the output. */
static bool is_reserved_in_c(const char *name) {
    static const char *const WORDS[] = {
        /* keywords, up to C23 */
        "alignas", "alignof", "auto", "bool", "break", "case", "char", "const",
        "constexpr", "continue", "default", "do", "double", "else", "enum", "extern",
        "false", "float", "for", "goto", "if", "inline", "int", "long", "nullptr",
        "register", "restrict", "return", "short", "signed", "sizeof", "static",
        "static_assert", "struct", "switch", "thread_local", "true", "typedef", "typeof",
        "typeof_unqual", "union", "unsigned", "void", "volatile", "while", "asm",
        /* macros and functions the output relies on */
        "NULL", "EOF", "stdin", "stdout", "stderr", "errno", "printf", "scanf",
        "fprintf", "exit",
    };
    return in_list(name, WORDS, sizeof WORDS / sizeof WORDS[0]) ||
           strncmp(name, "plike_", 6) == 0 || strncmp(name, "__", 2) == 0 ||
           (name[0] == '_' && name[1] >= 'A' && name[1] <= 'Z');
}

/* Known to compilers as functions: fine for a local variable only. */
static bool is_library_name(const char *name) {
    static const char *const WORDS[] = {
        "main", "FILE", "size_t",
        
        "abort", "abs", "acos", "asin", "atan", "atan2", "atexit", "atof", "atoi", "atol",
        "bsearch", "calloc", "cbrt", "ceil", "clearerr", "copysign", "cos", "cosh", "div",
        "erf", "exit", "exp", "exp2", "expm1", "fabs", "fclose", "fdim", "feof", "ferror",
        "fflush", "fgetc", "fgets", "floor", "fma", "fmax", "fmin", "fmod", "fopen",
        "fprintf", "fputc", "fputs", "fread", "free", "frexp", "fscanf", "fseek", "ftell",
        "fwrite", "gamma", "getc", "getchar", "getenv", "gets", "hypot", "index", "isalnum",
        "isalpha", "isdigit", "islower", "isnan", "isinf", "isspace", "isupper", "j0", "j1",
        "jn", "labs", "ldexp", "ldiv", "lgamma", "log", "log10", "log1p", "log2", "logb",
        "malloc", "memchr", "memcmp", "memcpy", "memmove", "memset", "modf", "nan",
        "nearbyint", "perror", "pow", "printf", "putc", "putchar", "puts", "qsort", "rand",
        "random", "realloc", "remainder", "remove", "rename", "rewind", "rint", "rindex",
        "round", "scanf", "signbit", "sin", "sinh", "snprintf", "sprintf", "sqrt", "srand",
        "sscanf", "strcat", "strchr", "strcmp", "strcpy", "strlen", "strncpy", "strstr",
        "strtol", "system", "tan", "tanh", "tgamma", "tolower", "toupper", "trunc",
        "ungetc", "y0", "y1", "yn",
    };
    return in_list(name, WORDS, sizeof WORDS / sizeof WORDS[0]);
}

static UsedName **bucket(Sema *s, const char *name) {
    unsigned hash = 5381;
    for (const char *c = name; *c; c++) hash = hash * 33u + (unsigned char)*c;
    return &s->used[hash % NAME_BUCKETS];
}

static bool is_used(Sema *s, const char *name) {
    for (UsedName *u = *bucket(s, name); u; u = u->next)
        if (strcmp(u->name, name) == 0) return true;
    return false;
}

static void use_name(Sema *s, const char *name) {
    if (is_used(s, name)) return;
    UsedName *used = ARENA_NEW(s->arena, UsedName);
    used->name = name;
    used->next = *bucket(s, name);
    *bucket(s, name) = used;
}

/* A C identifier no source name maps to, close to the given one. */
static const char *fresh_name(Sema *s, const char *name) {
    /* names reserved for how they begin need a different beginning */
    const char *candidate = strncmp(name, "plike_", 6) == 0 || name[0] == '_'
                                ? arena_printf(s->arena, "v_%s", name) : name;
    while (is_used(s, candidate) || is_reserved_in_c(candidate) || is_library_name(candidate))
        candidate = arena_printf(s->arena, "%s_", candidate);
    use_name(s, candidate);
    return candidate;
}

static const char *c_name(Sema *s, const char *name, bool is_global) {
    char clean[128];
    size_t n = 0;
    bool foreign = false; /* "año" */
    for (const char *c = name; *c && n + 1 < sizeof clean; c++) {
        if ((unsigned char)*c >= 0x80) foreign = true;
        else clean[n++] = *c;
        if ((unsigned char)*c >= 0xC0) clean[n++] = '_';
    }
    clean[n] = '\0';
    if (foreign) return fresh_name(s, arena_strdup(s->arena, clean));
    bool clash = is_reserved_in_c(name) || (is_global && is_library_name(name));
    return clash ? fresh_name(s, name) : name;
}

static void collect_names(Sema *s, const VarDecl *decl) {
    for (; decl; decl = decl->next) {
        use_name(s, decl->name);
        if (decl->spec && decl->spec->record && decl->spec->record->fields != decl)
            collect_names(s, decl->spec->record->fields);
    }
}

static void collect_statement_names(Sema *s, const Stmt *stmt) {
    for (; stmt; stmt = stmt->next) {
        collect_names(s, stmt->vars);
        collect_statement_names(s, stmt->body);
        for (const Stmt *o = stmt->otherwise; o; o = o->otherwise)
            collect_statement_names(s, o->body);
    }
}

/* ---- symbols ---- */

static Symbol *find_local(const Scope *scope, const char *name) {
    for (Symbol *sym = scope->symbols; sym; sym = sym->next)
        if (strcmp(sym->name, name) == 0) return sym;
    return NULL;
}

/* Exact spelling first, in every scope; then ignoring case, with a warning:
 * pseudocode is often sloppy about "n" and "N". */
static Symbol *lookup(Sema *s, const char *name, Loc loc) {
    for (Scope *scope = s->scope; scope; scope = scope->parent) {
        Symbol *sym = find_local(scope, name);
        if (sym) return sym;
    }
    for (Scope *scope = s->scope; scope; scope = scope->parent) {
        for (Symbol *sym = scope->symbols; sym; sym = sym->next) {
            if (equals_ignore_case(sym->name, name)) {
                diag_warning(s->diag, loc, "'%s' is not declared, using '%s'", name, sym->name);
                return sym;
            }
        }
    }
    return NULL;
}

static Symbol *declare(Sema *s, SymbolKind kind, const char *name, Loc loc, Type *type) {
    Symbol *previous = find_local(s->scope, name);
    if (previous) {
        diag_error(s->diag, loc, "'%s' is already declared at line %d", name, previous->loc.line);
        return previous;
    }
    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->kind = kind;
    sym->name = name;
    sym->c_name = c_name(s, name, s->scope->parent == NULL);
    sym->loc = loc;
    sym->type = type;
    sym->is_global = s->scope->parent == NULL;
    sym->next = s->scope->symbols;
    s->scope->symbols = sym;
    return sym;
}

static Record *find_record(Sema *s, const char *name, Loc loc) {
    for (NamedRecord *r = s->records; r; r = r->next)
        if (strcmp(r->name, name) == 0) return r->record;
    for (NamedRecord *r = s->records; r; r = r->next) {
        if (equals_ignore_case(r->name, name)) {
            diag_warning(s->diag, loc, "type '%s' is not declared, using '%s'", name, r->name);
            return r->record;
        }
    }
    return NULL;
}

static void add_record(Sema *s, const char *name, Record *record, Loc loc) {
    for (NamedRecord *r = s->records; r; r = r->next) {
        if (strcmp(r->name, name) == 0) {
            diag_error(s->diag, loc, "type '%s' is already declared at line %d", name,
                       r->record->loc.line);
            return;
        }
    }
    NamedRecord *entry = ARENA_NEW(s->arena, NamedRecord);
    entry->name = name;
    entry->record = record;
    entry->next = s->records;
    s->records = entry;
}

/* ---- resolving types ---- */

enum { RECORD_NEW, RECORD_OPEN, RECORD_DONE };

static bool dims_are_constant(const Type *type) {
    long long value;
    for (int i = 0; i < type->dims.count; i++) {
        const Dim *dim = &type->dims.items[i];
        if (!dim->hi || !const_int(dim->hi, &value)) return false;
        if (dim->lo && !const_int(dim->lo, &value)) return false;
    }
    return true;
}

static void check_fixed_size(Sema *s, const Type *type, Loc loc, const char *what) {
    if (type->kind == TY_ARRAY && !dims_are_constant(type))
        diag_error(s->diag, loc, "the size of %s must be constant", what);
}

static void resolve_record(Sema *s, Record *record) {
    if (record->state == RECORD_DONE) return;
    if (record->state == RECORD_OPEN) {
        diag_error(s->diag, record->loc, "record '%s' contains itself; use a pointer",
                   record->name ? record->name : "");
        return;
    }
    record->state = RECORD_OPEN;
    if (!record->c_name)
        record->c_name = record->name ? c_name(s, record->name, true) : fresh_name(s, "record");
    for (VarDecl *field = record->fields; field; field = field->next) {
        for (VarDecl *other = record->fields; other != field; other = other->next)
            if (strcmp(other->name, field->name) == 0)
                diag_error(s->diag, field->loc, "field '%s' is already declared", field->name);
        Symbol *sym = ARENA_NEW(s->arena, Symbol);
        sym->kind = SYM_VAR;
        sym->name = field->name;
        sym->c_name = c_name(s, field->name, false);
        sym->loc = field->loc;
        sym->type = resolve_decl_type(s, field, field->spec, false);
        field->symbol = sym;
        check_concrete(s, sym->type, field->loc);
        check_fixed_size(s, sym->type, field->loc, "an array inside a record");
        Type *inner = sym->type;
        while (inner->kind == TY_ARRAY) inner = inner->base;
        if (inner->kind == TY_RECORD) resolve_record(s, inner->record);
    }
    record->state = RECORD_DONE;
    *s->records_tail = record;
    s->records_tail = &record->next;
}

static void check_dim_expr(Sema *s, Expr *expr) {
    if (!expr) return;
    Type *type = check_expr(s, expr);
    if (!is_error(type) && type->kind != TY_INT)
        diag_error(s->diag, expr->loc, "an array bound must be an integer, not %s",
                   type_name(s->arena, type));
}

/* With several declarators sharing one spec, bounds are checked once each; the
 * expressions simply get the same annotations again. */
static Type *resolve_decl_type(Sema *s, VarDecl *decl, TypeSpec *spec, bool dims_may_be_open) {
    Type *base;
    switch (spec->base) {
        case TOK_INTEGER: base = &T_INT; break;
        case TOK_REAL: base = &T_REAL; break;
        case TOK_LOGICAL: base = &T_BOOL; break;
        case TOK_CHARACTER: base = &T_CHAR; break;
        case TOK_STRINGTYPE: base = &T_STRING; break;
        default: {
            Record *record = spec->record;
            if (!record) record = find_record(s, spec->name, spec->loc);
            if (!record && equals_ignore_case(spec->name, "string")) {
                base = &T_STRING;
                break;
            }
            if (!record) {
                diag_error(s->diag, spec->loc, "unknown type '%s'", spec->name);
                return &T_ERROR;
            }
            base = ARENA_NEW(s->arena, Type);
            base->kind = TY_RECORD;
            base->record = record;
            if (spec->record && record->state == RECORD_NEW) {
                if (!record->name && decl) {
                    record->c_name = fresh_name(s, arena_printf(s->arena, "%s_record", decl->name));
                }
                resolve_record(s, record);
            }
        }
    }
    int pointers = spec->pointers + (decl ? decl->pointers : 0);
    while (pointers-- > 0) base = pointer_to(s, base);

    DimList dims = spec->dims;
    if (decl && decl->has_dims) {
        dims = decl->dims;
        bool rank_given = spec->rank > 1 || spec->dims.count > 0;
        if (rank_given && spec->rank != dims.count)
            diag_error(s->diag, decl->loc, "'%s' has %d dimension%s here but its type has %d",
                       decl->name, dims.count, dims.count == 1 ? "" : "s", spec->rank);
    } else if (!spec->is_array) {
        return base;
    }
    if (dims.count == 0) { /* "array of t" and "2d array of t" */
        dims.count = dims.capacity = spec->rank;
        dims.items = arena_alloc(s->arena, (size_t)spec->rank * sizeof(Dim));
    }
    for (int i = 0; i < dims.count; i++) {
        Dim *dim = &dims.items[i];
        check_dim_expr(s, dim->lo);
        check_dim_expr(s, dim->hi);
        long long lo = s->config->index_base, hi;
        if (!dim->hi && !dims_may_be_open) {
            diag_error(s->diag, decl ? decl->loc : spec->loc, "the size of '%s' is missing",
                       decl ? decl->name : "the array");
            return &T_ERROR;
        }
        bool lo_known = !dim->lo || const_int(dim->lo, &lo);
        if (dim->hi && lo_known && const_int(dim->hi, &hi)) {
            long long size = dim->lo ? hi - lo + 1 : hi;
            if (size <= 0 || size > INT_MAX)
                diag_error(s->diag, dim->hi->loc, "the size of an array must be positive");
        }
    }
    Type *type = ARENA_NEW(s->arena, Type);
    type->kind = TY_ARRAY;
    type->base = base;
    type->dims = dims;
    return type;
}

/* ---- expressions ---- */

static Symbol *find_builtin(Sema *s, const char *name) {
    const Builtin *b = builtin_named(name);
    if (!b) return NULL;
    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->kind = SYM_FUNCTION;
    sym->name = name;
    sym->c_name = sym->builtin = b->name;
    sym->type = b->result == 'i' ? &T_INT : b->result == 't' ? &T_STRING : b->result == 'b' ? &T_BOOL : b->result == 'v' ? &T_VOID : &T_REAL;
    sym->param_count = b->arity;
    return sym;
}

static bool is_lvalue(const Expr *expr) {
    switch (expr->kind) {
        case EX_NAME:
            return expr->symbol && !expr->is_call &&
                   (expr->symbol->kind == SYM_VAR || expr->symbol->kind == SYM_PARAM || expr->symbol->kind == SYM_RESULT);
        case EX_APPLY:
        case EX_INDEX: return !expr->is_call;
        case EX_FIELD: return expr->base->type->kind == TY_POINTER || is_lvalue(expr->base);
        case EX_UNARY:
            if (expr->op == TOK_STAR) return true;
            return expr->op == TOK_AT && is_lvalue(expr->base);
        default: return false;
    }
}

static Type *check_value(Sema *s, Expr *expr) {
    Type *type = check_expr(s, expr);
    if (type->kind == TY_VOID) {
        diag_error(s->diag, expr->loc, "a procedure has no value");
        return &T_ERROR;
    }
    return type;
}

static Type *check_scalar(Sema *s, Expr *expr, const char *what) {
    Type *type = check_value(s, expr);
    if (is_error(type) || is_scalar(type)) return type;
    diag_error(s->diag, expr->loc, "%s cannot be %s", what, type_name(s->arena, type));
    return &T_ERROR;
}

static void check_argument(Sema *s, Symbol *callee, int index, Expr *arg) {
    Type *type = check_value(s, arg);
    if (!callee->params) { /* builtin */
        const Builtin *b = callee->builtin ? builtin_named(callee->builtin) : NULL;
        char wants = b && index < (int)strlen(b->takes) ? b->takes[index] : 'n';
        if (wants == 't') {
            if (!is_error(type) && type->kind != TY_STRING && type->kind != TY_CHAR)
                diag_error(s->diag, arg->loc, "'%s' takes a text, not %s", callee->name, type_name(s->arena, type));
            return;
        }
        if (wants == 'a' && (type->kind == TY_STRING || is_number(type))) return;
        if (!is_error(type) && !is_number(type))
            diag_error(s->diag, arg->loc, "'%s' takes numbers, not %s", callee->name,
                       type_name(s->arena, type));
        return;
    }
    Symbol *param = callee->params[index];
    Type *want = param->type;
    if (is_error(type) || is_error(want)) return;
    bool ok;
    if (want->kind == TY_ARRAY) {
        ok = type->kind == TY_ARRAY ? same_type(want, type)
           : type->kind == TY_POINTER && want->dims.count == 1 && same_type(want->base, type->base);
        if (type->kind == TY_STRING)
            ok = want->base->kind == TY_CHAR && want->dims.count == 1 && param->mode == MODE_IN && arg->kind == EX_STRING;
    } else if (param->by_reference) {
        if (arg->kind == EX_UNARY && arg->op == TOK_AMP) { /* swap(&a, &b) */
            ok = type->kind == TY_POINTER && (same_type(type->base, want) || is_a(type->base, want));
        } else if (arg->kind == EX_UNARY && arg->op == TOK_AT) {
            ok = type->kind == TY_POINTER && (same_type(type->base, want) || is_a(type->base, want));
        } else if (!is_lvalue(arg)) {
            diag_error(s->diag, arg->loc, "'%s' is an %s parameter: it needs a variable",
                       param->name, param->mode == MODE_OUT ? "out" : "in/out");
            return;
        } else {
            ok = same_type(type, want) || is_a(type, want);
        }
    } else {
        ok = assignable(want, type, arg);
    }
    if (!ok)
        diag_error(s->diag, arg->loc, "parameter '%s' of '%s' is %s, not %s", param->name,
                   callee->name, type_name(s->arena, want), type_name(s->arena, type));
}

static Type *check_call(Sema *s, Expr *expr, Symbol *callee) {
    expr->is_call = true;
    expr->symbol = callee;
    if (expr->args.count != callee->param_count) {
        diag_error(s->diag, expr->loc, "'%s' takes %d argument%s, not %d", callee->name,
                   callee->param_count, callee->param_count == 1 ? "" : "s", expr->args.count);
        for (int i = 0; i < expr->args.count; i++) check_value(s, expr->args.items[i]);
        return callee->type;
    }
    for (int i = 0; i < expr->args.count; i++) check_argument(s, callee, i, expr->args.items[i]);
    const Builtin *b = callee->builtin ? builtin_named(callee->builtin) : NULL;
    if (b && b->result == 'n') { /* as what it is given */
        bool real = false;
        for (int i = 0; i < expr->args.count; i++) real |= !is_integral(expr->args.items[i]->type);
        return real ? &T_REAL : &T_INT;
    }
    return callee->type;
}

/* A function nobody defined: guess its prototype from this call, so that the
 * output compiles and can be linked with a C definition. */
static Type *check_external_call(Sema *s, Expr *expr, const char *name, bool needs_value) {
    for (Symbol *known = s->program->externals; known; known = known->next) {
        if (strcmp(known->name, name) != 0) continue;
        if (needs_value && known->type->kind == TY_VOID) known->type = &T_INT;
        return check_call(s, expr, known);
    }
    diag_warning(s->diag, expr->loc, "'%s' is not defined in this file", name);
    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->kind = SYM_FUNCTION;
    sym->name = name;
    sym->c_name = c_name(s, name, true);
    sym->loc = expr->loc;
    sym->type = needs_value ? &T_INT : &T_VOID;
    sym->param_count = expr->args.count;
    sym->params = arena_alloc(s->arena, (size_t)(expr->args.count + 1) * sizeof(Symbol *));
    for (int i = 0; i < expr->args.count; i++) {
        Type *type = check_value(s, expr->args.items[i]);
        if (type->kind == TY_ARRAY) {
            Type *array = ARENA_NEW(s->arena, Type);
            *array = *type;
            type = array;
        }
        Symbol *param = ARENA_NEW(s->arena, Symbol);
        param->kind = SYM_PARAM;
        param->name = arena_printf(s->arena, "argument %d", i + 1);
        param->type = type;
        param->flattened = type->kind == TY_ARRAY && type->dims.count > 1;
        sym->params[i] = param;
    }
    sym->next = s->program->externals;
    s->program->externals = sym;
    expr->is_call = true;
    expr->symbol = sym;
    return sym->type;
}

/* base[args]; also flattens a[i][j] into a[i, j]. */
static Type *check_index(Sema *s, Expr *expr) {
    Type *type = check_value(s, expr->base);
    for (int i = 0; i < expr->args.count; i++) {
        Type *index = check_value(s, expr->args.items[i]);
        if (!is_error(index) && !is_integral(index))
            diag_error(s->diag, expr->args.items[i]->loc, "an array index must be an integer, not %s",
                       type_name(s->arena, index));
    }
    if (is_error(type)) return type;
    if (type->kind == TY_POINTER) {
        if (expr->args.count != 1) {
            diag_error(s->diag, expr->loc, "a pointer takes one index, not %d", expr->args.count);
            return &T_ERROR;
        }
        return type->base;
    }
    if (type->kind != TY_ARRAY) {
        diag_error(s->diag, expr->loc, "%s is not an array", type_name(s->arena, type));
        return &T_ERROR;
    }
    if (expr->args.count > type->dims.count) {
        diag_error(s->diag, expr->loc, "the array has %d dimension%s but %d indices were given",
                   type->dims.count, type->dims.count == 1 ? "" : "s", expr->args.count);
        return &T_ERROR;
    }
    for (Expr *root = expr->base; root; root = root->base)
        if (root->symbol) root->symbol->indexed = true;

    if (expr->args.count == type->dims.count) return type->base;
    Type *rest = ARENA_NEW(s->arena, Type); /* a row of the array */
    rest->kind = TY_ARRAY;
    rest->base = type->base;
    rest->dims.items = type->dims.items + expr->args.count;
    rest->dims.count = rest->dims.capacity = type->dims.count - expr->args.count;
    return rest;
}

static Expr *new_expr(Sema *s, ExprKind kind, Loc loc) {
    Expr *e = ARENA_NEW(s->arena, Expr);
    e->kind = kind;
    e->loc = loc;
    e->depth = 1;
    return e;
}

static Expr *self_expr(Sema *s, Loc loc) {
    Expr *self = new_expr(s, EX_NAME, loc);
    self->text = "self";
    return self;
}

/* receiver.method(args): the receiver becomes the first argument. */
static Type *check_method(Sema *s, Expr *expr, Expr *receiver, const Function *method) {
    if (!method->symbol) return &T_ERROR;
    if (receiver->type && receiver->type->kind == TY_POINTER) {
        Expr *star = new_expr(s, EX_UNARY, receiver->loc);
        star->op = TOK_STAR;
        star->base = receiver;
        receiver = star;
    }
    ExprList args = {arena_alloc(s->arena, (size_t)(expr->args.count + 1) * sizeof(Expr *)),
                     expr->args.count + 1, expr->args.count + 1};
    args.items[0] = receiver;
    for (int i = 0; i < expr->args.count; i++) args.items[i + 1] = expr->args.items[i];
    expr->kind = EX_APPLY;
    expr->args = args;
    expr->is_method = true;
    if (!expr->base) {
        expr->base = new_expr(s, EX_NAME, expr->loc);
        expr->base->text = method->name;
    }
    return check_call(s, expr, method->symbol);
}

/* super.m, Base.m: the method of a class this one extends, on this object. */
static const Function *static_method(Sema *s, const Expr *field) {
    const Expr *base = field->base;
    if (base->kind != EX_NAME || !s->function || !s->function->function || !s->function->function->owner) return NULL;
    const Class *owner = s->function->function->owner;
    bool super = equals_ignore_case(base->text, "super");
    const Class *named = NULL;
    for (const Class *c = s->program->classes; c && !super; c = c->next)
        if (strcmp(c->name, base->text) == 0) named = c;
    if ((!super && !named) || lookup(s, base->text, base->loc)) return NULL;
    const Function *found = NULL;
    if (named && derives_from(owner, named)) found = find_method(s->program, named, field->text, false);
    for (const BaseName *b = super ? owner->bases : NULL; b && !found; b = b->next)
        if (b->klass) found = find_method(s->program, b->klass, field->text, false);
    if (!found) {
        if (super) diag_error(s->diag, field->loc, "no class that '%s' extends has '%s'", owner->name, field->text);
        else diag_error(s->diag, field->loc, "'%s' has no '%s' that '%s' could use", base->text, field->text, owner->name);
    } else if (found->is_abstract) {
        diag_error(s->diag, field->loc, "'%s' is abstract: there is nothing to call", found->full_name);
        found = NULL;
    }
    return found;
}

static bool is_static_receiver(Sema *s, const Expr *field) {
    const Expr *base = field->base;
    if (base->kind != EX_NAME || !s->function || !s->function->function || !s->function->function->owner) return false;
    if (lookup(s, base->text, base->loc)) return false;
    if (equals_ignore_case(base->text, "super")) return true;
    for (const Class *c = s->program->classes; c; c = c->next)
        if (strcmp(c->name, base->text) == 0) return true;
    return false;
}

static Type *check_field(Sema *s, Expr *expr) {
    if (is_static_receiver(s, expr)) { /* a call without parentheses */
        const Function *method = static_method(s, expr);
        if (!method) return &T_ERROR;
        Expr *self = self_expr(s, expr->loc);
        check_expr(s, self);
        expr->base = NULL;
        expr->is_static = true;
        return check_method(s, expr, self, method);
    }
    Type *type = check_value(s, expr->base);
    if (is_error(type)) return type;
    if (type->kind == TY_POINTER && type->base->kind == TY_RECORD) type = type->base;
    if (type->kind != TY_RECORD) {
        diag_error(s->diag, expr->loc, "%s is not a record", type_name(s->arena, type));
        return &T_ERROR;
    }
    for (VarDecl *field = type->record->fields; field; field = field->next) {
        if (strcmp(field->name, expr->text) == 0) {
            expr->symbol = field->symbol;
            return field->symbol ? field->symbol->type : &T_ERROR;
        }
    }
    const Function *method = type->record->klass ? find_method(s->program, type->record->klass, expr->text, false) : NULL;
    if (method) { /* a call without parentheses */
        Expr *receiver = expr->base;
        expr->base = NULL;
        return check_method(s, expr, receiver, method);
    }
    diag_error(s->diag, expr->loc, "there is no field '%s' in %s", expr->text,
               type_name(s->arena, type));
    return &T_ERROR;
}

static Type *check_unary(Sema *s, Expr *expr) {
    Type *type = check_value(s, expr->base);
    if (is_error(type)) return type;
    switch (expr->op) {
        case TOK_NOT:
            if (is_scalar(type)) return &T_BOOL;
            break;
        case TOK_MINUS: case TOK_PLUS:
            if (type->kind == TY_REAL) return type;
            if (is_integral(type)) return &T_INT;
            break;
        case TOK_TILDE:
            if (is_integral(type)) return &T_INT;
            break;
        case TOK_STAR:
            if (type->kind == TY_POINTER) return type->base;
            diag_error(s->diag, expr->loc, "%s is not a pointer", type_name(s->arena, type));
            return &T_ERROR;
        case TOK_AMP:
            if (is_lvalue(expr->base) && type->kind != TY_ARRAY) return pointer_to(s, type);
            diag_error(s->diag, expr->loc, "only a variable has an address");
            return &T_ERROR;
        case TOK_AT: /* the pointer behind an out parameter */
            if (expr->base->kind == EX_NAME && expr->base->symbol && expr->base->symbol->by_reference)
                return pointer_to(s, type);
            return type;
        default:
            break;
    }
    diag_error(s->diag, expr->loc, "%s cannot be applied to %s", token_kind_name(expr->op),
               type_name(s->arena, type));
    return &T_ERROR;
}

/* "server.state = Busy": the states of a component have a meaning next to its
 * state, also from outside. */
static void resolve_state(Sema *s, Expr *known, Expr *name) {
    if (name->kind != EX_NAME || name->symbol) return;
    if (known->kind != EX_FIELD && known->kind != EX_NAME) return;
    for (Scope *scope = s->scope; scope; scope = scope->parent)
        if (find_local(scope, name->text)) return;
    Type *type = check_expr(s, known);
    if (!type->states) return;
    int value = 0;
    for (const StateName *state = type->states->states; state; state = state->next, value++) {
        if (strcmp(state->name, name->text) != 0) continue;
        Symbol *sym = ARENA_NEW(s->arena, Symbol);
        sym->kind = SYM_STATE;
        sym->name = sym->c_name = state->name;
        sym->value = value;
        sym->type = type;
        name->symbol = sym;
        return;
    }
}

static Type *check_binary(Sema *s, Expr *expr) {
    if (expr->op == TOK_EQ || expr->op == TOK_NE) {
        resolve_state(s, expr->base, expr->right);
        resolve_state(s, expr->right, expr->base);
    }
    Type *left = check_value(s, expr->base);
    Type *right = check_value(s, expr->right);
    if (is_error(left) || is_error(right)) return &T_ERROR;
    bool numbers = is_number(left) && is_number(right);
    bool integers = is_integral(left) && is_integral(right);
    bool text = left->kind == TY_STRING || right->kind == TY_STRING;
    if (text && (expr->op == TOK_PLUS || expr->op == TOK_CONCAT) &&
        (left->kind == TY_STRING || is_number(left)) && (right->kind == TY_STRING || is_number(right)))
        return &T_STRING;
    switch (expr->op) {
        case TOK_RDIV:
            if (numbers) return &T_REAL;
            break;
        case TOK_PLUS: case TOK_MINUS:
            if (left->kind == TY_POINTER && is_integral(right)) return left;
            if (expr->op == TOK_PLUS && is_integral(left) && right->kind == TY_POINTER) return right;
            /* fall through */
        case TOK_STAR: case TOK_SLASH:
            if (numbers)
                return left->kind == TY_REAL || right->kind == TY_REAL ? &T_REAL : &T_INT;
            break;
        case TOK_MOD: case TOK_SHL: case TOK_SHR: case TOK_AMP: case TOK_PIPE: case TOK_CARET:
            if (integers) return &T_INT;
            break;
        case TOK_AND: case TOK_OR:
            if (is_scalar(left) && is_scalar(right)) return &T_BOOL;
            break;
        default: /* comparisons */
            if (numbers || (left->kind == TY_STRING && right->kind == TY_STRING)) return &T_BOOL;
            if (left->kind == TY_POINTER && right->kind == TY_POINTER &&
                (left->base->kind == TY_VOID || right->base->kind == TY_VOID))
                return &T_BOOL;
            if (left->kind == TY_POINTER && (same_type(left, right) || is_zero(expr->right))) return &T_BOOL;
            if (right->kind == TY_POINTER && is_zero(expr->base)) return &T_BOOL;
            break;
    }
    diag_error(s->diag, expr->loc, "%s cannot be applied to %s and %s", token_kind_name(expr->op),
               type_name(s->arena, left), type_name(s->arena, right));
    return &T_ERROR;
}

/* A function's name inside its own body is the value it will return. */
static Symbol *result_of(Sema *s, Symbol *sym) {
    if (s->function && sym->function && sym->function == s->function->function && s->function->result)
        return s->function->result;
    return sym;
}

static Symbol *special(Sema *s, SymbolKind kind, const char *name, Type *type) {
    Symbol *sym = ARENA_NEW(s->arena, Symbol);
    sym->kind = kind;
    sym->name = sym->c_name = name;
    sym->type = type;
    return sym;
}

static Type *check_name(Sema *s, Expr *expr) {
    if (expr->symbol && expr->symbol->kind == SYM_STATE) return expr->symbol->type;
    Symbol *sym = lookup(s, expr->text, expr->loc);
    if (!sym && strcmp(expr->text, "now") == 0 && has_components(s->program))
        sym = special(s, SYM_NOW, "now", &T_INT);
    if (!sym && strcmp(expr->text, "sender") == 0 && s->function && s->function->function->owner)
        sym = special(s, SYM_SENDER, "sender", &T_SOMEONE);
    if (sym && sym->kind == SYM_FIELD) { /* a variable of the class */
        expr->kind = EX_FIELD;
        expr->base = self_expr(s, expr->loc);
        expr->op = TOK_DOT;
        return check_field(s, expr);
    }
    if (!sym) {
        diag_error(s->diag, expr->loc, "'%s' is not declared", expr->text);
        return &T_ERROR;
    }
    sym = result_of(s, sym);
    expr->symbol = sym;
    if (sym->kind == SYM_FUNCTION && sym->function && sym->function->owner) {
        expr->base = NULL;
        Expr *self = self_expr(s, expr->loc);
        check_expr(s, self);
        return check_method(s, expr, self, sym->function);
    }
    if (sym->kind == SYM_FUNCTION) return check_call(s, expr, sym); /* call without () */
    return sym->type;
}

static Type *check_apply(Sema *s, Expr *expr, bool needs_value) {
    if (expr->is_method) return check_call(s, expr, expr->symbol); /* seen before */
    if (expr->base->kind == EX_FIELD && is_static_receiver(s, expr->base)) {
        const Function *method = static_method(s, expr->base);
        for (int i = 0; i < expr->args.count && !method; i++) check_value(s, expr->args.items[i]);
        if (!method) return &T_ERROR;
        Expr *self = self_expr(s, expr->loc);
        check_expr(s, self);
        expr->is_static = true;
        return check_method(s, expr, self, method);
    }
    if (expr->base->kind == EX_FIELD) {
        Type *type = check_value(s, expr->base->base);
        const Class *klass = class_of(type);
        const Function *method = klass ? find_method(s->program, klass, expr->base->text, false) : NULL;
        if (method) return check_method(s, expr, expr->base->base, method);
    }
    if (expr->base->kind == EX_NAME) {
        const char *name = expr->base->text;
        Symbol *sym = lookup(s, name, expr->base->loc);
        if (!sym) sym = find_builtin(s, name);
        if (!sym) return check_external_call(s, expr, name, needs_value);
        if (sym->kind == SYM_FUNCTION && sym->function && sym->function->owner) {
            Expr *self = self_expr(s, expr->loc);
            check_expr(s, self);
            return check_method(s, expr, self, sym->function);
        }
        if (sym->kind == SYM_FUNCTION) return check_call(s, expr, sym);
    }
    if (expr->args.count == 0) {
        diag_error(s->diag, expr->loc, "this is not a function");
        return &T_ERROR;
    }
    Type *type = check_index(s, expr);
    if (!s->config->paren_arrays && !is_error(type))
        diag_error(s->diag, expr->loc, "arrays are accessed with [] (see --mixed-arrays)");
    return type;
}

static Type *check_expr(Sema *s, Expr *expr) {
    Type *type = &T_ERROR;
    switch (expr->kind) {
        case EX_INT: {
            long long value;
            if (!parse_int(expr->text, &value) || value > UINT_MAX)
                diag_error(s->diag, expr->loc, "the number does not fit an integer");
            type = &T_INT;
            break;
        }
        case EX_FLOAT: type = &T_REAL; break;
        case EX_BOOL: type = &T_BOOL; break;
        case EX_CHAR: type = &T_CHAR; break;
        case EX_STRING: type = &T_STRING; break;
        case EX_NAME: type = check_name(s, expr); break;
        case EX_APPLY: type = check_apply(s, expr, true); break;
        case EX_INDEX: type = check_index(s, expr); break;
        case EX_FIELD: type = check_field(s, expr); break;
        case EX_UNARY: type = check_unary(s, expr); break;
        case EX_BINARY: type = check_binary(s, expr); break;
    }
    expr->type = type;
    return type;
}

/* ---- statements ---- */

static void declare_variables(Sema *s, VarDecl *decl) {
    for (; decl; decl = decl->next) {
        Type *type = resolve_decl_type(s, decl, decl->spec, false);
        decl->symbol = declare(s, SYM_VAR, decl->name, decl->loc, type);
        check_concrete(s, type, decl->loc);
        const Type *base = type;
        while (base->kind == TY_ARRAY) base = base->base;
        if (s->scope->parent && base->kind == TY_RECORD && base->record->klass && base->record->klass->is_component)
            diag_error(s->diag, decl->loc, "'%s' is a component: it must be a global variable", decl->name);
    }
}

static void check_block(Sema *s, Stmt *body) {
    Scope scope = {.parent = s->scope};
    s->scope = &scope;
    check_statements(s, body);
    s->scope = scope.parent;
}

static void check_assignment(Sema *s, Stmt *stmt) {
    Type *target = check_value(s, stmt->target);
    Type *value = check_value(s, stmt->value);
    if (stmt->target->is_call) {
        const char *name = stmt->target->symbol->name;
        diag_error(s->diag, stmt->target->loc, "'%s' is a function: it cannot be assigned", name);
        return;
    }
    if (is_error(target) || is_error(value)) return;
    if (!is_lvalue(stmt->target)) {
        diag_error(s->diag, stmt->target->loc, "this cannot be assigned");
        return;
    }
    if (!assignable(target, value, stmt->value))
        diag_error(s->diag, stmt->loc, "%s cannot be assigned to %s",
                   type_name(s->arena, value), type_name(s->arena, target));
}

static void check_return(Sema *s, Stmt *stmt) {
    if (!stmt->value) return;
    Type *type = check_value(s, stmt->value);
    if (s->function->type->kind == TY_VOID)
        diag_error(s->diag, stmt->loc, "a procedure cannot return a value");
    else if (!assignable(s->function->type, type, stmt->value))
        diag_error(s->diag, stmt->value->loc, "'%s' returns %s, not %s", s->function->name,
                   type_name(s->arena, s->function->type), type_name(s->arena, type));
}

static void check_for(Sema *s, Stmt *stmt) {
    Type *type = check_value(s, stmt->target);
    if (!is_error(type) && (!is_number(type) || !is_lvalue(stmt->target)))
        diag_error(s->diag, stmt->target->loc, "a loop variable must be a number");
    Expr *const bounds[] = {stmt->value, stmt->limit, stmt->step};
    for (int i = 0; i < 3; i++) {
        if (!bounds[i]) continue;
        Type *bound = check_value(s, bounds[i]);
        if (!is_error(bound) && !is_number(bound))
            diag_error(s->diag, bounds[i]->loc, "a loop bound must be a number, not %s",
                       type_name(s->arena, bound));
    }
    long long step;
    if (stmt->step && const_int(stmt->step, &step) && step == 0)
        diag_error(s->diag, stmt->step->loc, "the step cannot be 0");
    check_block(s, stmt->body);
}

static bool is_text(const Type *type) {
    return type->kind == TY_STRING ||
           (type->kind == TY_ARRAY && type->dims.count == 1 && type->base->kind == TY_CHAR) ||
           (type->kind == TY_POINTER && type->base->kind == TY_CHAR);
}

static void check_io(Sema *s, Stmt *stmt) {
    for (int i = 0; i < stmt->args.count; i++) {
        Expr *arg = stmt->args.items[i];
        Type *type = check_value(s, arg);
        if (is_error(type)) continue;
        if (stmt->kind == ST_READ) {
            if (!is_lvalue(arg) || (!is_number(type) && type->kind != TY_STRING))
                diag_error(s->diag, arg->loc, "read needs a variable of a basic type");
        } else if (!is_scalar(type) && !is_text(type)) {
            diag_error(s->diag, arg->loc, "cannot print %s", type_name(s->arena, type));
        }
    }
}

static Class *find_class(Sema *s, const char *name, Loc loc) {
    for (Class *c = s->program->classes; c; c = c->next)
        if (c->is_component && strcmp(c->name, name) == 0) return c;
    diag_error(s->diag, loc, "'%s' is not a component", name);
    return NULL;
}

static void check_send(Sema *s, Stmt *stmt) {
    const Class *klass = NULL;
    if (stmt->to_all_name) {
        stmt->to_all = klass = find_class(s, stmt->to_all_name, stmt->loc);
        if (!klass) return;
    } else {
        Type *type = check_value(s, stmt->target);
        if (is_error(type)) return;
        klass = class_of(type);
        bool someone = type->kind == TY_POINTER && type->base->kind == TY_VOID;
        if (!someone && (!klass || !klass->is_component)) {
            diag_error(s->diag, stmt->target->loc, "messages are sent to components, not to %s",
                       type_name(s->arena, type));
            return;
        }
        if (type->kind != TY_POINTER && !is_lvalue(stmt->target))
            diag_error(s->diag, stmt->target->loc, "only a variable has an address");
    }
    if (stmt->limit) {
        Type *after = check_value(s, stmt->limit);
        if (!is_error(after) && !is_integral(after))
            diag_error(s->diag, stmt->limit->loc, "a time must be an integer, not %s", type_name(s->arena, after));
    }
    const Function *handler = find_method(s->program, klass, stmt->name, true);
    ExprList *args = &stmt->value->args;
    for (int i = 0; i < args->count; i++) check_value(s, args->items[i]);
    if (!handler || !handler->symbol) {
        if (klass) diag_error(s->diag, stmt->value->loc, "'%s' does not handle the message '%s'", klass->name, stmt->name);
        else diag_error(s->diag, stmt->value->loc, "no component handles the message '%s'", stmt->name);
        return;
    }
    stmt->value->symbol = handler->symbol;
    if (args->count != handler->symbol->param_count - 1) {
        int n = handler->symbol->param_count - 1;
        diag_error(s->diag, stmt->value->loc, "'%s' takes %d argument%s, not %d", stmt->name, n, n == 1 ? "" : "s",
                   args->count);
        return;
    }
    for (int i = 0; i < args->count; i++) {
        Type *want = handler->symbol->params[i + 1]->type;
        if (!assignable(want, args->items[i]->type, args->items[i]))
            diag_error(s->diag, args->items[i]->loc, "parameter '%s' of '%s' is %s, not %s",
                       handler->symbol->params[i + 1]->name, stmt->name, type_name(s->arena, want),
                       type_name(s->arena, args->items[i]->type));
    }
}

/* Who sends or gets a message in "expect a -> b: M". The word "test" stands
 * for what is outside of the system. */
static void check_lifeline(Sema *s, Expr *expr) {
    if (expr->kind == EX_NAME && strcmp(expr->text, "test") == 0) {
        bool declared = false;
        for (Scope *scope = s->scope; scope; scope = scope->parent) declared |= find_local(scope, "test") != NULL;
        if (!declared) {
            expr->type = &T_VOID;
            return;
        }
    }
    Type *type = check_value(s, expr);
    if (!is_error(type) && (!class_of(type) || (type->kind != TY_POINTER && !is_lvalue(expr))))
        diag_error(s->diag, expr->loc, "%s is not a component or an object", type_name(s->arena, type));
}

static void check_statement(Sema *s, Stmt *stmt) {
    switch (stmt->kind) {
        case ST_SEND: check_send(s, stmt); break;
        case ST_STOP: case ST_STEP: case ST_TODO: break;
        case ST_CANCEL:
            if (!s->function->function->owner || !find_method(s->program, s->function->function->owner, stmt->name, true))
                diag_error(s->diag, stmt->loc, "no component handles the message '%s'", stmt->name);
            break;
        case ST_CHECK: check_scalar(s, stmt->value, "a condition"); break;
        case ST_RUN:
            if (stmt->value) {
                Type *type = check_value(s, stmt->value);
                if (!is_error(type) && !is_integral(type))
                    diag_error(s->diag, stmt->value->loc, "a time must be an integer, not %s", type_name(s->arena, type));
            }
            break;
        case ST_EXPECT: {
            check_lifeline(s, stmt->target);
            check_lifeline(s, stmt->limit);
            bool known = false;
            for (const Function *f = s->program->functions; f; f = f->next)
                known |= f->owner && strcmp(f->name, stmt->name) == 0;
            if (!known) diag_error(s->diag, stmt->loc, "no component handles the message '%s'", stmt->name);
            break;
        }
        case ST_ASSIGN: check_assignment(s, stmt); break;
        case ST_CALL:
            if (stmt->value->kind == EX_APPLY) {
                stmt->value->type = check_apply(s, stmt->value, false);
            } else {
                if (stmt->value->kind == EX_FIELD) { /* object.method */
                    check_expr(s, stmt->value);
                    if (!is_error(stmt->value->type) && !stmt->value->is_call)
                        diag_error(s->diag, stmt->loc, "this value is not used");
                    break;
                }
                Symbol *sym = lookup(s, stmt->value->text, stmt->value->loc);
                if (!sym && strcmp(stmt->value->text, "stop") == 0 && has_components(s->program)) {
                    stmt->kind = ST_STOP;
                    break;
                }
                if (!sym && !find_builtin(s, stmt->value->text)) {
                    stmt->value->type =
                        check_external_call(s, stmt->value, stmt->value->text, false);
                    break;
                }
                check_expr(s, stmt->value);
            }
            if (!is_error(stmt->value->type) && !stmt->value->is_call)
                diag_error(s->diag, stmt->loc, "this value is not used");
            break;
        case ST_IF:
            for (Stmt *branch = stmt; branch; branch = branch->otherwise) {
                if (branch->kind == ST_IF) check_scalar(s, branch->value, "a condition");
                check_block(s, branch->body);
            }
            break;
        case ST_WHILE:
            check_scalar(s, stmt->value, "a condition");
            check_block(s, stmt->body);
            break;
        case ST_REPEAT:
            check_block(s, stmt->body);
            check_scalar(s, stmt->value, "a condition");
            break;
        case ST_FOR: check_for(s, stmt); break;
        case ST_RETURN: check_return(s, stmt); break;
        case ST_PRINT: case ST_READ: check_io(s, stmt); break;
        case ST_BLOCK: check_block(s, stmt->body); break;
        case ST_VAR: declare_variables(s, stmt->vars); break;
    }
}

static void check_statements(Sema *s, Stmt *stmt) {
    for (; stmt; stmt = stmt->next) check_statement(s, stmt);
}

/* ---- functions ---- */

static VarDecl *find_param(Function *function, const char *name, bool ignore_case) {
    for (VarDecl *param = function->params; param; param = param->next)
        if (ignore_case ? equals_ignore_case(param->name, name) : strcmp(param->name, name) == 0)
            return param;
    return NULL;
}

static VarDecl *find_local_decl(Function *function, const char *name) {
    for (VarDecl *local = function->locals; local; local = local->next)
        if (strcmp(local->name, name) == 0) return local;
    return NULL;
}

/* A parameter (or the function itself) may get its type from the declarations
 * of the body. Moves those out of the list of locals. */
static void take_types_from_body(Sema *s, Function *function) {
    for (VarDecl **link = &function->locals; *link;) {
        VarDecl *local = *link;
        if (strcmp(local->name, function->name) == 0 && !function->is_procedure) {
            if (!function->result) function->result = local->spec;
            *link = local->next;
            continue;
        }
        VarDecl *param = find_param(function, local->name, false);
        if (!param) { /* "N" declared in the body for parameter "n" */
            param = find_param(function, local->name, true);
            if (param && (param->spec || find_local_decl(function, param->name))) param = NULL;
            if (param)
                diag_warning(s->diag, local->loc, "taking '%s' as the parameter '%s'",
                             local->name, param->name);
        }
        if (!param) {
            link = &local->next;
            continue;
        }
        if (param->spec && !param->spec_inherited) {
            diag_warning(s->diag, local->loc, "parameter '%s' already has a type, ignoring this one",
                         param->name);
        } else {
            if (s->config->params == PARAMS_DECL)
                diag_error(s->diag, local->loc, "the type of '%s' belongs in the parameter list "
                           "(see --params)", param->name);
            param->spec = local->spec;
            param->spec_inherited = false;
            param->pointers += local->pointers;
            if (local->has_dims) {
                param->has_dims = true;
                param->dims = local->dims;
            }
        }
        *link = local->next;
    }
}

static void declare_function(Sema *s, Function *function) {
    Symbol *sym;
    if (function->owner || function->is_test) { /* not visible by its name alone */
        for (const Function *f = s->program->functions; f != function && !function->is_handler; f = f->next)
            if (f->owner && f->owner == function->owner && f->is_handler == function->is_handler &&
                strcmp(f->name, function->name) == 0)
                diag_error(s->diag, function->loc, "'%s' is already declared at line %d", function->name, f->loc.line);
        sym = ARENA_NEW(s->arena, Symbol);
        sym->kind = SYM_FUNCTION;
        sym->name = function->name;
        sym->loc = function->loc;
        sym->type = &T_VOID;
        sym->c_name = fresh_name(s, function->is_test ? "test" : arena_printf(s->arena, "%s_%s", function->owner->name, function->name));
    } else {
        sym = declare(s, SYM_FUNCTION, function->name, function->loc, &T_VOID);
        if (sym->function || sym->kind != SYM_FUNCTION) return; /* a duplicate */
    }
    function->symbol = sym;
    sym->function = function;
    if (s->config->params == PARAMS_BODY) {
        for (VarDecl *param = function->params; param; param = param->next)
            if (param->spec && !(function->owner && param == function->params))
                diag_error(s->diag, param->loc, "the type of '%s' belongs in the body (see --params)",
                           param->name);
    }
    take_types_from_body(s, function);
    bool has_main_shape = !function->is_test && strcmp(function->name, "main") == 0 && !function->params;
    if (has_main_shape) sym->c_name = "main";

    for (VarDecl *param = function->params; param; param = param->next) sym->param_count++;
    sym->params = arena_alloc(s->arena, (size_t)(sym->param_count + 1) * sizeof(Symbol *));
}

static bool mentions_later_param(const Expr *expr, const Symbol *param) {
    if (!expr) return false;
    if (expr->kind == EX_NAME && expr->symbol && expr->symbol->kind == SYM_PARAM &&
        expr->symbol->position >= param->position)
        return true;
    if (mentions_later_param(expr->base, param) || mentions_later_param(expr->right, param))
        return true;
    for (int i = 0; i < expr->args.count; i++)
        if (mentions_later_param(expr->args.items[i], param)) return true;
    return false;
}

static bool mentions(const Expr *expr, const Symbol *sym) {
    if (!expr) return false;
    if (expr->kind == EX_NAME && expr->symbol == sym) return true;
    if (mentions(expr->base, sym) || mentions(expr->right, sym)) return true;
    for (int i = 0; i < expr->args.count; i++)
        if (mentions(expr->args.items[i], sym)) return true;
    return false;
}

/* Parameters live in their own scope, kept for when the body is checked. */
static void resolve_signature(Sema *s, Function *function, Scope *scope) {
    Symbol *sym = function->symbol;
    s->scope = scope;
    /* Declared before their types are resolved: the bounds of one parameter may
     * use another that comes later, as in f(A, n). */
    int i = 0;
    for (VarDecl *param = function->params; param; param = param->next, i++) {
        Symbol *p = declare(s, SYM_PARAM, param->name, param->loc, &T_ERROR);
        p->position = i;
        p->mode = param->mode;
        param->symbol = p;
        sym->params[i] = p;
    }
    for (int pass = 0; pass < 2; pass++) { /* scalars first */
        for (VarDecl *param = function->params; param; param = param->next) {
            Symbol *p = param->symbol;
            if (!param->spec) {
                if (pass == 0)
                    diag_error(s->diag, param->loc, "parameter '%s' has no type", param->name);
                continue;
            }
            bool is_array = param->has_dims || param->spec->is_array;
            if (is_array != (pass == 1)) continue;
            p->type = resolve_decl_type(s, param, param->spec, true);
            p->by_reference = !is_array && p->mode != MODE_IN;
            if (!p->by_reference && !is_array) check_concrete(s, p->type, param->loc);
        }
    }
    for (i = 0; i < sym->param_count; i++) {
        Symbol *p = sym->params[i];
        if (p->type->kind != TY_ARRAY) continue;
        for (int d = 0; d < p->type->dims.count; d++) {
            /* again, now that the types of all the parameters are known */
            Dim *dim = &p->type->dims.items[d];
            check_dim_expr(s, dim->lo);
            check_dim_expr(s, dim->hi);
            if (mentions(dim->lo, p) || mentions(dim->hi, p))
                diag_error(s->diag, p->loc, "the bounds of '%s' depend on itself", p->name);
        }
        for (int d = 1; d < p->type->dims.count; d++) {
            Dim *dim = &p->type->dims.items[d];
            if (mentions_later_param(dim->lo, p) || mentions_later_param(dim->hi, p))
                p->flattened = true;
            if (!dim->hi)
                diag_error(s->diag, p->loc, "only the first dimension of '%s' can be left out",
                           p->name);
        }
    }

    if (!function->is_procedure) {
        if (!function->result) {
            diag_error(s->diag, function->loc, "function '%s' has no result type", function->name);
            sym->type = &T_ERROR;
        } else {
            sym->type = resolve_decl_type(s, NULL, function->result, true);
            if (sym->type->kind == TY_ARRAY) {
                diag_error(s->diag, function->loc, "a function cannot return an array; "
                           "use an out parameter");
                sym->type = &T_ERROR;
            }
        }
        Symbol *result = ARENA_NEW(s->arena, Symbol);
        result->kind = SYM_RESULT;
        result->name = function->name;
        result->c_name = fresh_name(s, arena_printf(s->arena, "%s_result", function->name));
        result->loc = function->loc;
        result->type = sym->type;
        sym->result = result;
    }
    s->scope = scope->parent;
}

/* ---- inheritance ---- */

/* The variables of the classes it extends come first, each class once even
 * when it is reached along two ways. */
static void inherit(Sema *s, Class *c) {
    if (c->mark == 2) return;
    if (c->mark == 1) {
        diag_error(s->diag, c->loc, "class '%s' ends up extending itself", c->name);
        return;
    }
    c->mark = 1;
    VarDecl *first = NULL, **tail = &first;
    for (BaseName *b = c->bases; b; b = b->next) {
        Class *base = NULL;
        for (Class *k = s->program->classes; k; k = k->next)
            if (strcmp(k->name, b->name) == 0) base = k;
        for (const BaseName *other = c->bases; other != b && base; other = other->next) {
            if (other->klass != base) continue;
            diag_error(s->diag, b->loc, "'%s' is extended twice", b->name);
            base = NULL;
        }
        if (!base) {
            bool twice = false;
            for (const BaseName *other = c->bases; other != b; other = other->next) twice |= strcmp(other->name, b->name) == 0;
            if (!twice) diag_error(s->diag, b->loc, "there is no class '%s'", b->name);
            continue;
        }
        if (base->is_component || c->is_component) {
            diag_error(s->diag, b->loc, "components do not extend, and are not extended: only classes");
            continue;
        }
        if (c->is_interface && !base->is_interface) {
            diag_error(s->diag, b->loc, "an interface can only extend interfaces, and '%s' is a class", b->name);
            continue;
        }
        inherit(s, base);
        if (base->mark != 2) continue; /* a circle, already reported */
        b->klass = base;
        base->has_derived = true;
        for (const VarDecl *f = base->record->fields; f; f = f->next) {
            const VarDecl *origin = f->origin ? f->origin : f;
            bool have = false;
            for (const VarDecl *g = first; g; g = g->next) have |= g->origin == origin;
            if (have) continue;
            VarDecl *copy = ARENA_NEW(s->arena, VarDecl);
            *copy = *f;
            copy->origin = origin;
            copy->from = f->from ? f->from : base;
            copy->symbol = NULL;
            copy->next = NULL;
            *tail = copy;
            tail = &copy->next;
        }
    }
    if (c->is_interface && c->record->fields)
        diag_error(s->diag, c->record->fields->loc, "an interface has no variables: only what its classes must be able to do");
    *tail = c->record->fields;
    c->record->fields = first;
    c->mark = 2;
}

/* What is abstract and has not been written by this class or one it extends. */
static void find_pending(Sema *s, Class *c) {
    for (const Function *f = s->program->functions; f && !c->pending; f = f->next) {
        if (!f->is_abstract || !f->owner || !derives_from(c, f->owner)) continue;
        const Function *mine = find_method(s->program, c, f->name, false);
        if (mine && mine->is_abstract) c->pending = mine;
    }
}

/* Objects of an abstract class cannot exist: only pointers to them. */
static void check_concrete(Sema *s, const Type *type, Loc loc) {
    while (type->kind == TY_ARRAY) type = type->base;
    const Class *c = type->kind == TY_RECORD ? type->record->klass : NULL;
    if (!c || (!c->is_abstract && !c->pending)) return;
    if (c->pending)
        diag_error(s->diag, loc, "there can be no '%s': nobody has written '%s' (use a class that extends it, or a pointer)",
                   c->name, c->pending->full_name);
    else if (c->is_interface)
        diag_error(s->diag, loc, "there can be no '%s': it is an interface (use a class that extends it, or a pointer)", c->name);
    else
        diag_error(s->diag, loc, "there can be no '%s': it is abstract (use a class that extends it, or a pointer)", c->name);
}

/* A method that replaces one must be usable in its place. */
static void check_overrides(Sema *s, Class *c) {
    for (Function *f = s->program->functions; f; f = f->next) {
        if (f->owner != c || f->is_handler || !f->symbol) continue;
        for (const BaseName *b = c->bases; b; b = b->next) {
            Function *old = b->klass ? (Function *)find_method(s->program, b->klass, f->name, false) : NULL;
            if (!old || !old->symbol) continue;
            f->is_virtual = old->is_virtual = true;
            const Symbol *mine = f->symbol, *theirs = old->symbol;
            bool same = mine->param_count == theirs->param_count && same_type(mine->type, theirs->type);
            for (int i = 1; same && i < mine->param_count; i++)
                same = mine->params[i]->mode == theirs->params[i]->mode && same_type(mine->params[i]->type, theirs->params[i]->type);
            if (!same)
                diag_error(s->diag, f->loc, "'%s' replaces '%s': it must take and give the same", f->full_name, old->full_name);
        }
    }
    /* two classes that give the same thing, and nothing to choose with */
    for (const BaseName *a = c->bases; a; a = a->next) {
        for (const BaseName *b = a->next; b && a->klass; b = b->next) {
            if (!b->klass) continue;
            for (const Function *f = s->program->functions; f; f = f->next) {
                if (!f->owner || f->is_handler || !derives_from(b->klass, f->owner)) continue;
                const Function *one = find_method(s->program, a->klass, f->name, false);
                const Function *two = find_method(s->program, b->klass, f->name, false);
                if (!one || !two || one == two || one->is_abstract || two->is_abstract) continue;
                if (derives_from(one->owner, two->owner) || derives_from(two->owner, one->owner)) continue;
                const Function *mine = find_method(s->program, c, f->name, false);
                if (mine && mine->owner == c) continue;
                diag_error(s->diag, a->loc, "'%s' gets '%s' from both '%s' and '%s': it must have its own", c->name, f->name,
                           one->owner->name, two->owner->name);
                return;
            }
        }
    }
    /* what it leaves abstract makes those that are replaced be chosen by the object too */
    for (Function *f = s->program->functions; f; f = f->next)
        if (f->is_abstract && f->owner == c) f->is_virtual = true;
}

static Scope *class_scope(Sema *s, const Class *klass) {
    int i = 0;
    for (const Class *c = s->program->classes; c && c != klass; c = c->next) i++;
    return s->class_scopes[i];
}

/* Fields, states and methods, as seen from inside the class. */
static void fill_class_scope(Sema *s, Class *klass, Scope *scope) {
    for (const VarDecl *f = klass->record->fields; f; f = f->next) {
        Symbol *sym = ARENA_NEW(s->arena, Symbol);
        sym->kind = SYM_FIELD;
        sym->name = sym->c_name = f->name;
        sym->loc = f->loc;
        sym->field = f->symbol;
        sym->type = f->symbol ? f->symbol->type : &T_ERROR;
        sym->next = scope->symbols;
        scope->symbols = sym;
    }
    int value = 0;
    for (const StateName *state = klass->states; state; state = state->next, value++) {
        if (find_local(scope, state->name))
            diag_error(s->diag, state->loc, "'%s' is already declared at line %d", state->name,
                       find_local(scope, state->name)->loc.line);
        Symbol *sym = ARENA_NEW(s->arena, Symbol);
        sym->kind = SYM_STATE;
        sym->name = sym->c_name = state->name;
        sym->loc = state->loc;
        sym->value = value;
        Type *type = ARENA_NEW(s->arena, Type);
        type->kind = TY_INT;
        type->states = klass;
        sym->type = type;
        sym->next = scope->symbols;
        scope->symbols = sym;
    }
    for (Function *g = s->program->functions; g; g = g->next) {
        if (!g->owner || !derives_from(klass, g->owner) || g->is_handler) continue;
        const Function *f = find_method(s->program, klass, g->name, false); /* its own, or the nearest */
        if (!f || !f->symbol) continue;
        Symbol *copy = ARENA_NEW(s->arena, Symbol);
        *copy = *f->symbol;
        if (find_local(scope, f->name)) continue;
        /* the scope links symbols through `next`: a stand-in that leads to the method */
        copy->next = scope->symbols;
        scope->symbols = copy;
    }
}

static void check_handlers(Sema *s, Class *klass) {
    for (Function *f = s->program->functions; f; f = f->next) {
        if (f->owner != klass || !f->is_handler || !f->symbol) continue;
        for (const StateName *g = f->guards; g; g = g->next) {
            bool known = false;
            for (const StateName *state = klass->states; state; state = state->next)
                known |= strcmp(state->name, g->name) == 0;
            if (!known) diag_error(s->diag, g->loc, "'%s' has no state '%s'", klass->name, g->name);
        }
        for (int i = 1; i < f->symbol->param_count; i++) {
            Symbol *p = f->symbol->params[i];
            if (p->mode != MODE_IN || !is_scalar(p->type))
                if (!is_error(p->type))
                    diag_error(s->diag, p->loc, "a message carries simple values: '%s' cannot be one", p->name);
        }
        /* a message means the same for everybody */
        const Function *first = find_method(s->program, NULL, f->name, true);
        bool same = first->symbol && first->symbol->param_count == f->symbol->param_count;
        for (int i = 1; same && i < f->symbol->param_count; i++)
            same = same_type(first->symbol->params[i]->type, f->symbol->params[i]->type);
        if (!same)
            diag_error(s->diag, f->loc, "the message '%s' has other parameters at line %d", f->name, first->loc.line);
        if (strcmp(f->name, "start") == 0 && f->symbol->param_count > 1)
            diag_error(s->diag, f->loc, "the message 'start' has no parameters");
    }
}

static void check_links(Sema *s) {
    for (Link *link = s->program->links; link; link = link->next) {
        Expr *ends[] = {link->from, link->to}, *numbers[] = {link->delay, link->delay_max, link->loss};
        for (int i = 0; i < 2; i++) {
            Type *type = check_value(s, ends[i]), *base = type;
            while (base->kind == TY_ARRAY) base = base->base;
            const Class *klass = class_of(base);
            if (!is_error(type) && (base->kind == TY_POINTER || !klass || !klass->is_component || !is_lvalue(ends[i])))
                diag_error(s->diag, ends[i]->loc, "a link joins components, not %s", type_name(s->arena, type));
        }
        for (int i = 0; i < 3; i++) {
            if (!numbers[i]) continue;
            Type *type = check_value(s, numbers[i]);
            if (!is_error(type) && !is_integral(type))
                diag_error(s->diag, numbers[i]->loc, "a time must be an integer, not %s", type_name(s->arena, type));
        }
    }
}

bool analyze(Program *program, const Config *config, Arena *arena, Diag *diag) {
    Scope global = {0};
    Sema sema = {.program = program, .config = config, .arena = arena, .diag = diag,
                 .scope = &global, .records_tail = &program->records};
    Sema *s = &sema;

    collect_names(s, program->globals);
    for (TypeDecl *t = program->types; t; t = t->next) {
        use_name(s, t->name);
        collect_names(s, t->record->fields);
    }
    int functions = 0;
    for (Function *f = program->functions; f; f = f->next, functions++) {
        use_name(s, f->name);
        collect_names(s, f->params);
        collect_names(s, f->locals);
        collect_statement_names(s, f->body);
    }

    for (Class *c = program->classes; c; c = c->next) {
        if (!c->is_component || !c->states) continue;
        VarDecl *state = ARENA_NEW(arena, VarDecl); /* its current state */
        state->loc = c->states->loc;
        state->name = "state";
        state->spec = ARENA_NEW(arena, TypeSpec);
        state->spec->base = TOK_INTEGER;
        state->next = c->record->fields;
        c->record->fields = state;
    }
    for (Class *c = program->classes; c; c = c->next) inherit(s, c);
    for (Class *c = program->classes; c; c = c->next) find_pending(s, c);
    for (TypeDecl *t = program->types; t; t = t->next) add_record(s, t->name, t->record, t->loc);
    /* "var origin: record ... end" also names the type of other variables */
    for (VarDecl *g = program->globals; g; g = g->next) {
        if (!g->spec->record || g->spec->record->name) continue;
        bool taken = false;
        for (NamedRecord *r = s->records; r; r = r->next) taken |= strcmp(r->name, g->name) == 0;
        if (!taken) {
            g->spec->record->name = g->name;
            add_record(s, g->name, g->spec->record, g->loc);
        }
    }
    for (TypeDecl *t = program->types; t; t = t->next) resolve_record(s, t->record);
    for (Class *c = program->classes; c; c = c->next) {
        if (!c->is_component || !c->states || !c->record->fields->symbol) continue;
        Type *type = ARENA_NEW(arena, Type);
        type->kind = TY_INT;
        type->states = c;
        c->record->fields->symbol->type = type;
    }

    declare_variables(s, program->globals);
    for (VarDecl *g = program->globals; g; g = g->next)
        if (g->symbol) check_fixed_size(s, g->symbol->type, g->loc, "a global array");

    for (Function *f = program->functions; f; f = f->next) declare_function(s, f);
    Scope *scopes = arena_alloc(arena, (size_t)(functions + 1) * sizeof(Scope));
    int classes = 0;
    for (Class *c = program->classes; c; c = c->next) classes++;
    s->class_scopes = arena_alloc(arena, (size_t)(classes + 1) * sizeof(Scope *));
    for (int k = 0; k < classes; k++) {
        s->class_scopes[k] = ARENA_NEW(arena, Scope);
        s->class_scopes[k]->parent = &global;
    }
    int i = 0;
    for (Function *f = program->functions; f; f = f->next, i++) {
        scopes[i].parent = &global;
        if (f->owner) { /* through the class */
            Class *c = f->owner;
            for (Class *k = program->classes; k; k = k->next)
                if (k == c) scopes[i].parent = class_scope(s, c);
        }
        if (f->symbol) resolve_signature(s, f, &scopes[i]);
    }
    for (Class *c = program->classes; c; c = c->next) {
        fill_class_scope(s, c, class_scope(s, c));
        check_handlers(s, c);
        check_overrides(s, c);
    }
    check_links(s);
    if (program->seed) check_expr(s, program->seed);
    i = 0;
    for (Function *f = program->functions; f; f = f->next, i++) {
        if (!f->symbol) continue;
        s->function = f->symbol;
        s->scope = &scopes[i];
        declare_variables(s, f->locals);
        check_statements(s, f->body);
        s->scope = &global;
    }
    return diag->errors == 0;
}
