#include "plike/blocks.h"

#include "plike/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && equals_ignore_case(s + n - m, suffix);
}

bool is_document_path(const char *path) {
    char file[1024];
    block_number(path, file, sizeof file);
    return ends_with(file, ".md") || ends_with(file, ".markdown") || ends_with(file, ".tex") || ends_with(file, ".latex");
}

int block_number(const char *path, char *file, size_t size) {
    const char *hash = strrchr(path, '#');
    int number = 0;
    if (hash && hash[1] >= '1' && hash[1] <= '9' && !strchr(hash, '/')) number = atoi(hash + 1);
    snprintf(file, size, "%.*s", number ? (int)(hash - path) : (int)strlen(path), path);
    return number;
}

static bool word_at(const char *s, const char *word) {
    size_t n = strlen(word);
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = word[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    char c = s[n];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
}

Dialect guess_dialect(const char *text) {
    const char *s = text;
    for (;;) { /* the first word that is not a comment */
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (s[0] == '/' && s[1] == '/') s += strcspn(s, "\n");
        else break;
    }
    if (word_at(s, "programa")) return DIALECT_PORTUGOL;
    if (word_at(s, "algoritmo")) {
        const char *after = s + 9;
        while (*after == ' ') after++;
        return *after == '"' ? DIALECT_VISUALG : DIALECT_PSEINT;
    }
    if (word_at(s, "proceso") || word_at(s, "subproceso") || word_at(s, "funcion") || word_at(s, "subalgoritmo") ||
        word_at(s, "definir"))
        return DIALECT_PSEINT;
    if (strstr(text, "fimalgoritmo") || strstr(text, "FimAlgoritmo")) return DIALECT_VISUALG;
    if (strstr(text, "FinAlgoritmo") || strstr(text, "FinProceso") || strstr(text, "finalgoritmo")) return DIALECT_PSEINT;
    static const char *const CAMBRIDGE[] = {"DECLARE ", "OUTPUT ", "ENDPROCEDURE", "ENDFUNCTION", "ENDWHILE", "ENDIF", "NEXT "};
    for (size_t i = 0; i < sizeof CAMBRIDGE / sizeof CAMBRIDGE[0]; i++)
        if (strstr(text, CAMBRIDGE[i])) return DIALECT_CAMBRIDGE;
    return DIALECT_PLIKE;
}

/* ---- what a block is, from the name it is given ---- */

typedef struct {
    const char *name;
    bool diagram;
    Dialect dialect;
} Known;

static const Known KNOWN[] = {
    {"mermaid", true, 0}, {"plantuml", true, 0}, {"puml", true, 0}, {"uml", true, 0}, {"dot", true, 0}, {"graphviz", true, 0},
    {"pseudocode", false, DIALECT_PLIKE}, {"pseudo", false, DIALECT_PLIKE}, {"pscope", false, DIALECT_PLIKE},
    {"pseudoscope", false, DIALECT_PLIKE}, {"plike", false, DIALECT_PLIKE}, {"p-like", false, DIALECT_PLIKE},
    {"pseint", false, DIALECT_PSEINT}, {"psc", false, DIALECT_PSEINT}, {"visualg", false, DIALECT_VISUALG},
    {"alg", false, DIALECT_VISUALG}, {"portugol", false, DIALECT_PORTUGOL}, {"por", false, DIALECT_PORTUGOL},
    {"cambridge", false, DIALECT_CAMBRIDGE}, {"igcse", false, DIALECT_CAMBRIDGE}, {"pse", false, DIALECT_CAMBRIDGE},
};

static const Known *known(const char *language) {
    for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0]; i++)
        if (equals_ignore_case(language, KNOWN[i].name)) return &KNOWN[i];
    return NULL;
}

/* ---- reading ---- */

typedef struct {
    Arena *arena;
    const Source *source;
    Blocks *blocks;
    char **lines;       /* of the document, without their ends */
    int line_count;
    const char *heading;
    int depths[8];      /* outline: the node of the last heading of each level */
    int root;
} Doc;

static void split_lines(Doc *d) {
    int count = 1;
    for (const char *c = d->source->text; *c; c++) count += *c == '\n';
    d->lines = arena_alloc(d->arena, (size_t)(count + 1) * sizeof(char *));
    for (const char *s = d->source->text; *s || d->line_count == 0;) {
        size_t n = strcspn(s, "\n");
        d->lines[d->line_count++] = arena_strndup(d->arena, s, n);
        s += n;
        if (*s) s++;
        else break;
    }
}

/* The document with only these lines left (given as they must read). */
static const char *masked(Doc *d, int first, int last, char **replaced) {
    StrBuf text = {0};
    strbuf_puts(&text, "");
    for (int i = 0; i < d->line_count; i++) {
        if (i >= first && i <= last) strbuf_puts(&text, replaced ? replaced[i - first] : d->lines[i]);
        strbuf_puts(&text, "\n");
    }
    const char *kept = arena_strndup(d->arena, text.data, text.length);
    strbuf_free(&text);
    return kept;
}

static DocBlock *add_block(Doc *d, const char *language, bool diagram, Dialect dialect, int first, int last, char **replaced) {
    Blocks *b = d->blocks;
    if (b->count == b->capacity) {
        int capacity = b->capacity ? b->capacity * 2 : 16;
        DocBlock *items = arena_alloc(d->arena, (size_t)capacity * sizeof(DocBlock));
        if (b->count) memcpy(items, b->items, (size_t)b->count * sizeof(DocBlock));
        b->items = items;
        b->capacity = capacity;
    }
    DocBlock *block = &b->items[b->count++];
    *block = (DocBlock){.diagram = diagram, .dialect = dialect, .language = language, .title = d->heading, .line = first + 1,
                     .lines = last - first + 1, .text = masked(d, first, last, replaced)};
    return block;
}

static void outline_node(Doc *d, const char *text, int level) {
    Uml *u = d->blocks->outline;
    if (!u) {
        u = d->blocks->outline = ARENA_NEW(d->arena, Uml);
        u->kind = UML_TREE;
        u->tree_kind = "outline";
        u->first_day = -1;
        u->node_capacity = 256;
        u->nodes = arena_alloc(d->arena, 256 * sizeof(UmlNode));
        u->edge_capacity = 256;
        u->edges = arena_alloc(d->arena, 256 * sizeof(UmlEdge));
        const char *slash = strrchr(d->source->name, '/');
        const char *name = slash ? slash + 1 : d->source->name;
        u->nodes[u->node_count++] = (UmlNode){name, name, UML_START, NULL, 1, -1, 0, 0};
        for (int i = 0; i < 8; i++) d->depths[i] = 0;
    }
    if (u->node_count >= 255 || level < 1) return;
    if (level > 7) level = 7;
    int parent = d->depths[level - 1];
    int depth = u->nodes[parent].depth + 1;
    u->nodes[u->node_count] = (UmlNode){text, text, UML_PLAIN, NULL, 0, parent, depth, 0};
    u->edges[u->edge_count++] = (UmlEdge){parent, u->node_count, "", false, 0};
    for (int i = level; i < 8; i++) d->depths[i] = u->node_count;
    u->node_count++;
}

static const char *trimmed(Doc *d, const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t')) s++, n--;
    while (n && strchr(" \t\r#", s[n - 1])) n--;
    return arena_strndup(d->arena, s, n);
}

/* ---- Markdown ---- */

static void read_markdown(Doc *d) {
    for (int i = 0; i < d->line_count; i++) {
        const char *l = d->lines[i];
        int indent = 0;
        while (l[indent] == ' ' && indent < 3) indent++;
        const char *s = l + indent;
        if (*s == '#') {
            int level = 0;
            while (s[level] == '#') level++;
            if (level <= 6 && (s[level] == ' ' || s[level] == '\t')) {
                d->heading = trimmed(d, s + level, strlen(s + level));
                outline_node(d, d->heading, level);
            }
            continue;
        }
        if (strncmp(s, "```", 3) != 0 && strncmp(s, "~~~", 3) != 0) continue;
        char fence = *s;
        int run = 0;
        while (s[run] == fence) run++;
        char language[40];
        const char *info = s + run;
        while (*info == ' ' || *info == '{' || *info == '.') info++;
        snprintf(language, sizeof language, "%.*s", (int)strcspn(info, " \t\r}{,"), info);
        int first = i + 1, last = first - 1;
        for (i = first; i < d->line_count; i++) {
            const char *e = d->lines[i];
            while (*e == ' ') e++;
            int closing = 0;
            while (e[closing] == fence) closing++;
            if (closing >= run && !e[closing + strspn(e + closing, " \t\r")]) break;
            last = i;
        }
        const Known *k = known(language);
        if (last < first) continue;
        if (k) {
            add_block(d, arena_strdup(d->arena, language), k->diagram, k->dialect, first, last, NULL);
        } else if (!language[0]) { /* not said: a diagram tells by itself what it is */
            const char *text = masked(d, first, last, NULL);
            if (looks_like_uml(text)) add_block(d, "diagram", true, 0, first, last, NULL);
        }
    }
}

/* ---- LaTeX ---- */

/* What is between the braces that begin at s, which may hold others. */
static const char *braces(Doc *d, const char *s, const char **after) {
    while (*s == ' ') s++;
    if (*s != '{') {
        if (after) *after = s;
        return NULL;
    }
    int depth = 0;
    const char *c = s;
    for (; *c; c++) {
        if (*c == '{' && (c == s || c[-1] != '\\')) depth++;
        if (*c == '}' && c[-1] != '\\' && --depth == 0) break;
    }
    if (after) *after = *c ? c + 1 : c;
    return arena_strndup(d->arena, s + 1, (size_t)(c - s - 1));
}

static void replace_all(StrBuf *text, const char *from, const char *to) {
    size_t n = strlen(from);
    StrBuf out = {0};
    strbuf_puts(&out, "");
    for (const char *s = text->data; *s;) {
        bool letter = from[0] == '\\' && ((from[n - 1] >= 'a' && from[n - 1] <= 'z') || (from[n - 1] >= 'A' && from[n - 1] <= 'Z'));
        char next = s[n < strlen(s) ? n : strlen(s)];
        bool whole = !letter || !((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'));
        if (strncmp(s, from, n) == 0 && whole) {
            strbuf_puts(&out, to);
            s += n;
        } else {
            strbuf_append(&out, s++, 1);
        }
    }
    text->length = 0;
    text->data[0] = '\0';
    strbuf_append(text, out.data, out.length);
    strbuf_free(&out);
}

/* \name{x}: x, with what is given before and after it. */
static void unwrap(StrBuf *text, const char *command, const char *before, const char *after) {
    size_t n = strlen(command);
    for (;;) {
        char *at = strstr(text->data, command);
        if (!at || at[n] != '{') return;
        int depth = 0;
        char *c = at + n;
        for (; *c; c++) {
            if (*c == '{') depth++;
            if (*c == '}' && --depth == 0) break;
        }
        StrBuf out = {0};
        strbuf_puts(&out, "");
        strbuf_append(&out, text->data, (size_t)(at - text->data));
        strbuf_puts(&out, before);
        strbuf_append(&out, at + n + 1, (size_t)(c - at - n - 1));
        strbuf_puts(&out, after);
        if (*c) strbuf_puts(&out, c + 1);
        text->length = 0;
        text->data[0] = '\0';
        strbuf_append(text, out.data, out.length);
        strbuf_free(&out);
    }
}

/* Mathematics and typography, as plain pseudocode. */
static const char *plain_math(Doc *d, const char *s) {
    static const char *const PAIRS[][2] = {
        {"\\gets", "<-"}, {"\\leftarrow", "<-"}, {":=", "<-"}, {"\\leq", "<="}, {"\\le", "<="}, {"\\geq", ">="}, {"\\ge", ">="},
        {"\\neq", "<>"}, {"\\ne", "<>"}, {"\\land", " Y "}, {"\\wedge", " Y "}, {"\\lor", " O "}, {"\\vee", " O "},
        {"\\neg", " NO "}, {"\\lnot", " NO "}, {"\\cdot", "*"}, {"\\times", "*"}, {"\\div", "/"}, {"\\bmod", " MOD "},
        {"\\mod", " MOD "}, {"\\%", " MOD "}, {"\\lfloor", "trunc("}, {"\\rfloor", ")"}, {"\\True", "Verdadero"},
        {"\\False", "Falso"}, {"\\To", " Hasta "}, {"\\DownTo", " downto "}, {"\\,", " "}, {"\\;", " "}, {"\\ ", " "},
        {"\\quad", " "}, {"\\left", ""}, {"\\right", ""}, {"\\{", "{"}, {"\\}", "}"}, {"\\_", "_"}, {"\\(", ""}, {"\\)", ""},
        {"$", ""}, {"~", " "}, {"==", "="}, {"\\sqrt", "rc"}, {"\\infty", "1000000000"},
    };
    static const char *const WRAPS[] = {"\\textbf", "\\textit", "\\textsc", "\\texttt", "\\text", "\\mathit", "\\mathrm", "\\mathbf",
                                        "\\emph", "\\mathsf", "\\textrm", "\\operatorname"};
    StrBuf text = {0};
    strbuf_puts(&text, s);
    for (int guard = 0; guard < 8 && strstr(text.data, "\\Call{"); guard++) { /* \Call{F}{x} -> F(x) */
        char *at = strstr(text.data, "\\Call{");
        const char *after;
        const char *name = braces(d, at + 5, &after), *args = braces(d, after, &after);
        StrBuf out = {0};
        strbuf_puts(&out, "");
        strbuf_append(&out, text.data, (size_t)(at - text.data));
        strbuf_printf(&out, "%s(%s)%s", name ? name : "", args ? args : "", after);
        text.length = 0;
        text.data[0] = '\0';
        strbuf_append(&text, out.data, out.length);
        strbuf_free(&out);
    }
    unwrap(&text, "_", "[", "]");
    unwrap(&text, "\\Comment", " // ", "");
    for (size_t i = 0; i < sizeof WRAPS / sizeof WRAPS[0]; i++) unwrap(&text, WRAPS[i], "", "");
    for (size_t i = 0; i < sizeof PAIRS / sizeof PAIRS[0]; i++) replace_all(&text, PAIRS[i][0], PAIRS[i][1]);
    const char *kept = arena_strndup(d->arena, text.data, text.length);
    strbuf_free(&text);
    return kept;
}

/* A word of the pseudocode written out, at the beginning of a statement. */
static const char *spoken(Doc *d, const char *statement, bool function) {
    const char *s = statement;
    while (*s == ' ') s++;
    if (word_at(s, "return")) {
        const char *e = s + 6;
        while (*e == ' ') e++;
        if (!function || !*e) return "Retornar";
        return arena_printf(d->arena, "res_ <- %s; Retornar", e);
    }
    if (word_at(s, "print") || word_at(s, "output") || word_at(s, "write")) return arena_printf(d->arena, "Escribir %s", s + strcspn(s, " "));
    if (word_at(s, "read") || word_at(s, "input")) return arena_printf(d->arena, "Leer %s", s + strcspn(s, " "));
    return s;
}

/* One line of an algorithmic environment, as PSeInt would write it. */
static const char *algorithmic_line(Doc *d, const char *line, bool *function, bool *inside, bool *main_open) {
    StrBuf clean = {0};
    strbuf_puts(&clean, "");
    for (const char *c = line; *c; c++) { /* without its comment */
        if (*c == '%' && (c == line || c[-1] != '\\')) break;
        strbuf_append(&clean, c, 1);
    }
    const char *s = clean.data;
    while (*s == ' ' || *s == '\t') s++;
    const char *result = "";
    char command[24] = "";
    if (*s == '\\') {
        size_t n = 1;
        while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z')) n++;
        snprintf(command, sizeof command, "%.*s", (int)(n - 1 < 23 ? n - 1 : 23), s + 1);
        s += n;
    }
    for (char *c = command; *c; c++)
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    const char *after = s;
    bool statement = false;
    if (!strcmp(command, "function") || !strcmp(command, "procedure")) {
        const char *name = braces(d, s, &after), *params = braces(d, after, &after);
        bool is_function = command[0] == 'f';
        result = arena_printf(d->arena, is_function ? "%sFuncion res_ <- %s(%s)" : "%sSubProceso %s(%s)", *main_open ? "FinAlgoritmo " : "",
                              name ? plain_math(d, name) : "f", params ? plain_math(d, params) : "");
        *function = is_function;
        *inside = true;
        *main_open = false;
    } else if (!strcmp(command, "endfunction") || !strcmp(command, "endprocedure")) {
        result = command[3] == 'f' ? "FinFuncion" : "FinSubProceso";
        *inside = false;
    } else if (!strcmp(command, "if") || !strcmp(command, "elsif") || !strcmp(command, "elseif") || !strcmp(command, "while") ||
               !strcmp(command, "until") || !strcmp(command, "for") || !strcmp(command, "forall")) {
        const char *inner = braces(d, s, &after);
        const char *text = plain_math(d, inner ? inner : "");
        statement = true;
        if (command[0] == 'i') result = arena_printf(d->arena, "Si %s Entonces", text);
        else if (command[0] == 'e') result = arena_printf(d->arena, "SiNoSi %s Entonces", text);
        else if (command[0] == 'w') result = arena_printf(d->arena, "Mientras %s Hacer", text);
        else if (command[0] == 'u') result = arena_printf(d->arena, "Hasta Que %s", text);
        else { /* i <- 1 to n */
            StrBuf header = {0};
            strbuf_puts(&header, text);
            bool down = strstr(header.data, " downto ") != NULL;
            replace_all(&header, " downto ", " Hasta ");
            replace_all(&header, " to ", " Hasta ");
            char *equals = strchr(header.data, '=');
            char *arrow = strstr(header.data, "<-");
            if (equals && !arrow && equals > header.data && equals[-1] != '<' && equals[-1] != '>') {
                *equals = '<';
                StrBuf fixed = {0};
                strbuf_puts(&fixed, "");
                strbuf_append(&fixed, header.data, (size_t)(equals - header.data + 1));
                strbuf_puts(&fixed, "-");
                strbuf_puts(&fixed, equals + 1);
                strbuf_free(&header);
                header = fixed;
            }
            result = arena_printf(d->arena, "Para %s%s Hacer", header.data, down ? " Con Paso -1" : "");
            strbuf_free(&header);
        }
    } else if (!strcmp(command, "else")) {
        result = "SiNo";
        statement = true;
    } else if (!strcmp(command, "endif")) {
        result = "FinSi";
    } else if (!strcmp(command, "endwhile") || !strcmp(command, "endloop")) {
        result = "FinMientras";
    } else if (!strcmp(command, "endfor")) {
        result = "FinPara";
    } else if (!strcmp(command, "repeat")) {
        result = "Repetir";
        statement = true;
    } else if (!strcmp(command, "loop")) {
        result = "Mientras Verdadero Hacer";
        statement = true;
    } else if (!strcmp(command, "state") || !strcmp(command, "statex") || !strcmp(command, "return") || !strcmp(command, "print") ||
               !strcmp(command, "output") || !strcmp(command, "call")) {
        const char *text = !strcmp(command, "call") ? arena_printf(d->arena, "\\Call%s", s) : s;
        if (!strcmp(command, "return")) text = arena_printf(d->arena, "return %s", s);
        if (!strcmp(command, "print") || !strcmp(command, "output")) text = arena_printf(d->arena, "print %s", s);
        while (*text == ' ') text++;
        if (!strncmp(text, "\\Return", 7) || !strncmp(text, "\\RETURN", 7)) text = arena_printf(d->arena, "return %s", text + 7);
        if (!strncmp(text, "\\Print", 6) || !strncmp(text, "\\PRINT", 6)) text = arena_printf(d->arena, "print %s", text + 6);
        result = spoken(d, plain_math(d, text), *function);
        statement = *result != '\0';
    } else if (command[0]) {
        result = arena_printf(d->arena, "// %s", plain_math(d, s)); /* \Require, \Ensure and what is not known */
    } else if (*s) {
        result = spoken(d, plain_math(d, s), *function);
        statement = true;
    }
    /* statements outside any subprogram are the algorithm itself */
    if (statement && !*inside && !*main_open) {
        result = arena_printf(d->arena, "Algoritmo principal %s", result);
        *main_open = true;
    }
    strbuf_free(&clean);
    return result;
}

/* pgfgantt: \ganttbar{Design}{1}{5}, as the plans of PlantUML. */
static const char *gantt_line(Doc *d, const char *line, const char **names, int *count) {
    const char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    bool bar = !strncmp(s, "\\ganttbar", 9), group = !strncmp(s, "\\ganttgroup", 11), milestone = !strncmp(s, "\\ganttmilestone", 15);
    if (!strncmp(s, "\\ganttlink", 10)) {
        const char *after;
        const char *a = braces(d, strchr(s, '{') ? strchr(s, '{') : s, &after), *b = braces(d, after, &after);
        int from = a && !strncmp(a, "elem", 4) ? atoi(a + 4) : -1, to = b && !strncmp(b, "elem", 4) ? atoi(b + 4) : -1;
        if (from < 0 || to < 0 || from >= *count || to >= *count) return "";
        return arena_printf(d->arena, "[%s] -> [%s]", names[from], names[to]);
    }
    if (!bar && !group && !milestone) return "";
    const char *open = strchr(s, '{');
    if (strchr(s, '[') && strchr(s, '[') < open) open = strchr(strchr(s, ']') ? strchr(s, ']') : s, '{');
    if (!open) return "";
    const char *after;
    const char *name = braces(d, open, &after), *from = braces(d, after, &after), *to = milestone ? from : braces(d, after, &after);
    if (!name || !from || !to) return "";
    if (*count < 128) names[(*count)++] = name;
    int a = atoi(from), b = atoi(to);
    if (group) return arena_printf(d->arena, "-- %s --", name);
    if (milestone) return arena_printf(d->arena, "[%s] happens D+%d", name, a);
    return arena_printf(d->arena, "[%s] starts D+%d and lasts %d days", name, a - 1, b - a + 1);
}

static void read_latex(Doc *d) {
    static const struct {
        const char *command;
        int level;
    } HEADINGS[] = {{"\\part", 1}, {"\\chapter", 1}, {"\\section", 2}, {"\\subsection", 3}, {"\\subsubsection", 4}, {"\\paragraph", 5}};
    const char *caption = NULL;
    int first_in_float = -1;
    d->blocks->latex = true;
    for (int i = 0; i < d->line_count; i++) {
        const char *s = d->lines[i];
        while (*s == ' ' || *s == '\t') s++;
        for (size_t k = 0; k < sizeof HEADINGS / sizeof HEADINGS[0]; k++) {
            size_t n = strlen(HEADINGS[k].command);
            if (strncmp(s, HEADINGS[k].command, n) != 0 || (s[n] != '{' && s[n] != '*' && s[n] != '[')) continue;
            const char *open = strchr(s, '{');
            const char *text = open ? braces(d, open, NULL) : NULL;
            if (text) {
                d->heading = plain_math(d, text);
                outline_node(d, d->heading, HEADINGS[k].level);
            }
        }
        if (!strncmp(s, "\\begin{algorithm}", 17) || !strncmp(s, "\\begin{figure}", 14)) first_in_float = d->blocks->count, caption = NULL;
        if (strstr(s, "\\caption{")) caption = plain_math(d, braces(d, strstr(s, "\\caption{") + 8, NULL));
        if ((!strncmp(s, "\\end{algorithm}", 15) || !strncmp(s, "\\end{figure}", 12)) && first_in_float >= 0) {
            for (int k = first_in_float; caption && k < d->blocks->count; k++) d->blocks->items[k].title = caption;
            first_in_float = -1;
        }
        if (strncmp(s, "\\begin{", 7) != 0) continue;
        char name[32], language[40] = "";
        snprintf(name, sizeof name, "%.*s", (int)strcspn(s + 7, "}"), s + 7);
        const char *rest = s + 7 + strlen(name) + 1;
        bool code = !strcmp(name, "lstlisting") || !strcmp(name, "minted") || !strcmp(name, "verbatim") || !strcmp(name, "Verbatim");
        bool chart = !strcmp(name, "plantuml") || !strcmp(name, "mermaid") || !strcmp(name, "ganttchart");
        if (strcmp(name, "algorithmic") != 0 && !code && !chart) continue;
        const char *end = arena_printf(d->arena, "\\end{%s}", name);
        int first = i + 1, last = first - 1;
        for (i = first; i < d->line_count && !strstr(d->lines[i], end); i++) last = i;
        if (last < first) continue;
        if (!strcmp(name, "lstlisting")) { /* [language=...] */
            const char *lang = strstr(rest, "language=");
            if (lang) snprintf(language, sizeof language, "%.*s", (int)strcspn(lang + 9, ",] "), lang + 9);
        } else if (!strcmp(name, "minted")) {
            const char *open = strrchr(rest, '{');
            if (open) snprintf(language, sizeof language, "%.*s", (int)strcspn(open + 1, "}"), open + 1);
        }
        const Known *k = code ? known(language) : NULL;
        if (code && k) {
            add_block(d, arena_strdup(d->arena, language), k->diagram, k->dialect, first, last, NULL);
        } else if (code) {
            const char *text = masked(d, first, last, NULL);
            if (looks_like_uml(text)) add_block(d, "diagram", true, 0, first, last, NULL);
        } else if (!strcmp(name, "plantuml") || !strcmp(name, "mermaid")) {
            add_block(d, arena_strdup(d->arena, name), true, 0, first, last, NULL);
        } else {
            /* turned into something that is read: the begin and the end lines are part of it */
            int count = last - first + 3;
            char **lines = arena_alloc(d->arena, (size_t)count * sizeof(char *));
            if (!strcmp(name, "ganttchart")) {
                const char *names[128];
                int named = 0;
                lines[0] = "@startgantt";
                for (int k2 = first; k2 <= last; k2++) lines[k2 - first + 1] = (char *)gantt_line(d, d->lines[k2], names, &named);
                lines[count - 1] = "@endgantt";
                add_block(d, "pgfgantt", true, 0, first - 1, last + 1, lines);
            } else {
                bool function = false, inside = false, main_open = false;
                lines[0] = "";
                for (int k2 = first; k2 <= last; k2++)
                    lines[k2 - first + 1] = (char *)algorithmic_line(d, d->lines[k2], &function, &inside, &main_open);
                lines[count - 1] = main_open ? "FinAlgoritmo" : "";
                add_block(d, "algorithmic", false, DIALECT_PSEINT, first - 1, last + 1, lines);
            }
        }
    }
}

Blocks *blocks_read(const Source *source, Arena *arena) {
    Doc doc = {.arena = arena, .source = source, .blocks = ARENA_NEW(arena, Blocks)};
    split_lines(&doc);
    char file[1024];
    block_number(source->name, file, sizeof file);
    if (ends_with(file, ".tex") || ends_with(file, ".latex")) read_latex(&doc);
    else read_markdown(&doc);
    if (!doc.blocks->outline) { /* no headings: what it holds is its outline */
        for (int i = 0; i < doc.blocks->count; i++) {
            const DocBlock *b = &doc.blocks->items[i];
            outline_node(&doc, arena_printf(arena, "%d  %s", i + 1, b->language), 1);
        }
    }
    return doc.blocks;
}

bool is_program_path(const char *path) {
    static const char *const ENDINGS[] = {".pscope", ".plike", ".p", ".pseudo", ".psc", ".alg", ".por", ".pse"};
    for (size_t i = 0; i < sizeof ENDINGS / sizeof ENDINGS[0]; i++)
        if (ends_with(path, ENDINGS[i])) return true;
    return false;
}

FileKind file_kind(const char *path, const char *text) {
    if (is_document_path(path)) return FILE_DOCUMENT;
    if (is_uml_path(path)) return FILE_DIAGRAM;
    if (is_program_path(path)) return FILE_PROGRAM;
    if (!text) return FILE_OTHER;
    if (looks_like_uml(text)) return FILE_DIAGRAM;
    if (guess_dialect(text) != DIALECT_PLIKE) return FILE_PROGRAM;
    /* P-like: subprograms or classes, and their ends */
    char start[4096];
    size_t n = 0;
    for (; text[n] && n < sizeof start - 1; n++) start[n] = text[n] >= 'A' && text[n] <= 'Z' ? (char)(text[n] - 'A' + 'a') : text[n];
    start[n] = '\0';
    bool begins = strstr(start, "procedure ") || strstr(start, "function ") || strstr(start, "component ") || strstr(start, "class ");
    return begins && strstr(start, "end") && (strstr(start, "begin") || strstr(start, ":=")) ? FILE_PROGRAM : FILE_OTHER;
}
