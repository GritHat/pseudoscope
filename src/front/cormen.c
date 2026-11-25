#include "pseudoscope/cormen.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* What is open, by its indentation: a block ends where a line begins no
 * further right than the line that opened it. */
typedef struct {
    int indent;
    char kind; /* p: procedure, m: what is outside any, f: for, w: while, r: repeat, i: if */
    int nested; /* the "elseif" of an if, each closed with it */
    bool value; /* a procedure that gives back a value */
} Open;

typedef struct {
    Arena *arena;
    Diag *diag;
    StrBuf out;
    Open open[64];
    int depth, line;
    bool value;          /* inside a procedure that gives back a value */
    bool main_done;
    char zero[16][32];   /* the arrays of this procedure that begin at 0, */
    unsigned zero_dims[16]; /* and which of their dimensions do */
    int zero_count;
} Rewriter;

static bool is_word(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* After this word, if the text begins with it. */
static const char *after_word(const char *s, const char *word) {
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0 || is_word(s[n])) return NULL;
    s += n;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void problem(Rewriter *r, const char *format, const char *what) {
    diag_error(r->diag, (Loc){r->line, 1}, format, what);
}

static const char *SYMBOLS[][2] = {
    {"\xe2\x89\xa4", "<="}, {"\xe2\x89\xa5", ">="}, {"\xe2\x89\xa0", "<>"}, {"\xe2\x86\x90", "<-"}, {"\xe2\x88\x9e", "1000000000"},
    {"\xe2\x8c\x8a", "trunc("}, {"\xe2\x8c\x8b", ")"}, {"\xc2\xb7", "*"}, {"\xc3\x97", "*"}, {"\xe2\x88\xa7", " Y "},
    {"\xe2\x88\xa8", " O "}, {"\xc2\xac", " NO "}, {"\xe2\x88\x92", "-"}, {"==", "="}, {"!=", "<>"},
};

static const char *expression(Rewriter *r, const char *s, size_t n);

/* The end of what the bracket at `s` opens. */
static const char *closing(const char *s, const char *end) {
    int depth = 0;
    for (; s < end; s++) {
        if (*s == '"') {
            for (s++; s < end && *s != '"'; s++) {}
        } else if (*s == '[' || *s == '(') {
            depth++;
        } else if (*s == ']' || *s == ')') {
            if (--depth == 0) return s;
        }
    }
    return NULL;
}

/* An expression, in the words that are read: names with hyphens, the signs of
 * mathematics, arrays that begin at 0. */
static const char *expression(Rewriter *r, const char *s, size_t n) {
    StrBuf out = {0};
    strbuf_puts(&out, "");
    const char *end = s + n;
    while (s < end) {
        if (*s == '"') { /* a text, as it is */
            const char *close = memchr(s + 1, '"', (size_t)(end - s - 1));
            size_t length = close ? (size_t)(close - s + 1) : (size_t)(end - s);
            strbuf_append(&out, s, length);
            s += length;
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < sizeof SYMBOLS / sizeof SYMBOLS[0] && !found; i++) {
            size_t length = strlen(SYMBOLS[i][0]);
            if ((size_t)(end - s) < length || strncmp(s, SYMBOLS[i][0], length) != 0) continue;
            strbuf_puts(&out, SYMBOLS[i][1]);
            s += length;
            found = true;
        }
        if (found) continue;
        if ((unsigned char)*s >= 0x80) { /* a sign we do not read: a set, a ceiling... */
            problem(r, "this sign of mathematics is not read: write it out in words%.0s", "");
            while (s < end && (unsigned char)*s >= 0x80) s++;
            continue;
        }
        if (*s == '.' && s + 1 < end && isalpha((unsigned char)s[1]) && out.length && (is_word(out.data[out.length - 1]) || out.data[out.length - 1] == ']')) {
            const char *name = s + 1;
            size_t length = 0;
            while (name + length < end && (is_word(name[length]) || (name[length] == '-' && isalpha((unsigned char)name[length + 1])))) length++;
            if (length == 6 && strncmp(name, "length", 6) == 0)
                problem(r, "the length of an array is not kept: pass it to the procedure, as in 'SORT(A, n)'%.0s", "");
            else problem(r, "objects with attributes, as in '%s', are not read yet: only numbers, texts and arrays", arena_strndup(r->arena, s, length + 1));
            s = name + length;
            continue;
        }
        if (!isalpha((unsigned char)*s) && *s != '_') {
            strbuf_append(&out, s, 1);
            s++;
            continue;
        }
        size_t length = 0;
        while (s + length < end && (is_word(s[length]) || s[length] == '\'')) length++;
        /* MERGE-SORT(...): one name, if a bracket follows */
        size_t whole = length;
        while (s + whole + 1 < end && s[whole] == '-' && isalpha((unsigned char)s[whole + 1])) {
            whole++;
            while (s + whole < end && (is_word(s[whole]) || s[whole] == '\'')) whole++;
        }
        const char *next = s + whole;
        while (next < end && *next == ' ') next++;
        if (whole > length && next < end && *next == '(') length = whole;
        char word[96];
        snprintf(word, sizeof word, "%.*s", (int)(length < sizeof word - 1 ? length : sizeof word - 1), s);
        for (char *c = word; *c; c++)
            if (*c == '-' || *c == '\'') *c = '_';
        s += length;
        static const char *const WORDS[][2] = {{"and", "Y"}, {"or", "O"}, {"not", "NO"}, {"mod", "MOD"}, {"TRUE", "Verdadero"},
                                               {"true", "Verdadero"}, {"FALSE", "Falso"}, {"false", "Falso"}};
        const char *said = word;
        for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
            if (strcmp(word, WORDS[i][0]) == 0) said = WORDS[i][1];
        if (strcmp(word, "NIL") == 0 || strcmp(word, "nil") == 0)
            problem(r, "objects with attributes, as in '%s', are not read yet: only numbers, texts and arrays", "NIL");
        strbuf_puts(&out, said);
        /* an array that begins at 0 is kept from 1: its indices move by one */
        int zero = -1;
        for (int i = 0; i < r->zero_count; i++)
            if (strcmp(r->zero[i], word) == 0) zero = i;
        const char *close = zero >= 0 && s < end && *s == '[' ? closing(s, end) : NULL;
        if (close) {
            strbuf_puts(&out, "[");
            const char *from = s + 1;
            int depth = 0, dim = 0;
            for (const char *c = s + 1; c <= close; c++) {
                if (*c == '[' || *c == '(') depth++;
                else if ((*c == ']' || *c == ')') && c < close) depth--;
                if (c < close && !(*c == ',' && depth == 0)) continue;
                const char *index = expression(r, from, (size_t)(c - from));
                if (r->zero_dims[zero] & (1u << dim)) strbuf_printf(&out, "%s(%s)+1", dim ? ", " : "", index);
                else strbuf_printf(&out, "%s%s", dim ? ", " : "", index);
                from = c + 1;
                dim++;
            }
            strbuf_puts(&out, "]");
            s = close + 1;
        }
    }
    const char *kept = arena_strndup(r->arena, out.data, out.length);
    strbuf_free(&out);
    return kept;
}

static const char *all(Rewriter *r, const char *s) { return expression(r, s, strlen(s)); }

/* The text without a word at its end ("do", "then"), which the older editions wrote. */
static size_t without(const char *s, size_t n, const char *word) {
    size_t w = strlen(word);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n > w && strncmp(s + n - w, word, w) == 0 && !is_word(s[n - w - 1])) n -= w;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    return n;
}

/* Where a word stands by itself in the text, outside brackets and texts. */
static const char *word_in(const char *s, const char *word) {
    int depth = 0;
    size_t w = strlen(word);
    for (const char *c = s; *c; c++) {
        if (*c == '"') {
            for (c++; *c && *c != '"'; c++) {}
            if (!*c) break;
        } else if (*c == '(' || *c == '[') {
            depth++;
        } else if (*c == ')' || *c == ']') {
            depth--;
        } else if (depth == 0 && strncmp(c, word, w) == 0 && (c == s || !is_word(c[-1])) && !is_word(c[w])) {
            return c;
        }
    }
    return NULL;
}

static const char *statement(Rewriter *r, const char *s, int indent);

static void push(Rewriter *r, char kind, int indent) {
    if (r->depth < 64) r->open[r->depth++] = (Open){.indent = indent, .kind = kind};
}

/* "let A[1:n] and B[0:k] be new arrays" */
static const char *let(Rewriter *r, const char *s) {
    const char *be = word_in(s, "be");
    StrBuf out = {0};
    strbuf_puts(&out, "");
    bool ok = be != NULL;
    while (ok && s < be) {
        while (s < be && (*s == ' ' || *s == ',')) s++;
        const char *and = after_word(s, "and");
        if (and) s = and;
        if (s >= be) break;
        size_t length = 0;
        while (is_word(s[length]) || s[length] == '\'') length++;
        const char *close = length && s[length] == '[' ? closing(s + length, be) : NULL;
        if (!close) {
            ok = false;
            break;
        }
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)(length < 31 ? length : 31), s);
        for (char *c = name; *c; c++)
            if (*c == '\'') *c = '_';
        strbuf_printf(&out, "%sDimension %s[", out.length ? "; " : "", name);
        unsigned zeros = 0;
        int dim = 0, depth = 0;
        const char *from = s + length + 1;
        for (const char *c = from; c <= close; c++) {
            if (*c == '[' || *c == '(') depth++;
            else if ((*c == ']' || *c == ')') && c < close) depth--;
            if (c < close && !(*c == ',' && depth == 0)) continue;
            /* from:to, from..to, or only how many */
            const char *split = NULL;
            for (const char *k = from; k < c; k++)
                if (*k == ':' || (k[0] == '.' && k[1] == '.')) split = split ? split : k;
            const char *low = "1", *high;
            if (split) {
                low = expression(r, from, (size_t)(split - from));
                const char *rest = split + (*split == ':' ? 1 : 2);
                high = expression(r, rest, (size_t)(c - rest));
            } else {
                high = expression(r, from, (size_t)(c - from));
            }
            while (*low == ' ') low++;
            bool from_zero = strncmp(low, "0", 1) == 0 && !is_word(low[1]);
            if (!from_zero && !(strncmp(low, "1", 1) == 0 && strspn(low + 1, " ") == strlen(low + 1)))
                problem(r, "an array begins at 0 or at 1 here, not at '%s'", low);
            if (from_zero) zeros |= 1u << dim;
            strbuf_printf(&out, from_zero ? "%s(%s)+1" : "%s%s", dim ? ", " : "", high);
            from = c + 1;
            dim++;
        }
        strbuf_puts(&out, "]");
        if (zeros && r->zero_count < 16) {
            snprintf(r->zero[r->zero_count], sizeof r->zero[0], "%s", name);
            r->zero_dims[r->zero_count++] = zeros;
        }
        s = close + 1;
    }
    if (!ok || !out.length) problem(r, "this 'let' is not understood: write 'let A[1:n] be a new array'%.0s", "");
    const char *kept = arena_strndup(r->arena, out.data, out.length);
    strbuf_free(&out);
    return kept;
}

/* One statement, in the words that are read. What it opens is noted. */
static const char *statement(Rewriter *r, const char *s, int indent) {
    const char *rest;
    size_t n = strlen(s);
    if ((rest = after_word(s, "for"))) {
        if (after_word(rest, "each") || word_in(rest, "in")) {
            problem(r, "'for each' goes through a set, and sets are not read yet%.0s", "");
            return "";
        }
        n = without(rest, strlen(rest), "do");
        char *line = arena_strndup(r->arena, rest, n);
        char *down = (char *)word_in(line, "downto"), *to = down ? down : (char *)word_in(line, "to"), *by = (char *)word_in(line, "by");
        char *gets = strstr(line, "\xe2\x86\x90");
        char *equals = gets ? gets : strchr(line, '=');
        if (!to || !equals || equals > to) return all(r, s); /* let the reader say what is wrong */
        *equals = '\0';
        *to = '\0';
        if (by) *by = '\0';
        const char *step = by ? arena_printf(r->arena, down ? " Con Paso -(%s)" : " Con Paso %s", all(r, by + 2)) : down ? " Con Paso -1" : "";
        push(r, 'f', indent);
        return arena_printf(r->arena, "Para %s <- %s Hasta %s%s Hacer", all(r, line), all(r, equals + (gets ? 3 : 1)),
                            all(r, to + (down ? 6 : 2)), step);
    }
    if ((rest = after_word(s, "while"))) {
        push(r, 'w', indent);
        return arena_printf(r->arena, "Mientras %s Hacer", expression(r, rest, without(rest, strlen(rest), "do")));
    }
    if (after_word(s, "repeat")) {
        push(r, 'r', indent);
        return "Repetir";
    }
    if ((rest = after_word(s, "until"))) {
        if (!r->depth || r->open[r->depth - 1].kind != 'r' || r->open[r->depth - 1].indent != indent) {
            problem(r, "'%s' has nothing above it to go with", "until");
            return "";
        }
        r->depth--;
        return arena_printf(r->arena, "Hasta Que %s", all(r, rest));
    }
    bool otherwise = (rest = after_word(s, "elseif")) || (rest = after_word(s, "elif")) ||
                     ((rest = after_word(s, "else")) && (rest = after_word(rest, "if")));
    if (otherwise || (rest = after_word(s, "if"))) {
        Open *top = r->depth ? &r->open[r->depth - 1] : NULL;
        if (otherwise && (!top || top->kind != 'i' || top->indent != indent)) {
            problem(r, "'%s' has nothing above it to go with", "elseif");
            return "";
        }
        if (otherwise) top->nested++;
        else push(r, 'i', indent);
        const char *then = word_in(rest, "then");
        const char *condition = expression(r, rest, then ? (size_t)(then - rest) : strlen(rest));
        const char *same_line = then && then[4] && strspn(then + 4, " \t") < strlen(then + 4) ? then + 4 + strspn(then + 4, " \t") : NULL;
        int before = r->depth;
        const char *inner = same_line ? statement(r, same_line, indent + 1) : "";
        (void)before;
        return arena_printf(r->arena, "%sSi %s Entonces %s", otherwise ? "SiNo " : "", condition, inner);
    }
    if ((rest = after_word(s, "else"))) {
        Open *top = r->depth ? &r->open[r->depth - 1] : NULL;
        if (!top || top->kind != 'i' || top->indent != indent) {
            problem(r, "'%s' has nothing above it to go with", "else");
            return "";
        }
        return arena_printf(r->arena, "SiNo %s", *rest ? statement(r, rest, indent + 1) : "");
    }
    if ((rest = after_word(s, "return"))) {
        if (!*rest) return "Retornar";
        if (word_in(rest, ",") || strchr(rest, ',')) {
            int depth = 0;
            for (const char *c = rest; *c; c++) {
                if (*c == '(' || *c == '[') depth++;
                else if (*c == ')' || *c == ']') depth--;
                else if (*c == ',' && depth == 0) {
                    problem(r, "'return' gives back one value here, not several%.0s", "");
                    return "";
                }
            }
        }
        return arena_printf(r->arena, "res_ <- %s; Retornar", all(r, rest));
    }
    if ((rest = after_word(s, "error"))) return arena_printf(r->arena, "Escribir \"error: \", %s; Retornar", *rest ? all(r, rest) : "\"\"");
    if ((rest = after_word(s, "print"))) return arena_printf(r->arena, "Escribir %s", all(r, rest));
    if ((rest = after_word(s, "exchange")) || (rest = after_word(s, "swap"))) {
        const char *with = word_in(rest, "with");
        if (with) {
            const char *a = expression(r, rest, (size_t)(with - rest)), *b = all(r, with + 4);
            return arena_printf(r->arena, "tmp_ <- %s; %s <- %s; %s <- tmp_", a, a, b, b);
        }
    }
    if ((rest = after_word(s, "let"))) return let(r, rest);
    /* a = b = c: from the right */
    const char *cuts[8];
    int count = 0, depth = 0;
    for (const char *c = s; *c && count < 8; c++) {
        if (*c == '"') {
            for (c++; *c && *c != '"'; c++) {}
            if (!*c) break;
        } else if (*c == '(' || *c == '[') {
            depth++;
        } else if (*c == ')' || *c == ']') {
            depth--;
        } else if (depth == 0 && strncmp(c, "\xe2\x86\x90", 3) == 0) {
            cuts[count++] = c;
            c += 2;
        } else if (depth == 0 && *c == '=' && c[1] != '=' && c > s && !strchr("<>!=", c[-1])) {
            cuts[count++] = c;
        }
    }
    if (!count) return all(r, s);
    const char *value = all(r, cuts[count - 1] + (*cuts[count - 1] == '=' ? 1 : 3));
    StrBuf out = {0};
    strbuf_puts(&out, "");
    for (int i = count - 1; i >= 0; i--) {
        const char *from = i ? cuts[i - 1] + (*cuts[i - 1] == '=' ? 1 : 3) : s;
        const char *target = expression(r, from, (size_t)(cuts[i] - from));
        strbuf_printf(&out, "%s%s <- %s", out.length ? "; " : "", target, value);
        value = target;
    }
    const char *kept = arena_strndup(r->arena, out.data, out.length);
    strbuf_free(&out);
    return kept;
}

/* A line of code: where it begins, after the number the book gives it, and where its comment begins. */
static int measure(const char *line, size_t n, const char **code, size_t *length, const char **comment) {
    size_t i = 0;
    int indent = 0;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) indent += line[i++] == '\t' ? 4 : 1;
    size_t digits = 0;
    while (i + digits < n && isdigit((unsigned char)line[i + digits])) digits++;
    if (digits && i + digits < n && (line[i + digits] == ' ' || line[i + digits] == '\t')) { /* "12  for ...": the number is part of the margin */
        i += digits;
        indent += (int)digits;
        while (i < n && (line[i] == ' ' || line[i] == '\t')) indent += line[i++] == '\t' ? 4 : 1;
    }
    *code = line + i;
    *comment = NULL;
    size_t end = i;
    for (; end < n; end++) {
        if (line[end] == '"') {
            for (end++; end < n && line[end] != '"'; end++) {}
        } else if ((line[end] == '/' && end + 1 < n && line[end + 1] == '/') || strncmp(line + end, "\xe2\x96\xb7", 3) == 0 ||
                   strncmp(line + end, "\xe2\x96\xb9", 3) == 0) {
            *comment = line + end + (line[end] == '/' ? 2 : 3);
            break;
        }
    }
    if (end > n) end = n;
    while (end > i && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) end--;
    *length = end - i;
    return indent;
}

/* NAME(what it takes), alone on its line: a procedure, if what follows is further right. */
static bool heading(const char *code, size_t n) {
    size_t i = 0;
    if (!n || !isalpha((unsigned char)code[0]) || code[n - 1] != ')') return false;
    while (i < n && (is_word(code[i]) || code[i] == '-' || code[i] == '\'')) i++;
    while (i < n && code[i] == ' ') i++;
    return i < n && code[i] == '(' && closing(code + i, code + n) == code + n - 1;
}

bool looks_like_cormen(const char *text) {
    const char *line = text;
    bool head = false;
    for (int seen = 0; *line && seen < 40; seen++) {
        size_t n = strcspn(line, "\n");
        const char *code, *comment;
        size_t length;
        int indent = measure(line, n, &code, &length, &comment);
        if (length) {
            if (head) return indent > 0 && !after_word(code, "begin") && !after_word(code, "var");
            if (indent || !heading(code, length) || !isupper((unsigned char)code[0])) return false;
            for (size_t i = 0; i < length && code[i] != '('; i++)
                if (islower((unsigned char)code[i]) && i < 2) return false; /* Algoritmo(...), Funcion(...): somebody else's */
            head = true;
        }
        line += n + (line[n] == '\n');
    }
    return false;
}

static void close_one(Rewriter *r, StrBuf *before) {
    Open *top = &r->open[--r->depth];
    switch (top->kind) {
        case 'f': strbuf_puts(before, "FinPara "); break;
        case 'w': strbuf_puts(before, "FinMientras "); break;
        case 'i':
            for (int i = 0; i <= top->nested; i++) strbuf_puts(before, "FinSi ");
            break;
        case 'r': problem(r, "'%s' has nothing above it to go with", "repeat"); break;
        case 'p': strbuf_puts(before, top->value ? "FinFuncion " : "FinSubProceso "); break;
        case 'm': strbuf_puts(before, "FinAlgoritmo "); break;
        default: break;
    }
}

Source cormen_source(const Source *written, Config *config, Arena *arena, Diag *diag) {
    Rewriter rewriter = {.arena = arena, .diag = diag};
    Rewriter *r = &rewriter;
    strbuf_puts(&r->out, "");
    const char *line = written->text;
    while (*line) {
        size_t n = strcspn(line, "\n");
        r->line++;
        const char *code, *comment;
        size_t length;
        int indent = measure(line, n, &code, &length, &comment);
        StrBuf before = {0};
        strbuf_puts(&before, "");
        if (length) {
            char *text = arena_strndup(arena, code, length);
            /* is something further right coming? Then a name with brackets is the head of a procedure */
            bool deeper = false;
            for (const char *next = line + n + (line[n] == '\n'); *next; next += strcspn(next, "\n") + (next[strcspn(next, "\n")] == '\n')) {
                const char *c, *k;
                size_t l;
                int at = measure(next, strcspn(next, "\n"), &c, &l, &k);
                if (!l) continue;
                deeper = at > indent;
                break;
            }
            bool head = deeper && heading(text, length) && !word_in(text, "=");
            for (int i = 0; head && i < r->depth; i++)
                if (r->open[i].kind != 'p' && r->open[i].kind != 'm' && r->open[i].indent < indent) head = false;
            while (r->depth) {
                Open *top = &r->open[r->depth - 1];
                if (top->kind == 'm' ? !head : top->indent < indent) break;
                if (top->indent == indent && top->kind == 'i' && (after_word(text, "else") || after_word(text, "elseif") || after_word(text, "elif"))) break;
                if (top->indent == indent && top->kind == 'r' && after_word(text, "until")) break;
                close_one(r, &before);
            }
            const char *said;
            if (head) {
                /* it gives back a value if a "return" of its own has one */
                bool value = false;
                for (const char *next = line + n + (line[n] == '\n'); *next; next += strcspn(next, "\n") + (next[strcspn(next, "\n")] == '\n')) {
                    const char *c, *k, *rest;
                    size_t l;
                    int at = measure(next, strcspn(next, "\n"), &c, &l, &k);
                    if (!l) continue;
                    if (at <= indent) break;
                    for (size_t i = 0; i + 6 <= l; i++)
                        if ((i == 0 || !is_word(c[i - 1])) && (rest = after_word(c + i, "return")) && rest < c + l) value = true;
                }
                r->value = value;
                r->zero_count = 0;
                push(r, 'p', indent);
                r->open[r->depth - 1].value = value;
                said = arena_printf(arena, value ? "Funcion res_ <- %s" : "SubProceso %s", all(r, text));
            } else {
                if (!r->depth) { /* outside every procedure: the program itself */
                    if (r->main_done) problem(r, "the statements outside the procedures go together, in one place%.0s", "");
                    r->main_done = true;
                    r->zero_count = 0;
                    r->value = false;
                    push(r, 'm', -1);
                    strbuf_puts(&before, "Algoritmo principal ");
                }
                said = statement(r, text, indent);
            }
            strbuf_printf(&r->out, "%s%s", before.data, said);
        }
        if (comment) strbuf_printf(&r->out, " // %.*s", (int)(n - (size_t)(comment - line)), comment);
        strbuf_puts(&r->out, "\n");
        strbuf_free(&before);
        line += n + (line[n] == '\n');
    }
    StrBuf last = {0};
    strbuf_puts(&last, "");
    while (r->depth) close_one(r, &last);
    strbuf_printf(&r->out, "%s\n", last.data);
    strbuf_free(&last);

    config->dialect = DIALECT_PSEINT;
    config->dialect_given = true;
    config->index_base = 1;
    config->indexing_given = true;
    config->cormen = true;
    Source run = {written->name, arena_strndup(arena, r->out.data, r->out.length), r->out.length, written->shown ? written->shown : written->text};
    strbuf_free(&r->out);
    return run;
}
