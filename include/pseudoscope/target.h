#ifndef PSEUDOSCOPE_TARGET_H
#define PSEUDOSCOPE_TARGET_H

#include "pseudoscope/codegen.h"

/* The languages a program can be translated to. */
typedef enum { LANG_C, LANG_CPP, LANG_JAVA, LANG_RUST, LANG_TS, LANG_JS, LANG_PYTHON, LANG_COUNT } Lang;

const char *lang_name(Lang lang);       /* "C++" */
const char *lang_option(Lang lang);     /* "cpp": for --to */
const char *lang_extension(Lang lang);  /* ".cpp" */
bool lang_from_name(const char *name, Lang *lang);
bool lang_from_path(const char *path, Lang *lang); /* by the extension of the output */

/* What the language cannot say goes to diag. Returns whether the program can
 * be translated to it. */
bool translatable(const Program *program, const Config *config, Lang lang, Diag *diag);

/* `name` is that of the class to write, for the languages that want one. */
void translate_program(const Program *program, const Config *config, Arena *arena, Lang lang, const char *name, StrBuf *out);
void translate_function(const Program *program, const Config *config, Arena *arena, Lang lang,
                        const Function *function, StrBuf *out, LineMap *map);
void translate_record(const Program *program, const Config *config, Arena *arena, Lang lang,
                      const Record *record, StrBuf *out);

/* The generator of the other languages (emit.c). */
void emit_program(const Program *program, const Config *config, Arena *arena, Lang lang, const char *name, StrBuf *out);
void emit_function(const Program *program, const Config *config, Arena *arena, Lang lang,
                   const Function *function, StrBuf *out, LineMap *map);
void emit_record(const Program *program, const Config *config, Arena *arena, Lang lang, const Record *record, StrBuf *out);
/* A word that cannot be a name in the language. */
bool lang_reserves(Lang lang, const char *word);

/* What a translation must bring along (runtime.c): `needs` is a set of N_*. */
void emit_prelude(Lang lang, unsigned needs, long long seed, StrBuf *out);
enum {
    N_MOD = 1 << 0, N_TEXT = 1 << 1, N_INPUT = 1 << 2, N_RANDOM = 1 << 3, N_MATH = 1 << 4, N_FIXED = 1 << 5,
    N_LOCALE = 1 << 6, N_COPY = 1 << 7, N_CASE = 1 << 8, N_SIM = 1 << 9, N_ABC = 1 << 10, N_FAIL = 1 << 11,
    N_DIV = 1 << 12, N_TESTS = 1 << 13, N_GLOBALS = 1 << 14, N_VECTOR = 1 << 15, N_STRING = 1 << 16, N_PROCESS = 1 << 17
};

#endif
