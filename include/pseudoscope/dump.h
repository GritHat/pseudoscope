#ifndef PSEUDOSCOPE_DUMP_H
#define PSEUDOSCOPE_DUMP_H

#include "pseudoscope/ast.h"

void dump_tokens(TokenList tokens, StrBuf *out);
void dump_tree(const Program *program, StrBuf *out);     /* indented text */
void dump_tree_dot(const Program *program, StrBuf *out); /* Graphviz */

#endif
