#include "plike/lexer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    TokenKind kind;
} Keyword;

static const Keyword COMMON[] = {
    {"function", TOK_FUNCTION}, {"procedure", TOK_PROCEDURE},
    {"endfunction", TOK_ENDFUNCTION}, {"endprocedure", TOK_ENDPROCEDURE},
    {"var", TOK_VAR}, {"type", TOK_TYPE}, {"record", TOK_RECORD},
    {"begin", TOK_BEGIN}, {"end", TOK_END},
    {"if", TOK_IF}, {"then", TOK_THEN}, {"elseif", TOK_ELSEIF},
    {"else", TOK_ELSE}, {"endif", TOK_ENDIF},
    {"while", TOK_WHILE}, {"do", TOK_DO}, {"endwhile", TOK_ENDWHILE},
    {"for", TOK_FOR}, {"to", TOK_TO}, {"step", TOK_STEP}, {"endfor", TOK_ENDFOR},
    {"repeat", TOK_REPEAT}, {"until", TOK_UNTIL}, {"return", TOK_RETURN},
    {"in", TOK_IN}, {"out", TOK_OUT}, {"inout", TOK_INOUT},
    {"print", TOK_PRINT}, {"read", TOK_READ},
    {"integer", TOK_INTEGER}, {"real", TOK_REAL}, {"logical", TOK_LOGICAL},
    {"character", TOK_CHARACTER}, {"array", TOK_ARRAY}, {"of", TOK_OF},
};

/* Written bare ("and") in the standard style, dotted (".and.") in the other. */
static const Keyword OPERATORS[] = {
    {"and", TOK_AND}, {"or", TOK_OR}, {"not", TOK_NOT}, {"mod", TOK_MOD},
    {"eq", TOK_EQ}, {"equal", TOK_EQ}, {"equals", TOK_EQ},
    {"ne", TOK_NE}, {"notequal", TOK_NE}, {"notequals", TOK_NE},
    {"true", TOK_TRUE}, {"false", TOK_FALSE},
};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    const Source *source;
    const Config *config;
    Arena *arena;
    Diag *diag;
    size_t pos;
    int line;
    size_t line_start;
    bool fresh_line;
    Token *items;
    int count, capacity;
} Lexer;

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (lower(c) >= 'a' && lower(c) <= 'z') || c == '_';
}
static bool is_word(char c) { return is_alpha(c) || is_digit(c); }
static bool is_hex(char c) {
    return is_digit(c) || (lower(c) >= 'a' && lower(c) <= 'f');
}

bool equals_ignore_case(const char *a, const char *b) {
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return lower(*a) == lower(*b);
}

static char at(const Lexer *lx, size_t offset) {
    size_t index = lx->pos + offset;
    return index < lx->source->length ? lx->source->text[index] : '\0';
}

static bool at_end(const Lexer *lx) { return lx->pos >= lx->source->length; }

static Loc here(const Lexer *lx) {
    return (Loc){lx->line, (int)(lx->pos - lx->line_start) + 1};
}

static void newline(Lexer *lx) { /* pos is just past the '\n' */
    lx->line++;
    lx->line_start = lx->pos;
    lx->fresh_line = true;
}

static void push(Lexer *lx, TokenKind kind, Loc loc, const char *text, size_t length) {
    if (lx->count == lx->capacity) {
        int capacity = lx->capacity ? lx->capacity * 2 : 256;
        Token *items = arena_alloc(lx->arena, (size_t)capacity * sizeof(Token));
        if (lx->count) memcpy(items, lx->items, (size_t)lx->count * sizeof(Token));
        lx->items = items;
        lx->capacity = capacity;
    }
    lx->items[lx->count++] = (Token){
        kind, loc, arena_strndup(lx->arena, text, length), lx->fresh_line};
    lx->fresh_line = false;
}

static void take(Lexer *lx, TokenKind kind, size_t length) {
    push(lx, kind, here(lx), lx->source->text + lx->pos, length);
    lx->pos += length;
}

static bool word_is(const char *text, size_t length, const char *word) {
    if (strlen(word) != length) return false;
    for (size_t i = 0; i < length; i++)
        if (lower(text[i]) != word[i]) return false;
    return true;
}

static void skip_blanks(Lexer *lx) {
    while (!at_end(lx)) {
        char c = at(lx, 0);
        if (c == '\n') {
            lx->pos++;
            newline(lx);
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            lx->pos++;
        } else if (c == '/' && at(lx, 1) == '/') {
            while (!at_end(lx) && at(lx, 0) != '\n') lx->pos++;
        } else if (c == '/' && at(lx, 1) == '*') {
            Loc start = here(lx);
            lx->pos += 2;
            while (!at_end(lx) && !(at(lx, 0) == '*' && at(lx, 1) == '/')) {
                if (at(lx, 0) == '\n') {
                    lx->pos++;
                    newline(lx);
                } else {
                    lx->pos++;
                }
            }
            if (at_end(lx)) diag_error(lx->diag, start, "comment is never closed");
            else lx->pos += 2;
        } else {
            return;
        }
    }
}

static void lex_word(Lexer *lx) {
    const char *text = lx->source->text + lx->pos;
    size_t length = 1;
    while (is_word(at(lx, length))) length++;

    /* "in/out", with or without blanks around the slash. */
    if (word_is(text, length, "in")) {
        size_t i = length;
        while (at(lx, i) == ' ' || at(lx, i) == '\t') i++;
        if (at(lx, i) == '/') {
            i++;
            while (at(lx, i) == ' ' || at(lx, i) == '\t') i++;
            if (lx->pos + i + 3 <= lx->source->length &&
                word_is(text + i, 3, "out") && !is_word(at(lx, i + 3))) {
                take(lx, TOK_INOUT, i + 3);
                return;
            }
        }
    }
    for (size_t i = 0; i < COUNT(COMMON); i++) {
        if (word_is(text, length, COMMON[i].text)) {
            take(lx, COMMON[i].kind, length);
            return;
        }
    }
    if (lx->config->operators != OPS_DOTTED) {
        for (size_t i = 0; i < COUNT(OPERATORS); i++) {
            if (word_is(text, length, OPERATORS[i].text)) {
                take(lx, OPERATORS[i].kind, length);
                return;
            }
        }
    }
    take(lx, TOK_IDENT, length);
}

/* ".and." and friends, at the current dot. Returns its length or 0. */
static size_t dotted_operator(const Lexer *lx, size_t offset, TokenKind *kind) {
    if (lx->config->operators == OPS_STANDARD || at(lx, offset) != '.') return 0;
    size_t length = 1;
    while (is_alpha(at(lx, offset + length))) length++;
    if (length == 1 || at(lx, offset + length) != '.') return 0;
    const char *word = lx->source->text + lx->pos + offset + 1;
    for (size_t i = 0; i < COUNT(OPERATORS); i++) {
        if (word_is(word, length - 1, OPERATORS[i].text)) {
            *kind = OPERATORS[i].kind;
            return length + 1;
        }
    }
    return 0;
}

static void lex_number(Lexer *lx) {
    Loc loc = here(lx);
    char prefix = lower(at(lx, 1));
    size_t length = 0;
    bool real = false;

    if (at(lx, 0) == '0' && (prefix == 'x' || prefix == 'o' || prefix == 'b')) {
        length = 2;
        while (prefix == 'x'   ? is_hex(at(lx, length))
               : prefix == 'o' ? at(lx, length) >= '0' && at(lx, length) <= '7'
                               : at(lx, length) == '0' || at(lx, length) == '1')
            length++;
        if (length == 2 || is_word(at(lx, length))) {
            while (is_word(at(lx, length))) length++;
            diag_error(lx->diag, loc, "invalid number '%.*s'", (int)length,
                       lx->source->text + lx->pos);
        }
        take(lx, TOK_INT, length);
        return;
    }

    while (is_digit(at(lx, length))) length++;

    if (lower(at(lx, length)) == 'd' && !is_word(at(lx, length + 1))) {
        take(lx, TOK_DIM, length + 1); /* the "2d" of "2d array" */
        return;
    }

    TokenKind ignored;
    if (at(lx, length) == '.' && at(lx, length + 1) != '.' &&
        !dotted_operator(lx, length, &ignored)) {
        real = true;
        length++;
        while (is_digit(at(lx, length))) length++;
    }
    if (lower(at(lx, length)) == 'e') {
        size_t exponent = length + 1;
        if (at(lx, exponent) == '+' || at(lx, exponent) == '-') exponent++;
        if (is_digit(at(lx, exponent))) {
            real = true;
            while (is_digit(at(lx, exponent))) exponent++;
            length = exponent;
        }
    }
    if (lower(at(lx, length)) == 'f') {
        real = true;
        length++;
    }
    if (is_word(at(lx, length))) {
        while (is_word(at(lx, length))) length++;
        diag_error(lx->diag, loc, "invalid number '%.*s'", (int)length,
                   lx->source->text + lx->pos);
    } else if (!real && length > 1 && at(lx, 0) == '0') {
        for (size_t i = 0; i < length; i++) {
            if (at(lx, i) > '7') {
                diag_error(lx->diag, loc,
                           "'%.*s' starts with 0, so it is octal, but has the digit %c",
                           (int)length, lx->source->text + lx->pos, at(lx, i));
                break;
            }
        }
    }
    take(lx, real ? TOK_FLOAT : TOK_INT, length);
}

/* Length of the escape sequence at offset (after the backslash), 0 if invalid. */
static size_t escape_length(const Lexer *lx, size_t offset) {
    char c = at(lx, offset);
    if (c != '\0' && strchr("ntrabfv0\\'\"", c)) return 1;
    return 0;
}

static void lex_quoted(Lexer *lx, char quote) {
    Loc loc = here(lx);
    size_t length = 1;
    int characters = 0;
    for (;;) {
        char c = at(lx, length);
        if (lx->pos + length >= lx->source->length || c == '\n') {
            diag_error(lx->diag, loc, "unterminated %s",
                       quote == '"' ? "string" : "character");
            lx->pos += length;
            return;
        }
        if (c == quote) break;
        if (c == '\\') {
            size_t escape = escape_length(lx, length + 1);
            if (!escape) {
                Loc bad = {loc.line, loc.column + (int)length};
                diag_error(lx->diag, bad, "unknown escape sequence");
            }
            length += escape;
        }
        length++;
        characters++;
    }
    if (quote == '\'' && characters != 1)
        diag_error(lx->diag, loc, "a character literal holds exactly one character");
    push(lx, quote == '"' ? TOK_STRING : TOK_CHAR, loc,
         lx->source->text + lx->pos + 1, length - 1);
    lx->pos += length + 1;
}

static void lex_symbol(Lexer *lx) {
    static const Keyword SYMBOLS[] = { /* longest first */
        {":=", TOK_ASSIGN}, {"==", TOK_EQ}, {"!=", TOK_NE}, {"<>", TOK_NE},
        {"<=", TOK_LE}, {">=", TOK_GE}, {"<<", TOK_SHL}, {">>", TOK_SHR},
        {"&&", TOK_AND}, {"||", TOK_OR}, {"->", TOK_ARROW}, {"..", TOK_DOTDOT},
        {"+", TOK_PLUS}, {"-", TOK_MINUS}, {"*", TOK_STAR}, {"/", TOK_SLASH},
        {"%", TOK_MOD}, {"<", TOK_LT}, {">", TOK_GT}, {"!", TOK_NOT},
        {"&", TOK_AMP}, {"|", TOK_PIPE}, {"^", TOK_CARET}, {"~", TOK_TILDE},
        {"@", TOK_AT}, {".", TOK_DOT}, {"(", TOK_LPAREN}, {")", TOK_RPAREN},
        {"[", TOK_LBRACKET}, {"]", TOK_RBRACKET}, {",", TOK_COMMA},
        {":", TOK_COLON}, {";", TOK_SEMICOLON},
    };
    TokenKind kind;
    size_t dotted = dotted_operator(lx, 0, &kind);
    if (dotted) {
        take(lx, kind, dotted);
        return;
    }
    for (size_t i = 0; i < COUNT(SYMBOLS); i++) {
        size_t length = strlen(SYMBOLS[i].text);
        if (strncmp(lx->source->text + lx->pos, SYMBOLS[i].text, length) == 0) {
            take(lx, SYMBOLS[i].kind, length);
            return;
        }
    }
    if (at(lx, 0) == '=') {
        take(lx, lx->config->assign == ASSIGN_EQUALS ? TOK_ASSIGN : TOK_EQ, 1);
        return;
    }
    unsigned char c = (unsigned char)at(lx, 0);
    if (c >= 0x21 && c < 0x7f) diag_error(lx->diag, here(lx), "unexpected character '%c'", c);
    else diag_error(lx->diag, here(lx), "unexpected byte 0x%02x", c);
    lx->pos++;
    while (((unsigned char)at(lx, 0) & 0xC0) == 0x80) lx->pos++; /* rest of a UTF-8 char */
}

TokenList lex(const Source *source, const Config *config, Arena *arena, Diag *diag) {
    Lexer lx = {.source = source, .config = config, .arena = arena, .diag = diag,
                .line = 1, .fresh_line = true};
    if (source->length >= 3 && memcmp(source->text, "\xEF\xBB\xBF", 3) == 0)
        lx.pos = lx.line_start = 3;

    for (;;) {
        skip_blanks(&lx);
        if (at_end(&lx)) break;
        char c = at(&lx, 0);
        if (is_alpha(c)) lex_word(&lx);
        else if (is_digit(c)) lex_number(&lx);
        else if (c == '"' || c == '\'') lex_quoted(&lx, c);
        else lex_symbol(&lx);
    }
    push(&lx, TOK_EOF, here(&lx), "", 0);
    return (TokenList){lx.items, lx.count};
}

const char *token_kind_name(TokenKind kind) {
    static const char *const NAMES[TOK_COUNT] = {
        [TOK_EOF] = "end of file", [TOK_IDENT] = "identifier",
        [TOK_INT] = "integer number", [TOK_FLOAT] = "real number",
        [TOK_STRING] = "string", [TOK_CHAR] = "character",
        [TOK_DIM] = "array dimension", [TOK_INOUT] = "'in/out'",
        [TOK_ASSIGN] = "':='", [TOK_PLUS] = "'+'", [TOK_MINUS] = "'-'",
        [TOK_STAR] = "'*'", [TOK_SLASH] = "'/'", [TOK_MOD] = "'%'",
        [TOK_LT] = "'<'", [TOK_GT] = "'>'", [TOK_LE] = "'<='", [TOK_GE] = "'>='",
        [TOK_EQ] = "'=='", [TOK_NE] = "'!='", [TOK_AND] = "'and'",
        [TOK_OR] = "'or'", [TOK_NOT] = "'not'", [TOK_SHL] = "'<<'",
        [TOK_SHR] = "'>>'", [TOK_AMP] = "'&'", [TOK_PIPE] = "'|'",
        [TOK_CARET] = "'^'", [TOK_TILDE] = "'~'", [TOK_AT] = "'@'",
        [TOK_ARROW] = "'->'", [TOK_DOT] = "'.'", [TOK_DOTDOT] = "'..'",
        [TOK_LPAREN] = "'('", [TOK_RPAREN] = "')'", [TOK_LBRACKET] = "'['",
        [TOK_RBRACKET] = "']'", [TOK_COMMA] = "','", [TOK_COLON] = "':'",
        [TOK_SEMICOLON] = "';'", [TOK_TRUE] = "'true'", [TOK_FALSE] = "'false'",
    };
    if (kind >= 0 && kind < TOK_COUNT && NAMES[kind]) return NAMES[kind];
    static char keyword[32];
    for (size_t i = 0; i < COUNT(COMMON); i++) {
        if (COMMON[i].kind == kind) {
            snprintf(keyword, sizeof keyword, "'%s'", COMMON[i].text);
            return keyword;
        }
    }
    return "token";
}
