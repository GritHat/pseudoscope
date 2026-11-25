#include "pseudoscope/target.h"
#include "pseudoscope/builtins.h"

#include <string.h>

static const struct {
    const char *name, *option, *extension;
    const char *others[4]; /* other ways to ask for it */
} LANGS[LANG_COUNT] = {
    [LANG_C] = {"C", "c", ".c", {"h"}},
    [LANG_CPP] = {"C++", "cpp", ".cpp", {"c++", "cxx", "cc", "hpp"}},
    [LANG_JAVA] = {"Java", "java", ".java", {0}},
    [LANG_RUST] = {"Rust", "rust", ".rs", {"rs"}},
    [LANG_TS] = {"TypeScript", "ts", ".ts", {"typescript"}},
    [LANG_JS] = {"JavaScript", "js", ".js", {"javascript", "mjs", "node"}},
    [LANG_PYTHON] = {"Python", "python", ".py", {"py", "python3"}},
};

const char *lang_name(Lang lang) { return LANGS[lang].name; }
const char *lang_option(Lang lang) { return LANGS[lang].option; }
const char *lang_extension(Lang lang) { return LANGS[lang].extension; }

bool lang_from_name(const char *name, Lang *lang) {
    for (int i = 0; i < LANG_COUNT; i++) {
        bool found = equals_ignore_case(name, LANGS[i].option) || equals_ignore_case(name, LANGS[i].name);
        for (int k = 0; k < 4 && LANGS[i].others[k]; k++) found |= equals_ignore_case(name, LANGS[i].others[k]);
        if (!found) continue;
        *lang = (Lang)i;
        return true;
    }
    return false;
}

bool lang_from_path(const char *path, Lang *lang) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && !strchr(dot, '/') && lang_from_name(dot + 1, lang);
}

/* ---- names that are taken ---- */

static const char *const HELPERS =
    " fail read text random upper lower substring mod token run sim Component Sim Event seed input G SIM SEED INPUT "
    "read_int read_real read_bool read_char read_text readInt readReal readBool readChar readText nextChar firstChar skipBlanks "
    "inputAt copy set receive result ";

static const char *const WORDS[LANG_COUNT] = {
    [LANG_CPP] = " alignas alignof and and_eq asm bitand bitor bool catch class compl concept constexpr const_cast decltype "
                 "delete dynamic_cast explicit export false friend mutable namespace new noexcept not not_eq nullptr operator "
                 "or or_eq private protected public reinterpret_cast requires static_assert static_cast template this throw "
                 "true try typeid typename using virtual wchar_t xor xor_eq std string vector "
                 "time exit abort remove rename signal rand srand index free malloc calloc realloc ",
    [LANG_JAVA] = " abstract assert boolean byte catch class extends final finally implements import instanceof interface "
                  "native new null package private protected public strictfp super synchronized this throw throws transient "
                  "try var record true false String Object Math System Main Integer Double ",
    [LANG_RUST] = " as crate dyn fn impl in let loop match mod move mut pub ref self Self super trait type unsafe use where "
                  "async await abstract become box final macro override priv try typeof unsized virtual yield true false "
                  "i32 f64 bool str usize String Vec Option Some None Box Clone ",
    [LANG_TS] = " catch class debugger delete export extends false finally function import in instanceof new null super "
                "this throw true try typeof var with yield let await async of arguments eval undefined NaN Infinity Object "
                "Array String Number Math console process require type interface any number string boolean never unknown "
                "declare module namespace abstract implements private public protected readonly as fs ",
    [LANG_JS] = " catch class debugger delete export extends false finally function import in instanceof new null super "
                "this throw true try typeof var with yield let await async of arguments eval undefined NaN Infinity Object "
                "Array String Number Math console process require implements interface package private protected public fs ",
    [LANG_PYTHON] = " False None True and as assert async await class def del elif except finally from global import in is "
                    "lambda nonlocal not or pass raise try with yield print len int float str bool list range ord chr abs "
                    "copy math sys self ABC abstractmethod tuple ",
};

static bool listed(const char *list, const char *word) {
    size_t length = strlen(word);
    for (const char *at = list ? strstr(list, word) : NULL; at; at = strstr(at + 1, word))
        if (at > list && at[-1] == ' ' && at[length] == ' ') return true;
    return false;
}

bool lang_reserves(Lang lang, const char *word) {
    if (lang == LANG_C) return false;
    return listed(WORDS[lang], word) || listed(HELPERS, word);
}

/* ---- what a language cannot say ---- */

typedef struct {
    const Program *program;
    Lang lang;
    Diag *diag;
    int problems;
} Check;

static bool objects_are_references(Lang lang) {
    return lang == LANG_JAVA || lang == LANG_JS || lang == LANG_TS || lang == LANG_PYTHON;
}

static void check_type(Check *c, const Type *type, Loc loc, const char *name) {
    while (type && type->kind == TY_ARRAY) type = type->base;
    if (!type || type->kind != TY_POINTER || !objects_are_references(c->lang)) return;
    if (type->base->kind == TY_RECORD || type->base->kind == TY_VOID) return;
    diag_error(c->diag, loc, "%s has no pointers to plain values: '%s' cannot be translated (C, C++ and Rust have them)",
               lang_name(c->lang), name);
    c->problems++;
}

static void check_decls(Check *c, const VarDecl *decl) {
    for (; decl; decl = decl->next)
        if (decl->symbol) check_type(c, decl->symbol->type, decl->loc, decl->name);
}

static bool gives_values_back(const Check *c, const Expr *e) {
    if (!e->is_call || !e->symbol || !e->symbol->function || c->lang == LANG_CPP || c->lang == LANG_C) return false;
    for (int i = 0; i < e->symbol->param_count; i++)
        if (e->symbol->params[i]->by_reference && e->symbol->params[i]->type->kind != TY_RECORD) return true;
    return false;
}

/* `strict`: a place that is evaluated again and again, or maybe not at all. */
static void check_expr(Check *c, const Expr *e, bool strict) {
    if (!e) return;
    if (e->is_call && e->symbol && !e->symbol->function && !e->symbol->builtin && c->lang != LANG_C) {
        diag_error(c->diag, e->loc, "'%s' is not in the file: it cannot be called in %s", e->symbol->name, lang_name(c->lang));
        c->problems++;
    }
    if (e->is_call && e->symbol && e->symbol->builtin) {
        const Builtin *b = builtin_named(e->symbol->builtin);
        if (b && b->here_only) {
            diag_error(c->diag, e->loc, "'%s' comes from a library that only works when the program is run here (exec, ui): it is not translated",
                       e->base && e->base->text ? e->base->text : e->symbol->name);
            c->problems++;
        }
    }
    if (strict && gives_values_back(c, e)) {
        diag_error(c->diag, e->loc, "in %s what '%s' changes comes back with its result: call it in a statement of its own",
                   lang_name(c->lang), e->symbol->name);
        c->problems++;
    }
    if (objects_are_references(c->lang) && e->type && e->type->kind == TY_POINTER && e->kind == EX_BINARY) {
        diag_error(c->diag, e->loc, "%s has no pointers that move from an element to the next", lang_name(c->lang));
        c->problems++;
    }
    bool lazy = e->kind == EX_BINARY && (e->op == TOK_AND || e->op == TOK_OR);
    check_expr(c, e->base, strict);
    check_expr(c, e->right, strict || lazy);
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i], strict);
}

static void check_statements(Check *c, const Stmt *s) {
    for (; s; s = s->next) {
        bool loop = s->kind == ST_WHILE || s->kind == ST_REPEAT;
        check_expr(c, s->target, false);
        check_expr(c, s->value, loop);
        check_expr(c, s->limit, true);
        check_expr(c, s->step, true);
        for (int i = 0; i < s->args.count; i++) check_expr(c, s->args.items[i], false);
        if (s->kind == ST_VAR) check_decls(c, s->vars);
        if (s->kind == ST_EXPECT && s->limit && s->limit->type) {
            const Class *to = class_of(s->limit->type);
            if (!to || !find_method(c->program, to, s->name, true)) {
                diag_error(c->diag, s->loc, "this expects a call between objects, which only the program run here keeps track of (exec, test, ui)");
                c->problems++;
            }
        }
        check_statements(c, s->body);
        for (const Stmt *o = s->otherwise; o; o = o->otherwise) {
            check_expr(c, o->value, true);
            check_statements(c, o->body);
        }
    }
}

static void check_classes(Check *c) {
    for (const Class *k = c->program->classes; k; k = k->next) {
        const BaseName *first = NULL;
        for (const BaseName *b = k->bases; b; b = b->next) {
            if (!b->klass) continue;
            if (c->lang == LANG_C) {
                diag_error(c->diag, b->loc, "C has no classes that extend others: '%s' can be translated to C++, Java, TypeScript or Python",
                           k->name);
                c->problems++;
                return;
            }
            if (c->lang == LANG_RUST && !b->klass->is_interface) {
                diag_error(c->diag, b->loc, "Rust has no classes that extend classes: '%s' could only extend an interface (a trait), and '%s' is not",
                           k->name, b->name);
                c->problems++;
                continue;
            }
            bool single = c->lang == LANG_JAVA || c->lang == LANG_TS || c->lang == LANG_JS;
            if (!single || b->klass->is_interface || k->is_interface) continue;
            if (first) {
                diag_error(c->diag, b->loc, "in %s a class extends one class only: '%s' has both '%s' and '%s' (C++ and Python allow it; or make one an interface)",
                           lang_name(c->lang), k->name, first->name, b->name);
                c->problems++;
            }
            first = b;
        }
        if (c->lang == LANG_C && (k->is_abstract || k->is_interface)) {
            diag_error(c->diag, k->loc, "C has no abstract classes or interfaces: '%s' can be translated to C++, Java, TypeScript or Python", k->name);
            c->problems++;
        }
    }
}

bool translatable(const Program *program, const Config *config, Lang lang, Diag *diag) {
    (void)config;
    Check c = {.program = program, .lang = lang, .diag = diag};
    check_classes(&c);
    if (lang == LANG_C) { /* its own generator: only what it cannot say is looked for */
        for (const Function *f = program->functions; f; f = f->next) check_statements(&c, f->body);
        return c.problems == 0;
    }
    check_decls(&c, program->globals);
    for (const Record *r = program->records; r; r = r->next) check_decls(&c, r->fields);
    for (const Function *f = program->functions; f; f = f->next) {
        check_decls(&c, f->params);
        check_decls(&c, f->locals);
        if (f->symbol) check_type(&c, f->symbol->type, f->loc, f->name);
        check_statements(&c, f->body);
    }
    return c.problems == 0;
}

/* ---- one door for all the languages ---- */

void translate_program(const Program *program, const Config *config, Arena *arena, Lang lang, const char *name, StrBuf *out) {
    if (lang == LANG_C) generate(program, config, arena, out);
    else emit_program(program, config, arena, lang, name, out);
}

void translate_function(const Program *program, const Config *config, Arena *arena, Lang lang,
                        const Function *function, StrBuf *out, LineMap *map) {
    if (lang == LANG_C) generate_function(program, config, arena, function, out, map);
    else emit_function(program, config, arena, lang, function, out, map);
}

void translate_record(const Program *program, const Config *config, Arena *arena, Lang lang,
                      const Record *record, StrBuf *out) {
    if (lang == LANG_C) generate_record(program, config, arena, record, out);
    else emit_record(program, config, arena, lang, record, out);
}
