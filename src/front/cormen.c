#include "pseudoscope/cormen.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* What is open, by its indentation: a block ends where a line begins no
 * further right than the line that opened it. */
typedef struct {
    int indent;
    char kind; /* p: procedure, m: what is outside any, f: for, w: while, r: repeat, i: if */
    int nested; /* the "elseif" of an if, each closed with it */
    bool value; /* a procedure that gives back a value */
} Open;

/* What a first reading tells of each procedure. */
typedef struct {
    char name[64];
    char params[12][32];
    int param_count;
    unsigned lengths;    /* the arrays it takes whose length it asks for (A.length, for each) */
    unsigned asked, indexed, walked, arrays; /* what tells an array from a collection */
    int values;          /* how many values its "return" gives back */
} Proc;

/* A name, in one procedure (or outside them all: -1), and what happens to it first. */
typedef struct {
    char name[32];
    int context;
    char first;          /* r: it is read; w: something is put in it */
    bool assigned;       /* by an assignment of its own */
    bool array;          /* made as an array */
} Seen;

/* A call: who makes it, and the names it hands on. */
typedef struct {
    char name[64];
    int context;
    char args[12][32];
    int count;
} Call;

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
    char sized[16][32];  /* the arrays made here, and how long each is */
    const char *sizes[16];
    int sized_count;
    Proc procs[64];
    int proc_count;
    const Proc *proc;    /* the one being read; NULL outside them all */
    Seen *seen;
    int seen_count;
    Call *calls;
    int call_count;
    StrBuf shared;       /* the names the procedures share */
    const char *targets; /* (a, b) = F(x): where what F gives back goes */
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
    {"\xe2\x8c\x8a", "trunc(floor("}, {"\xe2\x8c\x8b", "))"}, {"\xe2\x8c\x88", "trunc(ceil("}, {"\xe2\x8c\x89", "))"},
    {"\xe2\x88\xaa", " + "}, {"\xe2\x88\xa9", " * "}, {"\xe2\x88\x85", "set_()"}, {"\\", " - "},
    {"\xcf\x80", "@pi"}, {"\xc2\xb7", "*"}, {"\xc3\x97", "*"}, {"\xe2\x88\xa7", " Y "},
    {"\xe2\x88\xa8", " O "}, {"\xc2\xac", " NO "}, {"\xe2\x88\x92", "-"}, {"==", "="}, {"!=", "<>"},
};

static const char *expression(Rewriter *r, const char *s, size_t n);

static const Proc *proc_named(const Rewriter *r, const char *name) {
    for (int i = 0; i < r->proc_count; i++)
        if (strcmp(r->procs[i].name, name) == 0) return &r->procs[i];
    return NULL;
}

/* How long an array is: it was made here, or it came in with its length beside it. */
static const char *length_of(Rewriter *r, const char *name) {
    for (int i = 0; i < r->sized_count; i++)
        if (strcmp(r->sized[i], name) == 0) return r->sizes[i];
    for (int i = 0; r->proc && i < r->proc->param_count; i++)
        if (strcmp(r->proc->params[i], name) == 0 && (r->proc->lengths & (1u << i))) return arena_printf(r->arena, "@%s_length", name);
    return NULL;
}

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

enum { MAX_SEEN = 768, MAX_CALLS = 192 };

/* Whether something is ever put in this name, anywhere: else, in capitals, it is a word that stands for itself (WHITE). */
static bool ever_written(const Rewriter *r, const char *name) {
    for (int i = 0; i < r->seen_count; i++)
        if (strcmp(r->seen[i].name, name) == 0 && (r->seen[i].first == 'w' || r->seen[i].assigned || r->seen[i].array)) return true;
    for (int i = 0; i < r->proc_count; i++)
        for (int k = 0; k < r->procs[i].param_count; k++)
            if (strcmp(r->procs[i].params[k], name) == 0) return true;
    return false;
}

static bool capitals(const char *word) {
    int letters = 0;
    for (const char *c = word; *c; c++) {
        if (islower((unsigned char)*c)) return false;
        letters += isupper((unsigned char)*c) != 0;
    }
    return letters >= 2;
}

/* Where the last thing written begins: a name with what follows it, or what is between brackets. */
static size_t operand_start(const StrBuf *out) {
    size_t i = out->length;
    while (i && out->data[i - 1] == ' ') i--;
    for (;;) {
        if (i && (out->data[i - 1] == ')' || out->data[i - 1] == ']')) {
            int depth = 0;
            while (i) {
                char c = out->data[--i];
                if (c == ')' || c == ']') depth++;
                else if ((c == '(' || c == '[') && --depth == 0) break;
            }
        } else if (i && (is_word(out->data[i - 1]) || out->data[i - 1] == '@' || out->data[i - 1] == '.')) {
            while (i && (is_word(out->data[i - 1]) || out->data[i - 1] == '@' || out->data[i - 1] == '.')) i--;
        } else {
            return i;
        }
    }
}

/* Whether something that has a value was just written: else a bar opens |S|. */
static bool operand_before(const StrBuf *out) {
    size_t i = out->length;
    while (i && out->data[i - 1] == ' ') i--;
    if (!i) return false;
    char c = out->data[i - 1];
    if (c == ')' || c == ']' || c == '"') return true;
    if (!is_word(c)) return false;
    size_t from = i;
    while (from && is_word(out->data[from - 1])) from--;
    if (from && out->data[from - 1] == '@') return true;
    size_t n = i - from;
    const char *word = out->data + from;
    return !((n == 1 && (*word == 'Y' || *word == 'O')) || (n == 2 && strncmp(word, "NO", 2) == 0) || (n == 3 && strncmp(word, "MOD", 3) == 0));
}

/* Where what begins here ends: a name with its attributes and brackets, a set between braces. */
static const char *operand_end(const char *s, const char *end) {
    while (s < end && *s == ' ') s++;
    if (s < end && *s == '{') {
        int depth = 0;
        for (; s < end; s++) {
            if (*s == '{') depth++;
            else if (*s == '}' && --depth == 0) return s + 1;
        }
        return end;
    }
    while (s < end) {
        if (*s == '(' || *s == '[') {
            const char *close = closing(s, end);
            if (!close) return end;
            s = close + 1;
        } else if (is_word(*s) || *s == '.' || *s == '\'' || (unsigned char)*s >= 0x80) {
            s++;
        } else if (*s == '-' && s + 1 < end && isalpha((unsigned char)s[1])) { /* FIND-SET(u): one name, if a bracket follows */
            const char *k = s + 1;
            while (k < end && (is_word(*k) || (*k == '-' && k + 1 < end && isalpha((unsigned char)k[1])))) k++;
            if (k >= end || *k != '(') return s;
            s = k;
        } else {
            break;
        }
    }
    return s;
}

/* Whether these brackets hold two things, (u, v): where the comma is. */
static const char *pair_comma(const char *s, size_t n) {
    if (n < 5 || s[0] != '(' || closing(s, s + n) != s + n - 1) return NULL;
    int depth = 0;
    for (const char *c = s + 1; c < s + n - 1; c++) {
        if (*c == '(' || *c == '[' || *c == '{') depth++;
        else if (*c == ')' || *c == ']' || *c == '}') depth--;
        else if (*c == ',' && depth == 0) return c;
    }
    return NULL;
}

/* Something that goes into a collection: a value, or two together, (u, v). */
static const char *element(Rewriter *r, const char *s, size_t n) {
    while (n && *s == ' ') s++, n--;
    while (n && s[n - 1] == ' ') n--;
    const char *comma = pair_comma(s, n);
    if (!comma) return expression(r, s, n);
    return arena_printf(r->arena, "pair_(%s, %s)", expression(r, s + 1, (size_t)(comma - s - 1)), expression(r, comma + 1, (size_t)(s + n - 1 - comma - 1)));
}

/* What is done with queues, stacks and the like without saying how, when the writer has no procedure of that name. */
static const char *const TAKES[][2] = {{"DEQUEUE", "take_first_"}, {"POP", "take_last_"}, {"EXTRACT_MIN", "take_min_"},
                                       {"EXTRACT_MAX", "take_max_"}, {"STACK_EMPTY", "empty_"}, {"QUEUE_EMPTY", "empty_"}};

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
        bool member = strncmp(s, "\xe2\x88\x88", 3) == 0, stranger = strncmp(s, "\xe2\x88\x89", 3) == 0;
        bool word_in_ = !member && !stranger && s + 2 <= end && strncmp(s, "in", 2) == 0 && (s + 2 == end || !is_word(s[2])) && out.length &&
                        !is_word(out.data[out.length - 1]) && out.data[out.length - 1] != '.' && operand_before(&out);
        if (member || stranger || word_in_) { /* x ∈ S */
            while (out.length && out.data[out.length - 1] == ' ') out.data[--out.length] = '\0';
            if (word_in_ && out.length >= 3 && strcmp(out.data + out.length - 3, " NO") == 0) { /* not in */
                out.length -= 3;
                out.data[out.length] = '\0';
                stranger = true;
            }
            size_t from = operand_start(&out);
            const char *left = arena_strndup(r->arena, out.data + from, out.length - from);
            if (left[0] == '(' && strchr(left, ',')) left = arena_printf(r->arena, "pair_%s", left);
            out.length = from;
            out.data[from] = '\0';
            s += word_in_ ? 2 : 3;
            const char *stop = operand_end(s, end);
            strbuf_printf(&out, stranger ? "(NO in_(%s, %s))" : "in_(%s, %s)", left, expression(r, s, (size_t)(stop - s)));
            s = stop;
            continue;
        }
        if (*s == '{') { /* a set, written out */
            const char *stop = operand_end(s, end);
            strbuf_puts(&out, "set_of_(");
            const char *from = s + 1;
            int depth = 0, count = 0;
            for (const char *c = s + 1; c < stop; c++) {
                if (*c == '(' || *c == '[' || *c == '{') depth++;
                else if ((*c == ')' || *c == ']' || *c == '}') && c < stop - 1) depth--;
                if (c < stop - 1 && !(*c == ',' && depth == 0)) continue;
                if (strspn(from, " ") < (size_t)(c - from)) strbuf_printf(&out, "%s%s", count++ ? ", " : "", element(r, from, (size_t)(c - from)));
                from = c + 1;
            }
            strbuf_puts(&out, ")");
            s = stop;
            continue;
        }
        if (*s == '|' && !operand_before(&out)) { /* |S|: how many */
            const char *bar = memchr(s + 1, '|', (size_t)(end - s - 1));
            if (bar) {
                strbuf_printf(&out, "count_(%s)", expression(r, s + 1, (size_t)(bar - s - 1)));
                s = bar + 1;
                continue;
            }
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
            if (length == 6 && strncmp(name, "length", 6) == 0) { /* A.length: in place of the name, already written */
                size_t back = out.length;
                while (back && (is_word(out.data[back - 1]) || out.data[back - 1] == '@')) back--;
                const char *array = arena_strndup(r->arena, out.data + back + (out.data[back] == '@'), out.length - back - (out.data[back] == '@'));
                const char *known = length_of(r, array);
                if (!known) problem(r, "the length of '%s' is not known here: it is neither made here nor taken by the procedure", array);
                out.length = back;
                out.data[back] = '\0';
                strbuf_printf(&out, "(%s)", known ? known : "0");
            } else { /* an attribute of an object */
                size_t back = out.length;
                while (back && is_word(out.data[back - 1])) back--;
                bool array = false; /* an array made here, or taken: it has elements, not attributes */
                if (back && out.data[back - 1] == '@' && (back == 1 || !is_word(out.data[back - 2])) && out.data[back - 2 + (back == 1)] != '.') {
                    const char *base = out.data + back;
                    for (int i = 0; i < r->sized_count; i++) array |= strcmp(r->sized[i], base) == 0;
                    for (int i = 0; r->proc && i < r->proc->param_count; i++)
                        array |= (r->proc->arrays & (1u << i)) && strcmp(r->proc->params[i], base) == 0;
                }
                if (array) problem(r, "an array has no attributes here ('%s'): keep that number in a name of its own, and hand it on beside the array",
                                   arena_strndup(r->arena, name, length));
                strbuf_puts(&out, ".@");
                for (size_t i = 0; i < length; i++) strbuf_append(&out, name[i] == '-' ? "_" : name + i, 1);
            }
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
        bool ours = said != word;
        if (strcmp(word, "NIL") == 0 || strcmp(word, "nil") == 0) said = "nil_", ours = true;
        for (size_t i = 0; i < sizeof TAKES / sizeof TAKES[0] && s < end && *s == '(' && !ours; i++)
            if (strcasecmp(word, TAKES[i][0]) == 0 && !proc_named(r, word)) said = TAKES[i][1], ours = true; /* Dequeue, as codebox has it */
        if (!ours && capitals(word) && !(s < end && (*s == '(' || *s == '[')) && !ever_written(r, word)) { /* WHITE: a word that stands for itself */
            strbuf_printf(&out, "\"%s\"", word);
            continue;
        }
        /* a procedure that asks for the length of what it takes, or gives back more than one value */
        const Proc *called = s < end && *s == '(' ? proc_named(r, word) : NULL;
        const char *shut = called && (called->lengths || called->values > 1) ? closing(s, end) : NULL;
        if (shut) {
            strbuf_printf(&out, "@%s(", word);
            const char *from = s + 1, *lengths = "";
            int depth = 0, index = 0;
            for (const char *c = s + 1; c <= shut; c++) {
                if (*c == '[' || *c == '(') depth++;
                else if ((*c == ']' || *c == ')') && c < shut) depth--;
                if (c < shut && !(*c == ',' && depth == 0)) continue;
                while (from < c && *from == ' ') from++;
                if (c > from) strbuf_printf(&out, "%s%s", index ? ", " : "", expression(r, from, (size_t)(c - from)));
                if (called->lengths & (1u << index)) {
                    size_t n2 = (size_t)(c - from);
                    while (n2 && from[n2 - 1] == ' ') n2--;
                    const char *array = arena_strndup(r->arena, from, n2), *known = length_of(r, array);
                    if (!known) problem(r, "the length of '%s' is not known here: it is neither made here nor taken by the procedure", array);
                    lengths = arena_printf(r->arena, "%s, %s", lengths, known ? known : "0");
                }
                from = c + 1;
                index++;
            }
            strbuf_printf(&out, "%s%s%s)", lengths, r->targets ? ", " : "", r->targets ? r->targets : "");
            r->targets = NULL;
            s = shut + 1;
            continue;
        }
        if (!ours) strbuf_puts(&out, "@"); /* a name of the writer's, whatever word of ours it looks like */
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
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)(length < 31 ? length : 31), s);
        for (char *c = name; *c; c++)
            if (*c == '\'') *c = '_';
        if (length && !close && (s + length == be || s[length] == ' ' || s[length] == ',')) { /* let x be a new node */
            strbuf_printf(&out, "%s@%s <- new_object_()", out.length ? "; " : "", name);
            s += length;
            continue;
        }
        if (!close) {
            ok = false;
            break;
        }
        strbuf_printf(&out, "%sDimension @%s[", out.length ? "; " : "", name);
        const char *first_size = NULL;
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
            if (!dim) first_size = arena_printf(r->arena, from_zero ? "(%s)+1" : "%s", high);
            from = c + 1;
            dim++;
        }
        strbuf_puts(&out, "]");
        if (first_size && r->sized_count < 16) {
            snprintf(r->sized[r->sized_count], sizeof r->sized[0], "%s", name);
            r->sizes[r->sized_count++] = first_size;
        }
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
        const char *each = after_word(rest, "each");
        if (each || word_in(rest, "in") || strstr(rest, "\xe2\x88\x88")) { /* for each x in A: one after the other */
            const char *from = each ? each : rest, *in = word_in(from, "in"), *sign = strstr(from, "\xe2\x88\x88");
            const char *cut = sign && (!in || sign < in) ? sign : in;
            if (cut) {
                const char *array = cut + (cut == in ? 2 : 3);
                while (*array == ' ') array++;
                array = arena_strndup(r->arena, array, without(array, strlen(array), "do"));
                const char *end = cut;
                while (end > from && end[-1] == ' ') end--;
                const char *name = end;
                while (name > from && (is_word(name[-1]) || name[-1] == '\'')) name--;
                const char *known = name < end ? length_of(r, array) : NULL;
                if (known) {
                    const char *index = arena_printf(r->arena, "each_%d_", r->depth);
                    push(r, 'f', indent);
                    return arena_printf(r->arena, "Para %s <- 1 Hasta %s Hacer %s <- %s", index, known, expression(r, name, (size_t)(end - name)),
                                        all(r, arena_printf(r->arena, "%s[%s]", array, index)));
                }
                /* a collection: from its first to its last */
                const char *at = arena_printf(r->arena, "at_%d_", r->depth), *through = all(r, array), *takes = NULL;
                if (name < end) {
                    takes = arena_printf(r->arena, "%s <- item_(%s, %s)", expression(r, name, (size_t)(end - name)), at, through);
                } else if (end > from && end[-1] == ')') { /* for each edge (u, v) ∈ G.E */
                    const char *open = end - 1;
                    while (open > from && *open != '(') open--;
                    const char *comma = pair_comma(open, (size_t)(end - open));
                    if (comma)
                        takes = arena_printf(r->arena, "%s <- item_(%s); %s <- item2_(%s)", expression(r, open + 1, (size_t)(comma - open - 1)), at,
                                             expression(r, comma + 1, (size_t)(end - 1 - comma - 1)), at);
                }
                if (takes) {
                    push(r, 'w', indent);
                    return arena_printf(r->arena, "%s <- first_(%s); Mientras more_(%s) Hacer %s; %s <- next_(%s)", at, through, at, takes, at, at);
                }
            }
            problem(r, "this 'for each' is not understood: write 'for each x in S'%.0s", "");
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
        const char *inner = same_line ? statement(r, same_line, indent + (int)(same_line - s)) : "";
        (void)before;
        return arena_printf(r->arena, "%sSi (%s) Entonces %s", otherwise ? "SiNo " : "", condition, inner);
    }
    if ((rest = after_word(s, "else"))) {
        Open *top = r->depth ? &r->open[r->depth - 1] : NULL;
        if (!top || top->kind != 'i' || top->indent != indent) {
            problem(r, "'%s' has nothing above it to go with", "else");
            return "";
        }
        /* what "else" has on its line begins a block where it stands: the book puts under it what belongs to it */
        return arena_printf(r->arena, "SiNo %s", *rest ? statement(r, rest, indent + (int)(rest - s)) : "");
    }
    if ((rest = after_word(s, "return"))) {
        if (!*rest) return "Retornar";
        if (r->proc && r->proc->values > 1) { /* each value into its own place, given by who called */
            size_t n2 = strlen(rest), named2 = 0;
            while (is_word(rest[named2]) || (rest[named2] == '-' && isalpha((unsigned char)rest[named2 + 1]))) named2++;
            if (named2 && named2 < 64 && rest[named2] == '(' && closing(rest + named2, rest + n2) == rest + n2 - 1) {
                char name[64]; /* return F(x), where F too gives back several: they are handed on */
                snprintf(name, sizeof name, "%.*s", (int)named2, rest);
                for (char *c = name; *c; c++)
                    if (*c == '-') *c = '_';
                const Proc *from = proc_named(r, name);
                if (from && from->values == r->proc->values) {
                    StrBuf places = {0};
                    strbuf_puts(&places, "");
                    for (int i = 1; i <= from->values; i++) strbuf_printf(&places, "%sout%d_", i > 1 ? ", " : "", i);
                    r->targets = arena_strndup(r->arena, places.data, places.length);
                    strbuf_free(&places);
                    const char *call = all(r, rest);
                    r->targets = NULL;
                    return arena_printf(r->arena, "%s; Retornar", call);
                }
            }
            if (*rest == '(' && closing(rest, rest + n2) == rest + n2 - 1) rest++, n2 -= 2;
            StrBuf out = {0};
            strbuf_puts(&out, "");
            const char *from = rest;
            int depth = 0, index = 0;
            for (const char *c = rest; c <= rest + n2; c++) {
                if (c < rest + n2 && (*c == '(' || *c == '[')) depth++;
                else if (c < rest + n2 && (*c == ')' || *c == ']')) depth--;
                if (c < rest + n2 && !(*c == ',' && depth == 0)) continue;
                strbuf_printf(&out, "out%d_ <- %s; ", ++index, expression(r, from, (size_t)(c - from)));
                from = c + 1;
            }
            if (index != r->proc->values) problem(r, "'%s' gives back a different number of values here than elsewhere", r->proc->name);
            const char *kept = arena_printf(r->arena, "%sRetornar", out.data);
            strbuf_free(&out);
            return kept;
        }
        return arena_printf(r->arena, "res_ <- %s; Retornar", all(r, rest));
    }
    if ((rest = after_word(s, "error"))) return arena_printf(r->arena, "Escribir \"error: \", %s; Retornar", *rest ? all(r, rest) : "\"\"");
    if ((rest = after_word(s, "print"))) { /* one thing after the other, a blank between them */
        StrBuf out = {0};
        strbuf_puts(&out, "Escribir ");
        const char *from = rest;
        int depth = 0;
        for (const char *c = rest;; c++) {
            if (*c == '"') {
                for (c++; *c && *c != '"'; c++) {}
                if (!*c) c--;
            } else if (*c == '(' || *c == '[' || *c == '{') {
                depth++;
            } else if (*c == ')' || *c == ']' || *c == '}') {
                depth--;
            }
            if (*c && !(*c == ',' && depth == 0)) continue;
            const char *start = from, *last = c;
            while (start < c && *start == ' ') start++;
            while (last > start && last[-1] == ' ') last--;
            /* no blank beside a text that brings its own, or is nothing */
            bool own = from > rest && ((start + 1 < last && start[0] == '"' && (start[1] == ' ' || start[1] == '"')) ||
                                       (out.length > 2 && out.data[out.length - 1] == '"' && (out.data[out.length - 2] == ' ' || out.data[out.length - 2] == '"')));
            strbuf_printf(&out, "%s%s", from == rest ? "" : own ? ", " : ", \" \", ", expression(r, start, (size_t)(last - start)));
            from = c + 1;
            if (!*c) break;
        }
        const char *kept = arena_strndup(r->arena, out.data, out.length);
        strbuf_free(&out);
        return kept;
    }
    if ((rest = after_word(s, "exchange")) || (rest = after_word(s, "swap"))) {
        const char *with = word_in(rest, "with");
        if (with) {
            const char *a = expression(r, rest, (size_t)(with - rest)), *b = all(r, with + 4);
            return arena_printf(r->arena, "tmp_ <- %s; %s <- %s; %s <- tmp_", a, a, b, b);
        }
    }
    if ((rest = after_word(s, "let"))) return let(r, rest);
    /* ENQUEUE(Q, x), PUSH(S, x): what is done with queues and stacks, if the writer has no procedure of that name */
    size_t named = 0;
    while (is_word(s[named]) || (s[named] == '-' && isalpha((unsigned char)s[named + 1]))) named++;
    if (named && named < 32 && s[named] == '(' && closing(s + named, s + n) == s + n - 1) {
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)named, s);
        for (char *c = name; *c; c++)
            if (*c == '-') *c = '_';
        const char *comma = pair_comma(s + named, n - named);
        if (!proc_named(r, name)) {
            if (comma && (strcasecmp(name, "ENQUEUE") == 0 || strcasecmp(name, "PUSH") == 0 || strcasecmp(name, "INSERT") == 0)) {
                const char *into = expression(r, s + named + 1, (size_t)(comma - s - named - 1));
                return arena_printf(r->arena, "%s <- put_last_(%s, %s)", into, into, element(r, comma + 1, (size_t)(s + n - 1 - comma - 1)));
            }
            if (strcasecmp(name, "DECREASE_KEY") == 0 || strcasecmp(name, "INCREASE_KEY") == 0) return ""; /* the least is looked for each time */
            for (size_t i = 0; i < 4; i++)
                if (strcasecmp(name, TAKES[i][0]) == 0) return arena_printf(r->arena, "drop_ <- %s", all(r, s));
        }
    }
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
    if (count == 1 && *s == '(' && closing(s, cuts[0]) && closing(s, cuts[0]) + 1 + strspn(closing(s, cuts[0]) + 1, " ") == cuts[0]) {
        const char *close = closing(s, cuts[0]); /* (a, b) = F(x): F fills them in */
        r->targets = expression(r, s + 1, (size_t)(close - s - 1));
        const char *call = all(r, cuts[0] + (*cuts[0] == '=' ? 1 : 3));
        if (r->targets) problem(r, "several values at once come from a procedure that gives back several%.0s", "");
        r->targets = NULL;
        return call;
    }
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

static void note(Rewriter *r, int context, const char *name, size_t n, char what) {
    static const char *const WORDS[] = {"and", "or", "not", "mod", "NIL", "TRUE", "FALSE", "nil", "true", "false", "do", "then", "to", "downto", "by",
                                        "with", "in", NULL};
    char word[32];
    if (!n || n >= sizeof word) return;
    snprintf(word, sizeof word, "%.*s", (int)n, name);
    for (char *c = word; *c; c++)
        if (*c == '\'') *c = '_';
    for (int i = 0; WORDS[i]; i++)
        if (strcmp(WORDS[i], word) == 0) return;
    Seen *seen = NULL;
    for (int i = 0; i < r->seen_count && !seen; i++)
        if (r->seen[i].context == context && strcmp(r->seen[i].name, word) == 0) seen = &r->seen[i];
    if (!seen) {
        if (r->seen_count == MAX_SEEN) return;
        seen = &r->seen[r->seen_count++];
        *seen = (Seen){.context = context};
        snprintf(seen->name, sizeof seen->name, "%s", word);
    }
    if (!seen->first) seen->first = what == 'r' ? 'r' : 'w';
    if (what == 'w') seen->assigned = true;
    if (what == 'a') seen->array = true;
}

/* The names a piece of text reads, and the calls it makes. Attributes are not names. */
static void reads(Rewriter *r, int context, const char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        if (s[i] == '"') {
            for (i++; i < n && s[i] != '"'; i++) {}
            i++;
            continue;
        }
        if (!isalpha((unsigned char)s[i]) && s[i] != '_') {
            i++;
            continue;
        }
        size_t k = i;
        while (k < n && (is_word(s[k]) || s[k] == '\'')) k++;
        size_t whole = k;
        while (whole + 1 < n && s[whole] == '-' && isalpha((unsigned char)s[whole + 1])) {
            whole++;
            while (whole < n && is_word(s[whole])) whole++;
        }
        size_t after = whole;
        while (after < n && s[after] == ' ') after++;
        if (!(after < n && s[after] == '(')) whole = k, after = k;
        while (after < n && s[after] == ' ') after++;
        bool attribute = i > 0 && s[i - 1] == '.', call = after < n && s[after] == '(';
        const char *close = call ? closing(s + after, s + n) : NULL;
        if (close && !attribute && r->call_count < MAX_CALLS && whole - i < 64) { /* what it hands on, when it is a name */
            Call *made = &r->calls[r->call_count++];
            *made = (Call){.context = context};
            for (size_t c = 0; c < whole - i; c++) made->name[c] = s[i + c] == '-' || s[i + c] == '\'' ? '_' : s[i + c];
            const char *from = s + after + 1;
            int depth = 0;
            for (const char *c = from; c <= close && made->count < 12; c++) {
                if (*c == '(' || *c == '[' || *c == '{') depth++;
                else if ((*c == ')' || *c == ']' || *c == '}') && c < close) depth--;
                if (c < close && !(*c == ',' && depth == 0)) continue;
                while (from < c && *from == ' ') from++;
                size_t length = (size_t)(c - from);
                while (length && from[length - 1] == ' ') length--;
                bool name = length > 0 && length < 32;
                for (size_t x = 0; x < length; x++) name = name && is_word(from[x]);
                if (name) snprintf(made->args[made->count], 32, "%.*s", (int)length, from);
                made->count++;
                from = c + 1;
            }
        }
        if (!attribute && !call) note(r, context, s + i, k - i, 'r');
        i = whole > k ? whole : k;
    }
}

/* What a line does with the names in it: which it reads, which it fills in. What is read comes first. */
static void line_names(Rewriter *r, int context, const char *s) {
    const char *rest;
    while (*s == ' ') s++;
    if ((rest = after_word(s, "for"))) {
        const char *each = after_word(rest, "each");
        const char *from = each ? each : rest, *in = word_in(from, "in"), *sign = strstr(from, "\xe2\x88\x88");
        const char *cut = sign && (!in || sign < in) ? sign : in;
        if (each || cut) {
            if (!cut) return;
            reads(r, context, cut + (cut == in ? 2 : 3), strlen(cut + (cut == in ? 2 : 3)));
            const char *end = cut;
            while (end > from && end[-1] == ' ') end--;
            const char *open = end;
            if (end > from && end[-1] == ')')
                while (open > from && *open != '(') open--;
            for (const char *c = open < end ? open : end; c < end || c == end;) { /* the name, or the two between brackets */
                const char *name = end;
                if (open < end) {
                    while (c < end && !is_word(*c)) c++;
                    name = c;
                    while (c < end && is_word(*c)) c++;
                    if (c > name) note(r, context, name, (size_t)(c - name), 'f');
                    if (c >= end - 1) break;
                } else {
                    while (name > from && (is_word(name[-1]) || name[-1] == '\'')) name--;
                    note(r, context, name, (size_t)(end - name), 'f');
                    break;
                }
            }
            return;
        }
        const char *gets = strstr(rest, "\xe2\x86\x90"), *equals = gets ? gets : strchr(rest, '=');
        if (!equals) return;
        reads(r, context, equals, strlen(equals));
        size_t length = 0;
        while (is_word(rest[length]) || rest[length] == '\'') length++;
        note(r, context, rest, length, 'f');
        return;
    }
    if ((rest = after_word(s, "let"))) {
        const char *be = word_in(rest, "be");
        while (be && rest < be) {
            while (rest < be && (*rest == ' ' || *rest == ',')) rest++;
            const char *and = after_word(rest, "and");
            if (and) rest = and;
            size_t length = 0;
            while (rest + length < be && (is_word(rest[length]) || rest[length] == '\'')) length++;
            if (!length) break;
            const char *close = rest[length] == '[' ? closing(rest + length, be) : NULL;
            if (close) reads(r, context, rest + length, (size_t)(close - rest - length));
            note(r, context, rest, length, close ? 'a' : 'w');
            rest = close ? close + 1 : rest + length;
        }
        return;
    }
    bool otherwise = (rest = after_word(s, "elseif")) || (rest = after_word(s, "elif")) || ((rest = after_word(s, "else")) && (rest = after_word(rest, "if")));
    if (otherwise || (rest = after_word(s, "if"))) {
        const char *then = word_in(rest, "then");
        reads(r, context, rest, then ? (size_t)(then - rest) : strlen(rest));
        if (then) line_names(r, context, then + 4);
        return;
    }
    if ((rest = after_word(s, "else"))) {
        line_names(r, context, rest);
        return;
    }
    static const char *const READING[] = {"while", "until", "return", "print", "error", "exchange", "swap", "repeat", NULL};
    for (int i = 0; READING[i]; i++) {
        if (!(rest = after_word(s, READING[i]))) continue;
        reads(r, context, rest, strlen(rest));
        return;
    }
    const char *cuts[8];
    int count = 0, depth = 0;
    for (const char *c = s; *c && count < 8; c++) {
        if (*c == '"') {
            for (c++; *c && *c != '"'; c++) {}
            if (!*c) break;
        } else if (*c == '(' || *c == '[' || *c == '{') {
            depth++;
        } else if (*c == ')' || *c == ']' || *c == '}') {
            depth--;
        } else if (depth == 0 && strncmp(c, "\xe2\x86\x90", 3) == 0) {
            cuts[count++] = c;
            c += 2;
        } else if (depth == 0 && *c == '=' && c[1] != '=' && c > s && !strchr("<>!=", c[-1])) {
            cuts[count++] = c;
        }
    }
    if (!count) {
        reads(r, context, s, strlen(s));
        return;
    }
    const char *value = cuts[count - 1] + (*cuts[count - 1] == '=' ? 1 : 3);
    reads(r, context, value, strlen(value));
    for (int pass = 0; pass < 2; pass++) { /* what is looked into, then what is filled in */
        for (int i = 0; i < count; i++) {
            const char *from = i ? cuts[i - 1] + (*cuts[i - 1] == '=' ? 1 : 3) : s, *end = cuts[i];
            while (from < end && *from == ' ') from++;
            while (end > from && end[-1] == ' ') end--;
            bool name = end > from, pair = pair_comma(from, (size_t)(end - from)) != NULL;
            for (const char *c = from; c < end; c++) name = name && (is_word(*c) || *c == '\'');
            if (!name && !pair && pass == 0) reads(r, context, from, (size_t)(end - from));
            if (name && pass == 1) note(r, context, from, (size_t)(end - from), 'w');
            for (const char *c = from; pair && pass == 1 && c < end;) {
                while (c < end && !is_word(*c)) c++;
                const char *word = c;
                while (c < end && is_word(*c)) c++;
                if (c > word) note(r, context, word, (size_t)(c - word), 'w');
            }
        }
    }
}

/* A first reading: the procedures there are, what each takes, how many values
 * it gives back, which of what it takes are arrays, and the names the
 * procedures share: those one reads before putting anything in them, and
 * another fills in. */
static void survey(Rewriter *r, const char *text) {
    Proc *now = NULL;
    int head_indent = 0;
    r->seen = arena_alloc(r->arena, MAX_SEEN * sizeof(Seen));
    r->calls = arena_alloc(r->arena, MAX_CALLS * sizeof(Call));
    for (const char *line = text; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] == '\n')) {
        size_t n = strcspn(line, "\n");
        const char *code, *comment;
        size_t length;
        int indent = measure(line, n, &code, &length, &comment);
        if (!length) continue;
        bool deeper = false;
        for (const char *next = line + n + (line[n] == '\n'); *next; next += strcspn(next, "\n") + (next[strcspn(next, "\n")] == '\n')) {
            const char *c, *k;
            size_t l;
            int at = measure(next, strcspn(next, "\n"), &c, &l, &k);
            if (!l) continue;
            deeper = at > indent;
            break;
        }
        char *copy = arena_strndup(r->arena, code, length);
        if (deeper && heading(code, length) && !word_in(copy, "=") && (!now || indent <= head_indent) && r->proc_count < 64) {
            now = &r->procs[r->proc_count++];
            head_indent = indent;
            size_t i = 0;
            for (; i < length && code[i] != '(' && code[i] != ' ' && i < 63; i++) now->name[i] = code[i] == '-' || code[i] == '\'' ? '_' : code[i];
            const char *open = memchr(code, '(', length);
            for (const char *c = open + 1; c < code + length - 1 && now->param_count < 12;) {
                while (c < code + length - 1 && (*c == ' ' || *c == ',')) c++;
                size_t k = 0;
                while (c + k < code + length - 1 && (is_word(c[k]) || c[k] == '\'')) k++;
                if (!k) break;
                snprintf(now->params[now->param_count++], 32, "%.*s", (int)(k < 31 ? k : 31), c);
                c += k;
            }
            continue;
        }
        if (!now || indent <= head_indent) now = NULL;
        line_names(r, now ? (int)(now - r->procs) : -1, copy);
        for (size_t i = 0; now && i < length; i++) { /* what the body asks of what it takes */
            const char *rest;
            if ((i == 0 || !is_word(code[i - 1])) && (rest = after_word(copy + i, "return")) && *rest) {
                size_t n2 = strlen(rest);
                if (*rest == '(' && closing(rest, rest + n2) == rest + n2 - 1) rest++, n2 -= 2;
                int values = 1, depth = 0;
                for (const char *c = rest; c < rest + n2; c++) {
                    if (*c == '(' || *c == '[') depth++;
                    else if (*c == ')' || *c == ']') depth--;
                    else if (*c == ',' && depth == 0) values++;
                }
                if (values > now->values) now->values = values;
            }
            for (int k = 0; k < now->param_count; k++) {
                size_t w = strlen(now->params[k]);
                if (strncmp(copy + i, now->params[k], w) != 0 || (i && (is_word(copy[i - 1]) || copy[i - 1] == '.'))) continue;
                if (strncmp(copy + i + w, ".length", 7) == 0 && !is_word(copy[i + w + 7])) now->asked |= 1u << k;
                if (copy[i + w] == '[') now->indexed |= 1u << k;
                const char *before = copy + i;
                while (before > copy && before[-1] == ' ') before--;
                bool gone_through = after_word(copy, "for") && !is_word(copy[i + w]) && copy[i + w] != '.' && copy[i + w] != '[' &&
                                    ((before - copy >= 2 && strncmp(before - 2, "in", 2) == 0) || (before - copy >= 3 && strncmp(before - 3, "\xe2\x88\x88", 3) == 0));
                if (gone_through) now->walked |= 1u << k;
            }
        }
    }
    /* an array is what is indexed, or asked its length, or handed in by who has one */
    for (int i = 0; i < r->proc_count; i++) r->procs[i].arrays = r->procs[i].asked | r->procs[i].indexed;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < r->call_count; i++) {
            const Call *call = &r->calls[i];
            Proc *callee = (Proc *)proc_named(r, call->name);
            for (int k = 0; callee && k < call->count && k < callee->param_count; k++) {
                bool array = false;
                for (int x = 0; x < r->seen_count; x++)
                    if (r->seen[x].context == call->context && r->seen[x].array && strcmp(r->seen[x].name, call->args[k]) == 0) array = true;
                const Proc *caller = call->context >= 0 ? &r->procs[call->context] : NULL;
                for (int x = 0; caller && x < caller->param_count; x++)
                    if ((caller->arrays & (1u << x)) && strcmp(caller->params[x], call->args[k]) == 0) array = true;
                if (array && !(callee->arrays & (1u << k))) callee->arrays |= 1u << k, changed = true;
            }
        }
    }
    for (int i = 0; i < r->proc_count; i++) r->procs[i].lengths = r->procs[i].asked | (r->procs[i].walked & r->procs[i].arrays);
    strbuf_puts(&r->shared, "");
    for (int i = 0; i < r->seen_count; i++) {
        const Seen *seen = &r->seen[i];
        if (seen->first != 'r') continue;
        bool taken = false, elsewhere = false, array = false, listed = false;
        for (int k = 0; seen->context >= 0 && k < r->procs[seen->context].param_count; k++)
            taken |= strcmp(r->procs[seen->context].params[k], seen->name) == 0;
        for (int k = 0; k < r->seen_count; k++) {
            if (strcmp(r->seen[k].name, seen->name) != 0) continue;
            array |= r->seen[k].array;
            elsewhere |= r->seen[k].context != seen->context && r->seen[k].assigned;
            listed |= k < i && r->seen[k].first == 'r';
        }
        if (taken || !elsewhere || array) continue;
        size_t n = strlen(seen->name);
        const char *found = r->shared.length ? strstr(r->shared.data, seen->name) : NULL;
        (void)listed;
        while (found && !((found == r->shared.data || found[-1] == ' ') && (found[n] == ' ' || !found[n]))) found = strstr(found + 1, seen->name);
        if (!found) strbuf_printf(&r->shared, "%s%s", r->shared.length ? " " : "", seen->name);
    }
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
    survey(r, written->text);
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
                char known[64] = "";
                for (size_t i = 0; i < length && text[i] != '(' && text[i] != ' ' && i < 63; i++) known[i] = text[i] == '-' || text[i] == '\'' ? '_' : text[i];
                r->proc = proc_named(r, known);
                value = value && (!r->proc || r->proc->values < 2);
                r->value = value;
                r->zero_count = r->sized_count = 0;
                push(r, 'p', indent);
                r->open[r->depth - 1].value = value;
                StrBuf head_text = {0};
                strbuf_printf(&head_text, value ? "Funcion res_ <- @%s(" : "SubProceso @%s(", known);
                int listed = 0;
                for (int i = 0; r->proc && i < r->proc->param_count; i++) strbuf_printf(&head_text, "%s@%s", listed++ ? ", " : "", r->proc->params[i]);
                for (int i = 0; r->proc && i < r->proc->param_count; i++)
                    if (r->proc->lengths & (1u << i)) strbuf_printf(&head_text, "%s@%s_length", listed++ ? ", " : "", r->proc->params[i]);
                for (int i = 1; r->proc && r->proc->values > 1 && i <= r->proc->values; i++) strbuf_printf(&head_text, "%sout%d_ Por Referencia", listed++ ? ", " : "", i);
                strbuf_puts(&head_text, ")");
                said = arena_strndup(arena, head_text.data, head_text.length);
                strbuf_free(&head_text);
            } else {
                if (!r->depth) { /* outside every procedure: the program itself */
                    if (r->main_done) problem(r, "the statements outside the procedures go together, in one place%.0s", "");
                    r->main_done = true;
                    r->zero_count = r->sized_count = 0;
                    r->value = false;
                    r->proc = NULL;
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
    config->shared = r->shared.length ? arena_strndup(arena, r->shared.data, r->shared.length) : NULL;
    strbuf_free(&r->shared);
    Source run = {written->name, arena_strndup(arena, r->out.data, r->out.length), r->out.length, written->shown ? written->shown : written->text};
    strbuf_free(&r->out);
    return run;
}
