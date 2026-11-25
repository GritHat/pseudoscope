#include "plike/arena.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 65536

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t capacity;
    alignas(max_align_t) unsigned char data[];
};

static void out_of_memory(void) {
    fputs("plike: out of memory\n", stderr);
    exit(3);
}

void *arena_alloc(Arena *arena, size_t size) {
    const size_t alignment = alignof(max_align_t);
    if (size > SIZE_MAX / 2) out_of_memory();
    size = (size + alignment - 1) / alignment * alignment;

    if (!arena->head || arena->head->capacity - arena->head->used < size) {
        size_t capacity = size > BLOCK_SIZE ? size : BLOCK_SIZE;
        ArenaBlock *block = malloc(sizeof(ArenaBlock) + capacity);
        if (!block) out_of_memory();
        block->next = arena->head;
        block->used = 0;
        block->capacity = capacity;
        arena->head = block;
    }
    void *result = arena->head->data + arena->head->used;
    arena->head->used += size;
    return memset(result, 0, size);
}

char *arena_strndup(Arena *arena, const char *text, size_t length) {
    char *copy = arena_alloc(arena, length + 1);
    memcpy(copy, text, length);
    return copy;
}

char *arena_strdup(Arena *arena, const char *text) {
    return arena_strndup(arena, text, strlen(text));
}

char *arena_printf(Arena *arena, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0) out_of_memory();

    char *text = arena_alloc(arena, (size_t)length + 1);
    va_start(args, format);
    vsnprintf(text, (size_t)length + 1, format, args);
    va_end(args);
    return text;
}

void arena_free(Arena *arena) {
    while (arena->head) {
        ArenaBlock *next = arena->head->next;
        free(arena->head);
        arena->head = next;
    }
}

static void reserve(StrBuf *buffer, size_t extra) {
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < needed) capacity *= 2;
    char *data = realloc(buffer->data, capacity);
    if (!data) out_of_memory();
    buffer->data = data;
    buffer->capacity = capacity;
}

void strbuf_append(StrBuf *buffer, const char *text, size_t length) {
    reserve(buffer, length);
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void strbuf_puts(StrBuf *buffer, const char *text) {
    strbuf_append(buffer, text, strlen(text));
}

void strbuf_printf(StrBuf *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0) out_of_memory();

    reserve(buffer, (size_t)length);
    va_start(args, format);
    vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, args);
    va_end(args);
    buffer->length += (size_t)length;
}

void strbuf_free(StrBuf *buffer) {
    free(buffer->data);
    *buffer = (StrBuf){0};
}
