#include "pseudoscope/lexer.h"
#include "unit.h"

#include <stdlib.h>

/* Lexes text and renders it as "kind:text" separated by blanks. */
static char *render(const char *text, Config config, int *errors) {
    Arena arena = {0};
    Source source = {"t", text, strlen(text), NULL};
    FILE *sink = fopen("/dev/null", "w");
    Diag diag = {.source = &source, .out = sink};
    TokenList tokens = lex(&source, &config, &arena, &diag);
    StrBuf out = {0};
    strbuf_puts(&out, "");
    for (int i = 0; i + 1 < tokens.count; i++)
        strbuf_printf(&out, "%s%d:%s", i ? " " : "", tokens.items[i].kind,
                      tokens.items[i].text);
    CHECK(tokens.items[tokens.count - 1].kind == TOK_EOF);
    *errors = diag.errors;
    fclose(sink);
    arena_free(&arena);
    return out.data;
}

static void expect(const char *text, Config config, int want_errors, int n,
                   const TokenKind *kinds, int line) {
    Arena arena = {0};
    Source source = {"t", text, strlen(text), NULL};
    FILE *sink = fopen("/dev/null", "w");
    Diag diag = {.source = &source, .out = sink};
    TokenList tokens = lex(&source, &config, &arena, &diag);
    unit_checks++;
    bool ok = tokens.count == n + 1 && diag.errors == want_errors;
    for (int i = 0; ok && i < n; i++) ok = tokens.items[i].kind == kinds[i];
    if (!ok) {
        unit_failures++;
        fprintf(stderr, "%s:%d: \"%s\" -> %d tokens, %d errors:", __FILE__, line, text,
                tokens.count - 1, diag.errors);
        for (int i = 0; i + 1 < tokens.count; i++)
            fprintf(stderr, " %s", token_kind_name(tokens.items[i].kind));
        fputc('\n', stderr);
    }
    fclose(sink);
    arena_free(&arena);
}

#define KINDS(...) (sizeof((TokenKind[]){__VA_ARGS__}) / sizeof(TokenKind)), (TokenKind[]){__VA_ARGS__}
#define OK(text, ...) expect(text, CONFIG_DEFAULT, 0, KINDS(__VA_ARGS__), __LINE__)
#define BAD(text, ...) expect(text, CONFIG_DEFAULT, 1, KINDS(__VA_ARGS__), __LINE__)

void test_lexer(void) {
    /* keywords in any case, identifiers untouched */
    OK("IF If endIF iffy", TOK_IF, TOK_IF, TOK_ENDIF, TOK_IDENT);
    OK("in/out IN/OUT in / out inout", TOK_INOUT, TOK_INOUT, TOK_INOUT, TOK_INOUT);
    OK("in/outer", TOK_IN, TOK_SLASH, TOK_IDENT);

    /* dotted operators need no blanks around them */
    OK("a.and.b", TOK_IDENT, TOK_AND, TOK_IDENT);
    OK("1.eq.2", TOK_INT, TOK_EQ, TOK_INT);
    OK("x .NOT. .TRUE.", TOK_IDENT, TOK_NOT, TOK_TRUE);
    OK("r.andy", TOK_IDENT, TOK_DOT, TOK_IDENT);
    OK("a mod b eq c notequal d <> e", TOK_IDENT, TOK_MOD, TOK_IDENT, TOK_EQ,
       TOK_IDENT, TOK_NE, TOK_IDENT, TOK_NE, TOK_IDENT);

    /* numbers */
    OK("1..n", TOK_INT, TOK_DOTDOT, TOK_IDENT);
    OK("0. 1.5 2.f 1e5 1.5E-3", TOK_FLOAT, TOK_FLOAT, TOK_FLOAT, TOK_FLOAT, TOK_FLOAT);
    OK("0xFF 0o17 0b101 017", TOK_INT, TOK_INT, TOK_INT, TOK_INT);
    OK("2d 3D 10d", TOK_DIM, TOK_DIM, TOK_DIM);
    BAD("0b12", TOK_INT);
    BAD("0x", TOK_INT);
    BAD("12abc", TOK_INT);
    BAD("089", TOK_INT);

    /* strings and characters */
    OK("\"a\\\"b\" 'c' '\\n'", TOK_STRING, TOK_CHAR, TOK_CHAR);
    BAD("\"abc\nx", TOK_IDENT);
    BAD("'ab'", TOK_CHAR);
    BAD("\"\\q\"", TOK_STRING);

    /* comments, stray characters */
    OK("a // b\n/* c\n */ d", TOK_IDENT, TOK_IDENT);
    BAD("a /* b", TOK_IDENT);
    BAD("a $ b", TOK_IDENT, TOK_IDENT);
    BAD("a \xE2\x89\xA0 b", TOK_IDENT, TOK_IDENT);
    OK("\xEF\xBB\xBFvar", TOK_VAR);

    /* configuration */
    Config equals = CONFIG_DEFAULT;
    equals.assign = ASSIGN_EQUALS;
    expect("a = b == c := d", equals, 0, KINDS(TOK_IDENT, TOK_ASSIGN, TOK_IDENT,
           TOK_EQ, TOK_IDENT, TOK_ASSIGN, TOK_IDENT), __LINE__);
    Config dotted = CONFIG_DEFAULT;
    dotted.operators = OPS_DOTTED;
    expect("and .and.", dotted, 0, KINDS(TOK_IDENT, TOK_AND), __LINE__);
    Config standard = CONFIG_DEFAULT;
    standard.operators = OPS_STANDARD;
    expect("and a.or", standard, 0, KINDS(TOK_AND, TOK_IDENT, TOK_DOT, TOK_OR), __LINE__);

    int errors;
    char *text = render("\"x\\ty\" Foo", CONFIG_DEFAULT, &errors);
    CHECK(strstr(text, ":x\\ty ") && strstr(text, ":Foo"));
    free(text);

    /* positions */
    Arena arena = {0};
    Config config = CONFIG_DEFAULT;
    Source source = {"t", "a\n  b c", 7, NULL};
    Diag diag = {.source = &source, .out = stderr};
    TokenList tokens = lex(&source, &config, &arena, &diag);
    CHECK(tokens.items[1].loc.line == 2 && tokens.items[1].loc.column == 3);
    CHECK(tokens.items[1].starts_line && !tokens.items[2].starts_line);
    arena_free(&arena);
}
