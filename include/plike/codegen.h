#ifndef PLIKE_CODEGEN_H
#define PLIKE_CODEGEN_H

#include "plike/sema.h"

/* Writes the C translation of an analysed program. */
void generate(const Program *program, const Config *config, Arena *arena, StrBuf *out);

/* Whether the output has a main function. */
bool has_main(const Program *program);

#endif
