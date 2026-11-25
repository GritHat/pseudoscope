#ifndef PLIKE_ARENA_H
#define PLIKE_ARENA_H

#include <stdarg.h>
#include <stddef.h>

/* Bump allocator: everything built while translating one file lives here and
 * is released at once. Allocation failure terminates the process. */
typedef struct ArenaBlock ArenaBlock;

typedef struct {
    ArenaBlock *head;
} Arena;

void *arena_alloc(Arena *arena, size_t size); /* zero-initialised */
char *arena_strndup(Arena *arena, const char *text, size_t length);
char *arena_strdup(Arena *arena, const char *text);
char *arena_printf(Arena *arena, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void arena_free(Arena *arena);

#define ARENA_NEW(arena, type) ((type *)arena_alloc((arena), sizeof(type)))

/* Growable text buffer, always NUL-terminated once written to. */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StrBuf;

void strbuf_append(StrBuf *buffer, const char *text, size_t length);
void strbuf_puts(StrBuf *buffer, const char *text);
void strbuf_printf(StrBuf *buffer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void strbuf_free(StrBuf *buffer);

#endif
