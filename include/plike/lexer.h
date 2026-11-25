#ifndef PLIKE_LEXER_H
#define PLIKE_LEXER_H

#include "plike/arena.h"
#include "plike/config.h"
#include "plike/diag.h"

typedef enum {
    TOK_EOF,
    TOK_IDENT, TOK_INT, TOK_FLOAT, TOK_STRING, TOK_CHAR, TOK_DIM,

    TOK_FUNCTION, TOK_PROCEDURE, TOK_ENDFUNCTION, TOK_ENDPROCEDURE,
    TOK_VAR, TOK_TYPE, TOK_RECORD, TOK_BEGIN, TOK_END,
    TOK_IF, TOK_THEN, TOK_ELSEIF, TOK_ELSE, TOK_ENDIF,
    TOK_WHILE, TOK_DO, TOK_ENDWHILE, TOK_FOR, TOK_TO, TOK_STEP, TOK_ENDFOR,
    TOK_REPEAT, TOK_UNTIL, TOK_RETURN, TOK_IN, TOK_OUT, TOK_INOUT,
    TOK_PRINT, TOK_READ,
    TOK_INTEGER, TOK_REAL, TOK_LOGICAL, TOK_CHARACTER, TOK_ARRAY, TOK_OF,
    TOK_TRUE, TOK_FALSE,

    TOK_ASSIGN, TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_MOD,
    TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_EQ, TOK_NE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_SHL, TOK_SHR, TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_AT, TOK_ARROW, TOK_DOT, TOK_DOTDOT, TOK_ELLIPSIS,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_COLON, TOK_SEMICOLON,

    /* only in other dialects */
    TOK_PROGRAM, TOK_ENDPROGRAM, TOK_DECLARE, TOK_AS, TOK_DIMENSION, TOK_BYREF, TOK_STRINGTYPE,
    TOK_SWITCH, TOK_CASE, TOK_OTHERWISE, TOK_ENDSWITCH, TOK_NOLINE, TOK_PRINTLN, TOK_CALL,
    TOK_RETURNS, TOK_CONST, TOK_NEXT, TOK_ENDTYPE, TOK_IGNORE, TOK_UNTIL_WHILE,
    TOK_POWER, TOK_IDIV, TOK_RDIV, TOK_CONCAT,
    TOK_LBRACE, TOK_RBRACE, TOK_INCREMENT, TOK_DECREMENT, TOK_ADD_ASSIGN, TOK_SUB_ASSIGN,
    TOK_COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    Loc loc;
    /* Spelling. For TOK_STRING and TOK_CHAR: the body without quotes, with
     * escapes kept as written (they are the C ones). */
    const char *text;
    bool starts_line;
    int length;   /* in the source */
} Token;

typedef struct {
    Token *items;
    int count;
} TokenList;

/* Splits the source into tokens. Bad characters are reported and skipped; the
 * list always ends with TOK_EOF. */
TokenList lex(const Source *source, const Config *config, Arena *arena, Diag *diag);

/* The other dialects. */
TokenList lex_dialect(const Source *source, const Config *config, Arena *arena, Diag *diag);

const char *token_kind_name(TokenKind kind);

/* ASCII case-insensitive comparison: keywords may be written in any case. */
bool equals_ignore_case(const char *a, const char *b);

#endif
