#ifndef PSEUDOSCOPE_BLOCKS_H
#define PSEUDOSCOPE_BLOCKS_H

#include "pseudoscope/uml.h"
#include "pseudoscope/config.h"

/* Documents that hold programs and diagrams among their text: Markdown, with
 * its fenced blocks, and LaTeX, with its environments. */

typedef struct {
    bool diagram;           /* else a program */
    Dialect dialect;        /* of a program */
    const char *language;   /* as the document calls it: "mermaid", "algorithmic"... */
    const char *title;      /* the heading it is under, or its caption */
    int line, lines;        /* where it begins, and how long it is */
    const char *text;       /* the document with everything else blanked: lines keep their numbers */
    bool rewritten;         /* the text is ours, not the writer's: LaTeX said the way we read it */
} DocBlock;

typedef struct {
    DocBlock *items;
    int count, capacity;
    Uml *outline;           /* its headings, as a tree; NULL when it has none */
    bool latex;
} Blocks;

bool is_document_path(const char *path);
Blocks *blocks_read(const Source *source, Arena *arena);

/* "notes.md#3": copies the path to `file` and gives the number, 0 when none. */
int block_number(const char *path, char *file, size_t size);

/* The pseudocode a text without a telling name is written in. */
Dialect guess_dialect(const char *text);

/* What a file is: by its name, and by its text (which may be NULL) when the
 * name does not tell. Files need no extension. */
typedef enum { FILE_PROGRAM, FILE_DIAGRAM, FILE_DOCUMENT, FILE_OTHER } FileKind;
FileKind file_kind(const char *path, const char *text);
bool is_program_path(const char *path);

/* The settings for a file, guessing its pseudocode when its name does not say. */
Config config_for_text(Config config, const char *path, const char *text);

#endif
