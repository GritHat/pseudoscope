/* Reads other pseudocodes: PSeInt (Spanish), VisuAlg and Portugol (Portuguese),
 * and the one of the Cambridge exams (English). Their words become the tokens
 * of the language, so that most of the parser is the same for all. */

#include "pseudoscope/blocks.h"
#include "pseudoscope/lexer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text; /* lower case, without accents; a blank stands for any blanks */
    TokenKind kind;
} Word;

static const Word PSEINT[] = {
    {"proceso", TOK_PROGRAM}, {"algoritmo", TOK_PROGRAM},
    {"finproceso", TOK_ENDPROGRAM}, {"finalgoritmo", TOK_ENDPROGRAM},
    {"fin proceso", TOK_ENDPROGRAM}, {"fin algoritmo", TOK_ENDPROGRAM},
    {"funcion", TOK_FUNCTION}, {"subproceso", TOK_FUNCTION}, {"subalgoritmo", TOK_FUNCTION},
    {"finfuncion", TOK_ENDFUNCTION}, {"fin funcion", TOK_ENDFUNCTION},
    {"finsubproceso", TOK_ENDFUNCTION}, {"finsubalgoritmo", TOK_ENDFUNCTION},
    {"fin subproceso", TOK_ENDFUNCTION}, {"fin subalgoritmo", TOK_ENDFUNCTION},
    {"definir", TOK_DECLARE}, {"como", TOK_AS}, {"dimension", TOK_DIMENSION}, {"dimensionar", TOK_DIMENSION},
    {"entero", TOK_INTEGER}, {"enteros", TOK_INTEGER}, {"entera", TOK_INTEGER},
    {"real", TOK_REAL}, {"reales", TOK_REAL}, {"numero", TOK_REAL}, {"numerico", TOK_REAL}, {"numerica", TOK_REAL},
    {"logico", TOK_LOGICAL}, {"logica", TOK_LOGICAL}, {"booleano", TOK_LOGICAL},
    {"caracter", TOK_STRINGTYPE}, {"caracteres", TOK_STRINGTYPE}, {"texto", TOK_STRINGTYPE}, {"cadena", TOK_STRINGTYPE},
    {"si no", TOK_ELSE}, {"sino", TOK_ELSE}, {"si", TOK_IF}, {"entonces", TOK_THEN},
    {"finsi", TOK_ENDIF}, {"fin si", TOK_ENDIF},
    {"sinosi", TOK_ELSEIF}, {"retornar", TOK_RETURN}, {"devolver", TOK_RETURN}, /* not PSeInt's: for what is turned into it */
    {"mientras que", TOK_UNTIL_WHILE}, {"mientras", TOK_WHILE}, {"hacer", TOK_DO},
    {"finmientras", TOK_ENDWHILE}, {"fin mientras", TOK_ENDWHILE},
    {"para", TOK_FOR}, {"hasta que", TOK_UNTIL}, {"hasta", TOK_TO}, {"con paso", TOK_STEP},
    {"finpara", TOK_ENDFOR}, {"fin para", TOK_ENDFOR}, {"repetir", TOK_REPEAT},
    {"segun", TOK_SWITCH}, {"de otro modo", TOK_OTHERWISE}, {"finsegun", TOK_ENDSWITCH}, {"fin segun", TOK_ENDSWITCH},
    {"escribir sin saltar", TOK_PRINT}, {"escribir", TOK_PRINT}, {"mostrar", TOK_PRINT}, {"imprimir", TOK_PRINT},
    {"sin saltar", TOK_NOLINE}, {"sin bajar", TOK_NOLINE},
    {"leer", TOK_READ}, {"por referencia", TOK_BYREF}, {"por valor", TOK_IGNORE},
    {"limpiar pantalla", TOK_IGNORE}, {"borrar pantalla", TOK_IGNORE}, {"esperar tecla", TOK_IGNORE},
    {"verdadero", TOK_TRUE}, {"falso", TOK_FALSE}, {"y", TOK_AND}, {"o", TOK_OR}, {"no", TOK_NOT}, {"mod", TOK_MOD},
    {NULL, TOK_EOF},
};

static const Word VISUALG[] = {
    {"algoritmo", TOK_PROGRAM}, {"fimalgoritmo", TOK_ENDPROGRAM}, {"inicio", TOK_BEGIN}, {"var", TOK_VAR},
    {"funcao", TOK_FUNCTION}, {"fimfuncao", TOK_ENDFUNCTION},
    {"procedimento", TOK_PROCEDURE}, {"fimprocedimento", TOK_ENDPROCEDURE}, {"retorne", TOK_RETURN},
    {"inteiro", TOK_INTEGER}, {"real", TOK_REAL}, {"numerico", TOK_REAL}, {"logico", TOK_LOGICAL},
    {"caractere", TOK_STRINGTYPE}, {"caracter", TOK_STRINGTYPE}, {"literal", TOK_STRINGTYPE},
    {"vetor", TOK_ARRAY}, {"de", TOK_OF},
    {"se", TOK_IF}, {"entao", TOK_THEN}, {"senao", TOK_ELSE}, {"fimse", TOK_ENDIF},
    {"enquanto", TOK_WHILE}, {"faca", TOK_DO}, {"fimenquanto", TOK_ENDWHILE},
    {"para", TOK_FOR}, {"ate", TOK_TO}, {"passo", TOK_STEP}, {"fimpara", TOK_ENDFOR}, {"repita", TOK_REPEAT},
    {"escolha", TOK_SWITCH}, {"caso", TOK_CASE}, {"outrocaso", TOK_OTHERWISE}, {"fimescolha", TOK_ENDSWITCH},
    {"escreval", TOK_PRINTLN}, {"escreva", TOK_PRINT}, {"leia", TOK_READ},
    {"verdadeiro", TOK_TRUE}, {"falso", TOK_FALSE}, {"e", TOK_AND}, {"ou", TOK_OR}, {"nao", TOK_NOT},
    {"mod", TOK_MOD}, {"div", TOK_IDIV}, {"limpatela", TOK_IGNORE},
    {NULL, TOK_EOF},
};

static const Word PORTUGOL[] = {
    {"programa", TOK_PROGRAM}, {"funcao", TOK_FUNCTION}, {"retorne", TOK_RETURN}, {"const", TOK_CONST},
    {"inteiro", TOK_INTEGER}, {"real", TOK_REAL}, {"logico", TOK_LOGICAL}, {"caracter", TOK_STRINGTYPE},
    {"cadeia", TOK_STRINGTYPE}, {"vazio", TOK_IGNORE},
    {"se", TOK_IF}, {"senao", TOK_ELSE}, {"enquanto", TOK_WHILE}, {"faca", TOK_DO}, {"para", TOK_FOR},
    {"escolha", TOK_SWITCH}, {"caso contrario", TOK_OTHERWISE}, {"caso", TOK_CASE}, {"pare", TOK_ENDSWITCH},
    {"escreva", TOK_PRINT}, {"leia", TOK_READ}, {"limpa", TOK_IGNORE},
    {"verdadeiro", TOK_TRUE}, {"falso", TOK_FALSE}, {"e", TOK_AND}, {"ou", TOK_OR}, {"nao", TOK_NOT},
    {NULL, TOK_EOF},
};

static const Word CAMBRIDGE[] = {
    {"declare", TOK_DECLARE}, {"constant", TOK_CONST}, {"type", TOK_TYPE}, {"endtype", TOK_ENDTYPE},
    {"integer", TOK_INTEGER}, {"real", TOK_REAL}, {"boolean", TOK_LOGICAL}, {"char", TOK_STRINGTYPE},
    {"string", TOK_STRINGTYPE}, {"array", TOK_ARRAY}, {"of", TOK_OF},
    {"if", TOK_IF}, {"then", TOK_THEN}, {"else if", TOK_ELSEIF}, {"else", TOK_ELSE}, {"endif", TOK_ENDIF},
    {"while", TOK_WHILE}, {"do", TOK_DO}, {"endwhile", TOK_ENDWHILE},
    {"for", TOK_FOR}, {"to", TOK_TO}, {"step", TOK_STEP}, {"next", TOK_NEXT},
    {"repeat", TOK_REPEAT}, {"until", TOK_UNTIL},
    {"case of", TOK_SWITCH}, {"otherwise", TOK_OTHERWISE}, {"endcase", TOK_ENDSWITCH},
    {"procedure", TOK_PROCEDURE}, {"endprocedure", TOK_ENDPROCEDURE},
    {"function", TOK_FUNCTION}, {"endfunction", TOK_ENDFUNCTION}, {"returns", TOK_RETURNS}, {"return", TOK_RETURN},
    {"byref", TOK_BYREF}, {"byval", TOK_IGNORE}, {"call", TOK_CALL},
    {"output", TOK_PRINT}, {"print", TOK_PRINT}, {"input", TOK_READ},
    {"true", TOK_TRUE}, {"false", TOK_FALSE}, {"and", TOK_AND}, {"or", TOK_OR}, {"not", TOK_NOT},
    {"mod", TOK_MOD}, {"div", TOK_IDIV},
    {NULL, TOK_EOF},
};

static const Word *words_of(Dialect dialect) {
    switch (dialect) {
        case DIALECT_PSEINT: return PSEINT;
        case DIALECT_VISUALG: return VISUALG;
        case DIALECT_PORTUGOL: return PORTUGOL;
        default: return CAMBRIDGE;
    }
}

/* Functions of the dialects, by the names they have here. */
static const char *library_name(Dialect dialect, const char *word) {
    static const struct {
        int dialect; /* -1: all of them */
        const char *word, *name;
    } NAMES[] = {
        {DIALECT_VISUALG, "log", "log10"}, {DIALECT_VISUALG, "logn", "log"}, {DIALECT_CAMBRIDGE, "substring", "mid"},
        {-1, "rc", "sqrt"}, {-1, "raiz", "sqrt"}, {-1, "raizq", "sqrt"}, {-1, "ln", "log"}, {-1, "sen", "sin"}, {-1, "redon", "round"},
        {-1, "trunc", "trunc"}, {-1, "int", "trunc"}, {-1, "abs", "abs"}, {-1, "exp", "exp"}, {-1, "cos", "cos"}, {-1, "tan", "tan"},
        {-1, "sqrt", "sqrt"}, {-1, "azar", "random_below"}, {-1, "aleatorio", "random"}, {-1, "sorteia", "random"},
        {-1, "longitud", "length"}, {-1, "compr", "length"}, {-1, "length", "length"}, {-1, "len", "length"},
        {-1, "mayusculas", "upper"}, {-1, "maiusc", "upper"}, {-1, "ucase", "upper"}, {-1, "to_upper", "upper"},
        {-1, "minusculas", "lower"}, {-1, "minusc", "lower"}, {-1, "lcase", "lower"}, {-1, "to_lower", "lower"},
        {-1, "subcadena", "substring"}, {-1, "concatenar", "concat"},
        {-1, "asen", "asin"}, {-1, "arcsen", "asin"}, {-1, "acos", "acos"}, {-1, "arccos", "acos"}, {-1, "atan", "atan"},
        {-1, "arctan", "atan"}, {-1, "convertiranumero", "number_of"}, {-1, "convertiratexto", "text_of"},
        {-1, "asc", "char_code"}, {-1, "carac", "char_of"}, {-1, "chr", "char_of"}, {-1, "caracpnum", "number_of"},
        {-1, "numpcarac", "text_of"}, {-1, "copia", "mid"}, {-1, "pos", "position"}, {-1, "grauprad", "radians"},
        {-1, "radpgrau", "degrees"}, {-1, "quad", "square"}, {-1, "randi", "random_below"}, {-1, "mid", "mid"},
        {-1, "left", "left"}, {-1, "right", "right"}, {-1, "num_to_str", "text_of"}, {-1, "str_to_num", "number_of"},
        {-1, "eof", "file_eof"},
    };
    if (dialect == DIALECT_PLIKE) return NULL;
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if ((NAMES[i].dialect < 0 || NAMES[i].dialect == (int)dialect) && strcmp(NAMES[i].word, word) == 0) return NAMES[i].name;
    return NULL;
}

/* The libraries of Portugol Studio: what each of their functions is here.
 * A name that begins with a digit is a constant. */
static const char *portugol_member(const char *library, const char *member) {
    static const struct {
        const char *library, *member, *name;
    } MEMBERS[] = {
        {"matematica", "potencia", "pow"}, {"matematica", "raiz", "root"}, {"matematica", "seno", "sin"},
        {"matematica", "cosseno", "cos"}, {"matematica", "tangente", "tan"}, {"matematica", "logaritmo", "log_base"},
        {"matematica", "valor_absoluto", "abs"}, {"matematica", "arredondar", "round_to"}, {"matematica", "maior_numero", "max"},
        {"matematica", "menor_numero", "min"}, {"matematica", "pi", "3.141592653589793"},
        {"texto", "numero_caracteres", "length"}, {"texto", "caixa_alta", "upper"}, {"texto", "caixa_baixa", "lower"},
        {"texto", "extrair_subtexto", "slice"}, {"texto", "posicao_texto", "position_from"}, {"texto", "substituir", "replace"},
        {"texto", "obter_caracter", "char_at"},
        {"tipos", "inteiro_para_cadeia", "whole_text"}, {"tipos", "real_para_cadeia", "text_of"},
        {"tipos", "cadeia_para_inteiro", "whole_from"}, {"tipos", "cadeia_para_real", "number_of"},
        {"tipos", "real_para_inteiro", "trunc"}, {"tipos", "inteiro_para_real", "real_of"},
        {"tipos", "cadeia_e_inteiro", "is_whole"}, {"tipos", "cadeia_e_real", "is_number"},
        {"util", "aguarde", "wait"}, {"util", "sorteia", "random"}, {"util", "tempo_decorrido", "clock"},
        {"arquivos", "abrir_arquivo", "file_open"}, {"arquivos", "ler_linha", "file_read"},
        {"arquivos", "escrever_linha", "file_put"}, {"arquivos", "fim_arquivo", "file_eof"},
        {"arquivos", "fechar_arquivo", "file_close"}, {"arquivos", "arquivo_existe", "file_exists"},
        {"arquivos", "modo_leitura", "0"}, {"arquivos", "modo_escrita", "1"}, {"arquivos", "modo_acrescentar", "2"},
        {"graficos", "iniciar_modo_grafico", "canvas_on"}, {"graficos", "encerrar_modo_grafico", "canvas_off"},
        {"graficos", "definir_dimensoes_janela", "canvas"}, {"graficos", "definir_titulo_janela", "canvas_title"},
        {"graficos", "definir_cor", "color"}, {"graficos", "criar_cor", "rgb"}, {"graficos", "limpar", "clear"},
        {"graficos", "desenhar_retangulo", "rounded_rect"}, {"graficos", "desenhar_elipse", "ellipse"},
        {"graficos", "desenhar_linha", "line"}, {"graficos", "desenhar_ponto", "point"}, {"graficos", "desenhar_texto", "draw_text"},
        {"graficos", "renderizar", "show"}, {"graficos", "largura_janela", "canvas_width"},
        {"graficos", "altura_janela", "canvas_height"}, {"graficos", "cor_preto", "0"}, {"graficos", "cor_branco", "16777215"},
        {"graficos", "cor_vermelho", "16711680"}, {"graficos", "cor_verde", "65280"}, {"graficos", "cor_azul", "255"},
        {"graficos", "cor_amarelo", "16776960"},
    };
    for (size_t i = 0; i < sizeof MEMBERS / sizeof MEMBERS[0]; i++)
        if (strcmp(MEMBERS[i].library, library) == 0 && strcmp(MEMBERS[i].member, member) == 0) return MEMBERS[i].name;
    return NULL;
}

typedef struct {
    const Source *source;
    const Config *config;
    Arena *arena;
    Diag *diag;
    size_t pos, line_start;
    int line;
    bool fresh_line;
    Token *items;
    int count, capacity;
    struct {
        char alias[32], library[32];
    } libraries[8]; /* Portugol: inclua biblioteca Graficos --> g */
    int library_count;
} Lexer;

static char at(const Lexer *lx, size_t offset) {
    size_t i = lx->pos + offset;
    return i < lx->source->length ? lx->source->text[i] : '\0';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || ((unsigned char)c >= 0xC3 && (unsigned char)c < 0xE0);
}
static bool is_word(const Lexer *lx, size_t i) {
    char c = at(lx, i);
    return is_letter(c) || is_digit(c) || ((unsigned char)c >= 0x80 && (unsigned char)c < 0xC0);
}

/* The letter at `i`, lower case and without accents; *bytes is its length. */
static char plain(const Lexer *lx, size_t i, size_t *bytes) {
    unsigned char c = (unsigned char)at(lx, i), d = (unsigned char)at(lx, i + 1);
    *bytes = 1;
    if (c == 0xC3 && d >= 0x80) {
        static const char TABLE[] = "aaaaaaaceeeeiiiidnooooo*ouuuuypsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        *bytes = 2;
        return d - 0x80 < (int)sizeof TABLE - 1 ? TABLE[d - 0x80] : '?';
    }
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c;
}

static void push_as(Lexer *lx, TokenKind kind, Loc loc, const char *text, size_t length, size_t source_length) {
    if (lx->count == lx->capacity) {
        int capacity = lx->capacity ? lx->capacity * 2 : 256;
        Token *items = arena_alloc(lx->arena, (size_t)capacity * sizeof(Token));
        if (lx->count) memcpy(items, lx->items, (size_t)lx->count * sizeof(Token));
        lx->items = items;
        lx->capacity = capacity;
    }
    lx->items[lx->count++] = (Token){kind, loc, arena_strndup(lx->arena, text, length), lx->fresh_line, (int)source_length};
    lx->fresh_line = false;
}

static void push(Lexer *lx, TokenKind kind, Loc loc, const char *text, size_t length) {
    push_as(lx, kind, loc, text, length, length);
}

static Loc here(const Lexer *lx) { return (Loc){lx->line, (int)(lx->pos - lx->line_start) + 1}; }

static void take(Lexer *lx, TokenKind kind, size_t length) {
    push(lx, kind, here(lx), lx->source->text + lx->pos, length);
    lx->pos += length;
}

static void skip_blanks(Lexer *lx) {
    for (;;) {
        char c = at(lx, 0);
        if (lx->pos >= lx->source->length) return;
        if (c == '\n') {
            lx->pos++;
            lx->line++;
            lx->line_start = lx->pos;
            lx->fresh_line = true;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            lx->pos++;
        } else if (c == '/' && at(lx, 1) == '/') {
            while (lx->pos < lx->source->length && at(lx, 0) != '\n') lx->pos++;
        } else if (c == '/' && at(lx, 1) == '*') {
            Loc start = here(lx);
            lx->pos += 2;
            while (lx->pos < lx->source->length && !(at(lx, 0) == '*' && at(lx, 1) == '/')) {
                if (at(lx, 0) == '\n') {
                    lx->line++;
                    lx->line_start = lx->pos + 1;
                    lx->fresh_line = true;
                }
                lx->pos++;
            }
            if (lx->pos >= lx->source->length) diag_error(lx->diag, start, "comment is never closed");
            else lx->pos += 2;
        } else {
            return;
        }
    }
}

/* Length of the keyword at the current position if it is this one, or 0. */
static size_t matches(const Lexer *lx, const char *word) {
    size_t i = 0;
    while (*word) {
        if (*word == ' ') {
            size_t start = i;
            while (at(lx, i) == ' ' || at(lx, i) == '\t') i++;
            if (i == start) return 0;
            word++;
            continue;
        }
        size_t bytes;
        if (plain(lx, i, &bytes) != *word) return 0;
        i += bytes;
        word++;
    }
    return is_word(lx, i) ? 0 : i;
}

static size_t plain_word(const Lexer *lx, size_t from, char *out, size_t size) {
    size_t length = 0, used = 0;
    while (is_word(lx, from + length)) {
        size_t bytes;
        char c = plain(lx, from + length, &bytes);
        if (used + 1 < size) out[used++] = c;
        length += bytes;
    }
    out[used] = '\0';
    return length;
}

static void rest_of_line(Lexer *lx) {
    while (lx->pos < lx->source->length && at(lx, 0) != '\n') lx->pos++;
}

/* Words that are not of the language itself: the settings of the tools these
 * pseudocodes were written for, and the libraries of Portugol. */
static bool special_word(Lexer *lx, const char *name, size_t length) {
    Dialect dialect = lx->config->dialect;
    size_t after = length;
    while (at(lx, after) == ' ' || at(lx, after) == '\t') after++;
    char next = at(lx, after);
    bool used_as_name = next == '(' || next == '<' || next == ':' || next == '=' || next == ',' || next == '[';
    if (lx->fresh_line && !used_as_name) {
        static const char *const SETTINGS[] = {"arquivo", "timer", "eco", "cronometro", "pausa", "debug", "aleatorio"};
        bool skip = dialect == DIALECT_PSEINT && strcmp(name, "esperar") == 0;
        for (size_t i = 0; dialect == DIALECT_VISUALG && i < sizeof SETTINGS / sizeof SETTINGS[0]; i++) skip |= strcmp(name, SETTINGS[i]) == 0;
        if (skip) {
            rest_of_line(lx);
            return true;
        }
    }
    if ((dialect == DIALECT_PSEINT || dialect == DIALECT_VISUALG) && strcmp(name, "pi") == 0 && !used_as_name) {
        push_as(lx, TOK_FLOAT, here(lx), "3.141592653589793", 17, length);
        lx->pos += length;
        return true;
    }
    if (dialect != DIALECT_PORTUGOL) return false;
    if (strcmp(name, "inclua") == 0) { /* inclua biblioteca Graficos --> g */
        char word[32], library[32], alias[32];
        size_t i = after;
        i += plain_word(lx, i, word, sizeof word);
        while (at(lx, i) == ' ') i++;
        i += plain_word(lx, i, library, sizeof library);
        while (at(lx, i) == ' ') i++;
        snprintf(alias, sizeof alias, "%s", library);
        if (at(lx, i) == '-' && at(lx, i + 1) == '-' && at(lx, i + 2) == '>') {
            i += 3;
            while (at(lx, i) == ' ') i++;
            plain_word(lx, i, alias, sizeof alias);
        }
        if (strcmp(word, "biblioteca") == 0 && library[0] && lx->library_count < 8) {
            snprintf(lx->libraries[lx->library_count].alias, 32, "%s", alias);
            snprintf(lx->libraries[lx->library_count++].library, 32, "%s", library);
        }
        rest_of_line(lx);
        return true;
    }
    if (at(lx, length) != '.') return false;
    for (int k = 0; k < lx->library_count; k++) {
        if (strcmp(lx->libraries[k].alias, name) != 0) continue;
        char member[48];
        size_t n = plain_word(lx, length + 1, member, sizeof member);
        const char *known = portugol_member(lx->libraries[k].library, member);
        if (!known) {
            diag_error(lx->diag, here(lx), "the library '%s' has no '%s' here", lx->libraries[k].library, member);
            known = "0";
        }
        bool constant = known[0] >= '0' && known[0] <= '9';
        push_as(lx, constant ? (strchr(known, '.') ? TOK_FLOAT : TOK_INT) : TOK_IDENT, here(lx), known, strlen(known), length + 1 + n);
        lx->pos += length + 1 + n;
        return true;
    }
    return false;
}

/* Cambridge: OPENFILE, READFILE, WRITEFILE and CLOSEFILE, as calls. */
static void cambridge_files(Lexer *lx) {
    Token *out = arena_alloc(lx->arena, (size_t)(lx->count * 2 + 16) * sizeof(Token));
    int n = 0;
    for (int i = 0; i < lx->count;) {
        const Token *t = &lx->items[i];
        static const char *const WORDS[] = {"openfile", "readfile", "writefile", "closefile"};
        int which = -1;
        for (int k = 0; k < 4 && t->starts_line && t->kind == TOK_IDENT; k++)
            if (equals_ignore_case(t->text, WORDS[k])) which = k;
        if (which < 0) {
            out[n++] = lx->items[i++];
            continue;
        }
        int end = i + 1, split = -1;
        while (end < lx->count && !lx->items[end].starts_line && lx->items[end].kind != TOK_EOF) {
            if (split < 0 && (lx->items[end].kind == (which == 0 ? TOK_FOR : TOK_COMMA))) split = end;
            end++;
        }
        Token made = {TOK_IDENT, t->loc, "", false, 0};
#define MAKE(kind_, text_, first) (made.kind = (kind_), made.text = (text_), made.starts_line = (first), out[n++] = made)
        if (which == 1 && split > 0) { /* READFILE f, x: x <- file_read(f) */
            for (int k = split + 1; k < end; k++) {
                out[n] = lx->items[k];
                out[n++].starts_line = k == split + 1;
            }
            MAKE(TOK_ASSIGN, "<-", false);
            MAKE(TOK_IDENT, "file_read", false);
            MAKE(TOK_LPAREN, "(", false);
            for (int k = i + 1; k < split; k++) out[n++] = lx->items[k];
            MAKE(TOK_RPAREN, ")", false);
        } else {
            static const char *const CALLS[] = {"file_open", "file_read", "file_write", "file_close"};
            MAKE(TOK_CALL, "CALL", true);
            MAKE(TOK_IDENT, CALLS[which], false);
            MAKE(TOK_LPAREN, "(", false);
            for (int k = i + 1; k < (which == 0 && split > 0 ? split : end); k++) out[n++] = lx->items[k];
            if (which == 0) { /* FOR READ, WRITE or APPEND */
                const char *mode = split > 0 && split + 1 < end ? lx->items[split + 1].text : "read";
                MAKE(TOK_COMMA, ",", false);
                MAKE(TOK_INT, equals_ignore_case(mode, "write") ? "1" : equals_ignore_case(mode, "append") ? "2" : "0", false);
            }
            MAKE(TOK_RPAREN, ")", false);
        }
#undef MAKE
        i = end;
    }
    lx->items = out;
    lx->count = n;
}

static void lex_word(Lexer *lx) {
    size_t length = 0, best = 0;
    TokenKind kind = TOK_IDENT;
    for (const Word *w = words_of(lx->config->dialect); w->text; w++) {
        size_t n = matches(lx, w->text);
        if (n > best) {
            best = n;
            kind = w->kind;
        }
    }
    if (best) {
        take(lx, kind, best);
        return;
    }
    char name[64];
    size_t used = 0;
    while (is_word(lx, length)) {
        size_t bytes;
        char c = plain(lx, length, &bytes);
        if (used + 1 < sizeof name) name[used++] = c;
        length += bytes;
    }
    name[used] = '\0';
    if (special_word(lx, name, length)) return;
    size_t after = length;
    while (at(lx, after) == ' ' || at(lx, after) == '\t') after++;
    const char *library = at(lx, after) == '(' ? library_name(lx->config->dialect, name) : NULL;
    if (library) {
        push_as(lx, TOK_IDENT, here(lx), library, strlen(library), length);
        lx->pos += length;
        return;
    }
    /* These languages do not tell upper and lower case apart: one spelling,
     * that of the first time. */
    for (int i = 0; i < lx->count; i++) {
        const Token *t = &lx->items[i];
        if (t->kind != TOK_IDENT || strlen(t->text) != length) continue;
        Source other = {"", t->text, length, NULL};
        Lexer view = {.source = &other};
        size_t k = 0;
        bool same = lx->config->dialect != DIALECT_PORTUGOL && !lx->config->cormen;
        while (same && k < length) {
            size_t a, b;
            same = plain(&view, k, &a) == plain(lx, k, &b) && a == b;
            k += a;
        }
        if (same) {
            push(lx, TOK_IDENT, here(lx), t->text, length);
            lx->pos += length;
            return;
        }
    }
    take(lx, TOK_IDENT, length);
}

static void lex_number(Lexer *lx) {
    size_t n = 0;
    bool real = false;
    while (is_digit(at(lx, n))) n++;
    if (at(lx, n) == '.' && is_digit(at(lx, n + 1))) {
        real = true;
        n++;
        while (is_digit(at(lx, n))) n++;
    }
    const char *text = lx->source->text + lx->pos;
    size_t zeros = 0; /* "007" is not octal here */
    while (zeros + 1 < n && text[zeros] == '0' && is_digit(text[zeros + 1])) zeros++;
    push_as(lx, real ? TOK_FLOAT : TOK_INT, here(lx), text + zeros, n - zeros, n);
    lx->pos += n;
}

static void lex_quoted(Lexer *lx, char quote) {
    Loc loc = here(lx);
    size_t n = 1;
    StrBuf text = {0};
    strbuf_puts(&text, "");
    bool escapes = lx->config->dialect == DIALECT_PORTUGOL;
    while (at(lx, n) != quote && at(lx, n) != '\n' && lx->pos + n < lx->source->length) {
        char c = at(lx, n);
        if (escapes && c == '\\' && at(lx, n + 1)) {
            strbuf_append(&text, lx->source->text + lx->pos + n, 2);
            n += 2;
            continue;
        }
        if (c == '\\' || c == '"') strbuf_puts(&text, "\\");
        strbuf_append(&text, &c, 1);
        n++;
    }
    if (at(lx, n) != quote) {
        diag_error(lx->diag, loc, "unterminated %s", "string");
        lx->pos += n;
        strbuf_free(&text);
        return;
    }
    push_as(lx, TOK_STRING, loc, text.data, text.length, n + 1);
    strbuf_free(&text);
    lx->pos += n + 1;
}

static void lex_symbol(Lexer *lx) {
    static const struct {
        const char *text;
        TokenKind kind;
    } SYMBOLS[] = {
        {"\xE2\x86\x90", TOK_ASSIGN}, {"\xE2\x89\xA0", TOK_NE}, {"\xE2\x89\xA4", TOK_LE}, {"\xE2\x89\xA5", TOK_GE},
        {"<-", TOK_ASSIGN}, {":=", TOK_ASSIGN}, {"<>", TOK_NE}, {"!=", TOK_NE}, {"<=", TOK_LE}, {">=", TOK_GE},
        {"==", TOK_EQ}, {"..", TOK_DOTDOT}, {"+", TOK_PLUS}, {"-", TOK_MINUS}, {"*", TOK_STAR}, {"/", TOK_RDIV},
        {"\\", TOK_IDIV}, {"%", TOK_MOD}, {"^", TOK_POWER}, {"<", TOK_LT}, {">", TOK_GT}, {"=", TOK_EQ},
        {"~", TOK_NOT}, {"(", TOK_LPAREN}, {")", TOK_RPAREN}, {"[", TOK_LBRACKET}, {"]", TOK_RBRACKET},
        {",", TOK_COMMA}, {":", TOK_COLON}, {";", TOK_SEMICOLON}, {".", TOK_DOT},
    };
    Dialect dialect = lx->config->dialect;
    if (dialect == DIALECT_PORTUGOL) { /* C-like */
        static const struct {
            const char *text;
            TokenKind kind;
        } EXTRA[] = {{"++", TOK_INCREMENT}, {"--", TOK_DECREMENT}, {"+=", TOK_ADD_ASSIGN}, {"-=", TOK_SUB_ASSIGN},
                     {"==", TOK_EQ}, {"=", TOK_ASSIGN}, {"{", TOK_LBRACE}, {"}", TOK_RBRACE}, {"/", TOK_SLASH},
                     {"&", TOK_BYREF}, {"!=", TOK_NE}};
        for (size_t i = 0; i < sizeof EXTRA / sizeof EXTRA[0]; i++) {
            size_t n = strlen(EXTRA[i].text);
            if (strncmp(lx->source->text + lx->pos, EXTRA[i].text, n) == 0) {
                take(lx, EXTRA[i].kind, n);
                return;
            }
        }
    }
    if (dialect == DIALECT_CAMBRIDGE && at(lx, 0) == '&') {
        take(lx, TOK_CONCAT, 1);
        return;
    }
    if (dialect == DIALECT_PSEINT && (at(lx, 0) == '&' || at(lx, 0) == '|')) {
        size_t n = at(lx, 1) == at(lx, 0) ? 2 : 1;
        take(lx, at(lx, 0) == '&' ? TOK_AND : TOK_OR, n);
        return;
    }
    for (size_t i = 0; i < sizeof SYMBOLS / sizeof SYMBOLS[0]; i++) {
        size_t n = strlen(SYMBOLS[i].text);
        if (strncmp(lx->source->text + lx->pos, SYMBOLS[i].text, n) == 0) {
            take(lx, SYMBOLS[i].kind, n);
            return;
        }
    }
    unsigned char c = (unsigned char)at(lx, 0);
    if (c >= 0x21 && c < 0x7f) diag_error(lx->diag, here(lx), "unexpected character '%c'", c);
    else diag_error(lx->diag, here(lx), "unexpected byte 0x%02x", c);
    lx->pos++;
    while (((unsigned char)at(lx, 0) & 0xC0) == 0x80) lx->pos++;
}

TokenList lex_dialect(const Source *source, const Config *config, Arena *arena, Diag *diag) {
    Lexer lx = {.source = source, .config = config, .arena = arena, .diag = diag, .line = 1, .fresh_line = true};
    if (source->length >= 3 && memcmp(source->text, "\xEF\xBB\xBF", 3) == 0) lx.pos = lx.line_start = 3;
    for (;;) {
        skip_blanks(&lx);
        if (lx.pos >= source->length) break;
        char c = at(&lx, 0);
        if (is_letter(c)) lex_word(&lx);
        else if (is_digit(c)) lex_number(&lx);
        else if (c == '"' || c == '\'') lex_quoted(&lx, c);
        else lex_symbol(&lx);
    }
    push(&lx, TOK_EOF, here(&lx), "", 0);
    if (config->dialect == DIALECT_CAMBRIDGE) cambridge_files(&lx);
    return (TokenList){lx.items, lx.count};
}

static bool ends_with(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    return a >= b && equals_ignore_case(path + a - b, suffix);
}

Config config_for_text(Config config, const char *path, const char *text) {
    if (!config.dialect_given && text && !is_program_path(path)) {
        config.dialect = guess_dialect(text);
        config.dialect_given = true;
    }
    return config_for(config, path);
}

Config config_for(Config config, const char *path) {
    if (!config.dialect_given) {
        config.dialect = ends_with(path, ".psc") ? DIALECT_PSEINT : ends_with(path, ".alg") ? DIALECT_VISUALG
                       : ends_with(path, ".por") ? DIALECT_PORTUGOL : ends_with(path, ".pse") ? DIALECT_CAMBRIDGE
                       : ends_with(path, ".clrs") ? DIALECT_CORMEN : DIALECT_PLIKE;
    }
    if (config.dialect != DIALECT_PLIKE) {
        config.paren_arrays = false;
        config.params = PARAMS_MIXED;
        /* VisuAlg and Cambridge always write both bounds */
        if (!config.indexing_given && (config.dialect == DIALECT_PSEINT || config.dialect == DIALECT_PORTUGOL))
            config.index_base = 0;
    }
    return config;
}
