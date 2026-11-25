#include "plike/arena.h"
#include "plike/diag.h"
#include "unit.h"

#include <stdint.h>
#include <stdlib.h>

static void test_arena(void) {
    Arena arena = {0};
    char *small = arena_alloc(&arena, 3);
    char *large = arena_alloc(&arena, 200000);
    CHECK((uintptr_t)small % _Alignof(max_align_t) == 0);
    CHECK((uintptr_t)large % _Alignof(max_align_t) == 0);
    CHECK(large[0] == 0 && large[199999] == 0);
    CHECK_STR(arena_strndup(&arena, "abcdef", 3), "abc");
    CHECK_STR(arena_printf(&arena, "%s_%d", "x", 42), "x_42");
    arena_free(&arena);
    CHECK(arena.head == NULL);
}

static void test_strbuf(void) {
    StrBuf buffer = {0};
    for (int i = 0; i < 1000; i++) strbuf_printf(&buffer, "%d,", i % 10);
    CHECK(buffer.length == 2000);
    CHECK(strlen(buffer.data) == 2000);
    strbuf_free(&buffer);
    CHECK(buffer.data == NULL);
}

static void test_diag(void) {
    Source source = {"t.plike", "ab\n\tcd\n", 7};
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    Diag diag = {.source = &source, .out = out};
    diag_error(&diag, (Loc){2, 3}, "bad '%s'", "d");
    diag_error(&diag, (Loc){2, 3}, "same place, must be dropped");
    diag_warning(&diag, (Loc){0, 0}, "general");
    fclose(out);
    CHECK_STR(text, "t.plike:2:3: error: bad 'd'\n    2 | \tcd\n      | \t ^\n"
                    "t.plike: warning: general\n");
    CHECK(diag.errors == 1 && diag.warnings == 1);
    free(text);

    out = open_memstream(&text, &size);
    diag = (Diag){.source = &source, .out = out};
    for (int i = 1; i < 100; i++) diag_error(&diag, (Loc){1, i}, "e");
    fclose(out);
    CHECK(diag.errors == DIAG_MAX_ERRORS && diag.too_many);
    free(text);
}

/* The conversions of a format, without widths and precisions. */
static void conversions(const char *format, char *out) {
    for (; *format; format++) {
        if (*format != '%') continue;
        format++;
        if (*format == '%') continue;
        while (strchr(".0123456789*", *format)) {
            if (*format == '*') *out++ = '*';
            format++;
        }
        *out++ = *format;
    }
    *out = '\0';
}

static void test_italian(void) {
    for (int i = 0; i < ITALIAN_MESSAGE_COUNT; i++) {
        char english[32], italian[32];
        conversions(ITALIAN_MESSAGES[i][0], english);
        conversions(ITALIAN_MESSAGES[i][1], italian);
        CHECK_STR(italian, english);
    }
    Diag diag = {.italian = true};
    CHECK_STR(diag_text(&diag, "unknown type '%s'"), "il tipo '%s' non esiste");
    diag.italian = false;
    CHECK_STR(diag_text(&diag, "unknown type '%s'"), "unknown type '%s'");
}

void test_util(void) {
    test_italian();
    test_arena();
    test_strbuf();
    test_diag();
}
