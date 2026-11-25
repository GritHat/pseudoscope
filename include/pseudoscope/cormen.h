#ifndef PSEUDOSCOPE_CORMEN_H
#define PSEUDOSCOPE_CORMEN_H

#include "pseudoscope/config.h"
#include "pseudoscope/diag.h"
#include "pseudoscope/arena.h"

/* Pseudocode as in "Introduction to Algorithms" by Cormen, Leiserson, Rivest
 * and Stein: procedures without declarations, blocks by indentation.
 *
 * It is run in a rewriting of ours, line for line, in the words of another
 * pseudocode that has no declarations either. This gives the source to read
 * and changes the settings to read it with; what is shown, and quoted in the
 * messages, stays what was written. */
Source cormen_source(const Source *written, Config *config, Arena *arena, Diag *diag);

/* Does a text without a name look like it? */
bool looks_like_cormen(const char *text);

#endif
