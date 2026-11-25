#ifndef PSEUDOSCOPE_CODEGEN_H
#define PSEUDOSCOPE_CODEGEN_H

#include "pseudoscope/sema.h"

/* Writes the C translation of an analysed program. */
void generate(const Program *program, const Config *config, Arena *arena, StrBuf *out);

/* C lines of one function, and the statement each one comes from. */
typedef struct {
    const Stmt *stmt;
    int first, last; /* lines from 0 */
} LineSpan;

typedef struct {
    LineSpan *items;
    int count, capacity;
} LineMap;

void generate_function(const Program *program, const Config *config, Arena *arena,
                       const Function *function, StrBuf *out, LineMap *map);

/* The C declaration of a record. */
void generate_record(const Program *program, const Config *config, Arena *arena,
                     const Record *record, StrBuf *out);

/* Whether the output has a main function. */
bool has_main(const Program *program);

#endif
