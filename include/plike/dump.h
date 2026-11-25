#ifndef PLIKE_DUMP_H
#define PLIKE_DUMP_H

#include "plike/ast.h"

void dump_tokens(TokenList tokens, StrBuf *out);
void dump_tree(const Program *program, StrBuf *out);     /* indented text */
void dump_tree_dot(const Program *program, StrBuf *out); /* Graphviz */

#endif
