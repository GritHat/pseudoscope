#ifndef PSEUDOSCOPE_PARSER_H
#define PSEUDOSCOPE_PARSER_H

#include "pseudoscope/ast.h"

/* Builds the tree for a whole file. Syntax errors go to diag; a tree is
 * returned anyway, but is only meaningful when there were none. */
Program *parse(TokenList tokens, const Config *config, Arena *arena, Diag *diag);

#endif
