#include "pseudoscope/uml.h"

#include "pseudoscope/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Source *source;
    Arena *arena;
    Diag *diag;
    Uml *uml;
    int line;
    char inside[64];
    int depth;
    int indent;     /* of the line being read */
} Reader;

#define GROW(array, count, capacity)                                                               \
    do {                                                                                           \
        if ((count) == (capacity)) {                                                               \
            int wanted_ = (capacity) ? (capacity) * 2 : 32;                                        \
            void *items_ = arena_alloc(r->arena, (size_t)wanted_ * sizeof *(array));               \
            if (count) memcpy(items_, (array), (size_t)(count) * sizeof *(array));                 \
            (array) = items_;                                                                      \
            (capacity) = wanted_;                                                                  \
        }                                                                                          \
    } while (0)

/* ---- small text tools ---- */

static const char *skip(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static bool starts(const char *s, const char *word) {
    while (*word && *s == *word) s++, word++;
    return !*word;
}

/* The word, followed by something that is not a letter. */
static bool is_word(const char *s, const char *word) {
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0) return false;
    char c = s[n];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
}

static bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           (unsigned char)c >= 0x80;
}

/* Trims, drops quotes, and turns "\n" and "<br>" into blanks. */
static const char *clean(Reader *r, const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t')) s++, n--;
    while (n && strchr(" \t\r;", s[n - 1])) n--;
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') s++, n -= 2;
    char *text = arena_strndup(r->arena, s, n);
    char *out = text;
    for (const char *c = text; *c;) {
        if (c[0] == '\\' && c[1] == 'n') {
            *out++ = ' ';
            c += 2;
        } else if (strncmp(c, "<br>", 4) == 0 || strncmp(c, "<br/>", 5) == 0) {
            *out++ = ' ';
            c += c[3] == '/' ? 5 : 4;
        } else {
            *out++ = *c++;
        }
    }
    *out = '\0';
    return text;
}

static const char *rest(Reader *r, const char *s) { return clean(r, s, strlen(s)); }

/* A name: quoted, or letters and digits. Advances *s. */
static const char *take_name(Reader *r, const char **s) {
    const char *p = skip(*s);
    if (*p == '"') {
        const char *end = strchr(p + 1, '"');
        if (!end) return NULL;
        *s = end + 1;
        return arena_strndup(r->arena, p + 1, (size_t)(end - p - 1));
    }
    if (strncmp(p, "[*]", 3) == 0) {
        *s = p + 3;
        return "[*]";
    }
    size_t n = 0;
    while (name_char(p[n])) n++;
    if (!n) return NULL;
    *s = p + n;
    return arena_strndup(r->arena, p, n);
}

static void problem(Reader *r, const char *line) {
    diag_warning(r->diag, (Loc){r->line, 1}, "this line of the diagram is not understood: %.60s", line);
}

/* ---- sequence ---- */

static int person(Reader *r, const char *id, const char *name, bool actor) {
    Uml *u = r->uml;
    for (int i = 0; i < u->people_count; i++) {
        if (strcmp(u->people[i].id, id) != 0) continue;
        if (name) u->people[i].name = name;
        return i;
    }
    if (u->people_count == 64) return 63;
    u->people[u->people_count].id = id;
    u->people[u->people_count].name = name ? name : id;
    u->people[u->people_count].actor = actor;
    return u->people_count++;
}

static void add_row(Reader *r, UmlRow row) {
    Uml *u = r->uml;
    GROW(u->rows, u->row_count, u->row_capacity);
    row.line = r->line;
    u->rows[u->row_count++] = row;
}

/* Length of an arrow at s; tells its direction and whether it is dashed. */
static size_t arrow(const char *s, bool *left, bool *dashed) {
    size_t n = 0;
    bool head = false;
    *left = *dashed = false;
    int dashes = 0;
    while (s[n] && (strchr("-<>\\/xo)(.=", s[n]) || (s[n] == '[' && dashes))) {
        if (s[n] == '[') { /* -[#red]> */
            const char *end = strchr(s + n, ']');
            if (!end) return 0;
            n = (size_t)(end - s) + 1;
            continue;
        }
        if ((s[n] == 'x' || s[n] == 'o') && !dashes && n == 0) return 0;
        if ((s[n] == 'x' || s[n] == 'o') && name_char(s[n + 1])) break;
        if (s[n] == '-' || s[n] == '.' || s[n] == '=') dashes++;
        if (s[n] == '.') *dashed = true;
        if (s[n] == '<') {
            if (!dashes) *left = true;
            head = true;
        }
        if (s[n] == '>') head = true;
        if (s[n] == ')' || s[n] == 'x') head = true;
        n++;
    }
    if (dashes > 1) *dashed = true;
    if (!dashes || !head) return 0;
    const char *end = s + n;
    while (*end == '+' || (*end == '-' && n)) end++, n++; /* Mermaid activation marks */
    return n;
}

static bool sequence_line(Reader *r, const char *s) {
    static const char *const PEOPLE[] = {"participant", "actor", "boundary", "control", "entity", "database", "collections", "queue"};
    for (size_t i = 0; i < sizeof PEOPLE / sizeof PEOPLE[0]; i++) {
        if (!is_word(s, PEOPLE[i])) continue;
        s = skip(s + strlen(PEOPLE[i]));
        const char *first = take_name(r, &s), *second = NULL;
        if (!first) return false;
        s = skip(s);
        if (is_word(s, "as")) {
            s = skip(s + 2);
            second = r->uml->mermaid ? rest(r, s) : take_name(r, &s);
        }
        if (second && r->uml->mermaid) person(r, first, second, i == 1);
        else if (second) person(r, second, first, i == 1);
        else person(r, first, NULL, i == 1);
        return true;
    }
    static const char *const GROUPS[] = {"alt", "opt", "loop", "par", "break", "critical", "group", "rect", "box"};
    for (size_t i = 0; i < sizeof GROUPS / sizeof GROUPS[0]; i++) {
        if (!is_word(s, GROUPS[i])) continue;
        if (strcmp(GROUPS[i], "rect") == 0 || strcmp(GROUPS[i], "box") == 0) return true;
        add_row(r, (UmlRow){.kind = UML_GROUP, .group = GROUPS[i], .text = rest(r, s + strlen(GROUPS[i]))});
        return true;
    }
    if (is_word(s, "else") || is_word(s, "and")) {
        add_row(r, (UmlRow){.kind = UML_ELSE, .text = rest(r, s + (s[0] == 'e' ? 4 : 3))});
        return true;
    }
    if (strcmp(s, "end") == 0) {
        add_row(r, (UmlRow){.kind = UML_END, .text = ""});
        return true;
    }
    if (is_word(s, "activate") || is_word(s, "deactivate") || is_word(s, "destroy") || is_word(s, "create")) {
        bool on = s[0] == 'a';
        const char *p = s + strcspn(s, " \t");
        const char *name = take_name(r, &p);
        if (name && s[0] != 'c')
            add_row(r, (UmlRow){.kind = on ? UML_ACTIVATE : UML_DEACTIVATE, .from = person(r, name, NULL, false), .text = ""});
        return true;
    }
    if (starts(s, "==")) {
        size_t n = strlen(s);
        while (n && (s[n - 1] == '=' || s[n - 1] == ' ')) n--;
        s += 2;
        add_row(r, (UmlRow){.kind = UML_DIVIDER, .text = clean(r, s, n > 2 ? n - 2 : 0)});
        return true;
    }
    if (is_word(s, "note") || is_word(s, "Note") || is_word(s, "hnote") || is_word(s, "rnote")) {
        const char *colon = strchr(s, ':');
        if (!colon) return true;
        const char *p = strstr(s, "over ");
        if (!p) p = strstr(s, "of ");
        int who = 0;
        if (p) {
            p += p[0] == 'o' && p[1] == 'v' ? 5 : 3;
            const char *name = take_name(r, &p);
            if (name) who = person(r, name, NULL, false);
        }
        add_row(r, (UmlRow){.kind = UML_NOTE, .from = who, .to = who, .text = rest(r, colon + 1)});
        return true;
    }
    if (is_word(s, "return")) {
        for (int i = r->uml->row_count - 1; i >= 0; i--) {
            const UmlRow *row = &r->uml->rows[i];
            if (row->kind != UML_MESSAGE || row->dashed) continue;
            add_row(r, (UmlRow){.kind = UML_MESSAGE, .from = row->to, .to = row->from, .text = rest(r, s + 6), .dashed = true});
            break;
        }
        return true;
    }
    static const char *const IGNORED[] = {"autonumber", "hide", "skinparam", "title", "footer", "header", "ref", "newpage", "autoactivate"};
    for (size_t i = 0; i < sizeof IGNORED / sizeof IGNORED[0]; i++)
        if (is_word(s, IGNORED[i])) return true;

    /* A -> B : text */
    const char *p = s;
    const char *from = *p == '[' && p[1] == '-' ? (p++, "[") : take_name(r, &p);
    if (!from) return false;
    p = skip(p);
    bool left, dashed;
    size_t n = arrow(p, &left, &dashed);
    if (!n) return false;
    p = skip(p + n);
    const char *to = *p == ']' ? (p++, "]") : take_name(r, &p);
    if (!to) return false;
    p = skip(p);
    while (*p == '+' || *p == '-' || *p == '*') p++; /* ++ and -- activations */
    p = skip(p);
    const char *text = *p == ':' ? rest(r, p + 1) : "";
    int a = person(r, from, NULL, false), b = person(r, to, NULL, false);
    add_row(r, (UmlRow){.kind = UML_MESSAGE, .from = left ? b : a, .to = left ? a : b, .text = text, .dashed = dashed});
    return true;
}

/* ---- states and other graphs ---- */

static int node(Reader *r, const char *id, const char *text, UmlShape shape) {
    Uml *u = r->uml;
    for (int i = 0; i < u->node_count; i++) {
        if (strcmp(u->nodes[i].id, id) != 0) continue;
        if (text) u->nodes[i].text = text;
        if (shape != UML_PLAIN) u->nodes[i].shape = shape;
        return i;
    }
    GROW(u->nodes, u->node_count, u->node_capacity);
    u->nodes[u->node_count] = (UmlNode){id, text ? text : id, shape, r->inside[0] ? arena_strdup(r->arena, r->inside) : NULL, r->line, -1, 0, 0};
    return u->node_count++;
}

static void edge(Reader *r, int from, int to, const char *text, bool dashed) {
    Uml *u = r->uml;
    GROW(u->edges, u->edge_count, u->edge_capacity);
    u->edges[u->edge_count++] = (UmlEdge){from, to, text ? text : "", dashed, r->line};
}

static bool state_line(Reader *r, const char *s) {
    if (is_word(s, "state")) {
        s = skip(s + 5);
        const char *first = take_name(r, &s);
        if (!first) return false;
        s = skip(s);
        if (is_word(s, "as")) {
            s = skip(s + 2);
            const char *second = take_name(r, &s);
            if (second) node(r, second, first, UML_PLAIN);
        } else if (starts(s, "<<choice>>")) {
            node(r, first, NULL, UML_DECISION);
        } else if (*s == ':') {
            node(r, first, arena_printf(r->arena, "%s: %s", first, rest(r, s + 1)), UML_PLAIN);
        } else {
            node(r, first, NULL, UML_PLAIN);
        }
        if (strchr(s, '{')) {
            snprintf(r->inside, sizeof r->inside, "%s", first);
            r->depth++;
        }
        return true;
    }
    if (strcmp(s, "}") == 0) {
        if (r->depth > 0 && --r->depth == 0) r->inside[0] = '\0';
        return true;
    }
    if (is_word(s, "note") || is_word(s, "hide") || is_word(s, "skinparam") || is_word(s, "title") || is_word(s, "direction") ||
        is_word(s, "end") || strcmp(s, "--") == 0)
        return true;
    const char *p = s, *from = take_name(r, &p);
    if (!from) return false;
    p = skip(p);
    if (*p == ':') {
        node(r, from, arena_printf(r->arena, "%s: %s", from, rest(r, p + 1)), UML_PLAIN);
        return true;
    }
    bool left, dashed;
    size_t n = arrow(p, &left, &dashed);
    if (!n) { /* -down-> and the like */
        const char *end = strchr(p, '>');
        if (*p != '-' || !end) return false;
        n = (size_t)(end - p) + 1;
        left = false;
    }
    p = skip(p + n);
    const char *to = take_name(r, &p);
    if (!to) return false;
    p = skip(p);
    const char *text = *p == ':' ? rest(r, p + 1) : "";
    if (left) {
        const char *t = from;
        from = to;
        to = t;
    }
    /* the two ends called [*] are different places */
    int a = strcmp(from, "[*]") == 0 ? node(r, "[*]", "●", UML_START) : node(r, from, NULL, UML_PLAIN);
    int b = strcmp(to, "[*]") == 0 ? node(r, "[*]end", "◉", UML_STOP) : node(r, to, NULL, UML_PLAIN);
    edge(r, a, b, text, false);
    return true;
}

/* Mermaid flowchart: A[Text] -->|label| B{Text} */
static int flow_node(Reader *r, const char **s) {
    const char *p = skip(*s);
    size_t n = 0;
    while (name_char(p[n])) n++;
    if (!n) return -1;
    const char *id = arena_strndup(r->arena, p, n);
    p += n;
    UmlShape shape = UML_PLAIN;
    const char *text = NULL;
    static const struct {
        const char *open, *close;
        UmlShape shape;
    } SHAPES[] = {{"([", "])", UML_START}, {"[(", ")]", UML_DATA}, {"((", "))", UML_START}, {"[[", "]]", UML_PLAIN},
                  {"{{", "}}", UML_PLAIN}, {"{", "}", UML_DECISION}, {"[", "]", UML_PLAIN}, {"(", ")", UML_PLAIN}, {">", "]", UML_PLAIN}};
    for (size_t i = 0; i < sizeof SHAPES / sizeof SHAPES[0]; i++) {
        if (!starts(p, SHAPES[i].open)) continue;
        const char *start = p + strlen(SHAPES[i].open), *end = strstr(start, SHAPES[i].close);
        if (!end) return -1;
        text = clean(r, start, (size_t)(end - start));
        shape = SHAPES[i].shape;
        p = end + strlen(SHAPES[i].close);
        break;
    }
    *s = p;
    return node(r, id, text, shape);
}

static bool flow_line(Reader *r, const char *s) {
    if (is_word(s, "subgraph")) {
        snprintf(r->inside, sizeof r->inside, "%s", rest(r, s + 8));
        return true;
    }
    if (strcmp(s, "end") == 0) {
        r->inside[0] = '\0';
        return true;
    }
    if (is_word(s, "classDef") || is_word(s, "class") || is_word(s, "style") || is_word(s, "click") || is_word(s, "linkStyle") ||
        is_word(s, "direction"))
        return true;
    const char *p = s;
    int from = flow_node(r, &p);
    if (from < 0) return false;
    for (;;) {
        p = skip(p);
        if (!*p || *p == ';') return true;
        if (*p != '-' && *p != '=' && *p != '<' && *p != '.') return false;
        bool dashed = false;
        const char *text = NULL;
        size_t n = 0;
        while (p[n] && strchr("-=.<>xo", p[n])) {
            if (p[n] == '.') dashed = true;
            if ((p[n] == 'x' || p[n] == 'o') && name_char(p[n + 1])) break;
            n++;
        }
        bool headed = n > 0 && p[n - 1] == '>';
        p = skip(p + n);
        if (*p == '|') {
            const char *end = strchr(p + 1, '|');
            if (!end) return false;
            text = clean(r, p + 1, (size_t)(end - p - 1));
            p = end + 1;
        } else if (!headed && strstr(p, "--")) { /* -- text --> */
            const char *end = strstr(p, "--");
            text = clean(r, p, (size_t)(end - p));
            p = end;
            while (*p == '-' || *p == '>') p++;
        }
        int to = flow_node(r, &p);
        if (to < 0) return false;
        edge(r, from, to, text, dashed);
        from = to;
    }
}

/* ---- activity (PlantUML) ---- */

typedef struct {
    char kind; /* i: if, w: while, r: repeat */
    int top, exits[32], exit_count;
    const char *label; /* for the next edge out of `top` */
} Block;

typedef struct {
    int last[32], last_count; /* where the flow is now */
    const char *label;
    Block blocks[32];
    int depth;
    int serial;
} Activity;

static Activity act;

static void flow_to(Reader *r, int to) {
    for (int i = 0; i < act.last_count; i++) edge(r, act.last[i], to, i == 0 ? act.label : "", false);
    act.label = NULL;
    act.last[0] = to;
    act.last_count = 1;
}

static int act_node(Reader *r, const char *text, UmlShape shape) {
    return node(r, arena_printf(r->arena, "%%%d", act.serial++), text, shape);
}

static const char *between(Reader *r, const char *s, char open, char close) {
    const char *a = strchr(s, open), *b = a ? strrchr(s, close) : NULL;
    if (a && open == '(') { /* the matching one */
        int depth = 0;
        for (b = a; *b; b++) {
            if (*b == '(') depth++;
            if (*b == ')' && --depth == 0) break;
        }
        if (!*b) b = NULL;
    }
    return a && b && b > a ? clean(r, a + 1, (size_t)(b - a - 1)) : "";
}

static bool activity_line(Reader *r, const char *s) {
    if (is_word(s, "start")) {
        flow_to(r, act_node(r, "●", UML_START));
        return true;
    }
    if (is_word(s, "stop") || is_word(s, "end") || is_word(s, "kill") || is_word(s, "detach")) {
        flow_to(r, act_node(r, "◉", UML_STOP));
        act.last_count = 0;
        return true;
    }
    if (*s == ':') {
        size_t n = strlen(s);
        while (n && strchr(";|<>/]}", s[n - 1])) n--;
        flow_to(r, act_node(r, clean(r, s + 1, n - 1), UML_PLAIN));
        return true;
    }
    if (is_word(s, "if") || is_word(s, "elseif")) {
        bool more = s[0] == 'e';
        Block *b = act.depth ? &act.blocks[act.depth - 1] : NULL;
        if (more) {
            if (!b || b->kind != 'i') return false;
            for (int i = 0; i < act.last_count && b->exit_count < 32; i++) b->exits[b->exit_count++] = act.last[i];
            act.last[0] = b->top;
            act.last_count = 1;
            const char *label = strstr(s, ") is") ? NULL : NULL;
            act.label = label ? label : "no";
        }
        int d = act_node(r, between(r, s, '(', ')'), UML_DECISION);
        flow_to(r, d);
        const char *then = strstr(s, "then");
        act.label = then ? between(r, then, '(', ')') : "";
        if (!act.label[0]) act.label = "yes";
        if (more) {
            b->top = d;
        } else if (act.depth < 32) {
            act.blocks[act.depth++] = (Block){.kind = 'i', .top = d, .label = "no"};
        }
        return true;
    }
    if (is_word(s, "else")) {
        Block *b = act.depth ? &act.blocks[act.depth - 1] : NULL;
        if (!b || b->kind != 'i') return false;
        for (int i = 0; i < act.last_count && b->exit_count < 32; i++) b->exits[b->exit_count++] = act.last[i];
        act.last[0] = b->top;
        act.last_count = 1;
        b->kind = 'e';
        act.label = between(r, s, '(', ')');
        if (!act.label[0]) act.label = "no";
        return true;
    }
    if (is_word(s, "endif")) {
        Block *b = act.depth ? &act.blocks[--act.depth] : NULL;
        if (!b) return false;
        if (b->kind == 'i' && act.last_count < 32) act.last[act.last_count++] = b->top; /* no else */
        for (int i = 0; i < b->exit_count && act.last_count < 32; i++) act.last[act.last_count++] = b->exits[i];
        return true;
    }
    if (is_word(s, "while")) {
        int d = act_node(r, between(r, s, '(', ')'), UML_DECISION);
        flow_to(r, d);
        const char *is = strstr(s, ") is");
        act.label = is ? between(r, is + 1, '(', ')') : "yes";
        if (act.depth < 32) act.blocks[act.depth++] = (Block){.kind = 'w', .top = d};
        return true;
    }
    if (is_word(s, "endwhile")) {
        Block *b = act.depth ? &act.blocks[--act.depth] : NULL;
        if (!b) return false;
        flow_to(r, b->top);
        act.label = between(r, s, '(', ')');
        if (!act.label[0]) act.label = "no";
        return true;
    }
    if (is_word(s, "repeat")) {
        if (strstr(s, "while")) {
            Block *b = act.depth ? &act.blocks[--act.depth] : NULL;
            if (!b) return false;
            int d = act_node(r, between(r, s, '(', ')'), UML_DECISION);
            flow_to(r, d);
            edge(r, d, b->top, "yes", false);
            act.label = "no";
        } else {
            int d = act_node(r, "◇", UML_PLAIN);
            flow_to(r, d);
            if (act.depth < 32) act.blocks[act.depth++] = (Block){.kind = 'r', .top = d};
        }
        return true;
    }
    if (*s == '|' || is_word(s, "note") || is_word(s, "partition") || *s == '}' || is_word(s, "title") || is_word(s, "skinparam") ||
        is_word(s, "floating"))
        return true;
    if (starts(s, "->")) {
        act.label = rest(r, s + 2);
        return true;
    }
    return false;
}

/* ---- use cases and components (PlantUML) ---- */

static int shaped_node(Reader *r, const char **s) {
    const char *p = skip(*s);
    static const struct {
        char open, close;
        UmlShape shape;
    } SHAPES[] = {{':', ':', UML_ACTOR}, {'(', ')', UML_USECASE}, {'[', ']', UML_COMPONENT}};
    for (int i = 0; i < 3; i++) {
        if (*p != SHAPES[i].open) continue;
        const char *end = strchr(p + 1, SHAPES[i].close);
        if (!end || end == p + 1) return -1;
        const char *name = clean(r, p + 1, (size_t)(end - p - 1));
        *s = end + 1;
        return node(r, name, NULL, SHAPES[i].shape);
    }
    const char *name = take_name(r, &p);
    if (!name) return -1;
    *s = p;
    return node(r, name, NULL, UML_PLAIN);
}

static bool graph_line(Reader *r, const char *s) {
    static const struct {
        const char *word;
        UmlShape shape;
    } KINDS[] = {{"actor", UML_ACTOR}, {"usecase", UML_USECASE}, {"component", UML_COMPONENT}, {"database", UML_DATA},
                 {"node", UML_COMPONENT}, {"artifact", UML_COMPONENT}, {"queue", UML_DATA}, {"interface", UML_COMPONENT},
                 {"cloud", UML_COMPONENT}, {"object", UML_PLAIN}};
    for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++) {
        if (!is_word(s, KINDS[i].word)) continue;
        const char *p = skip(s + strlen(KINDS[i].word));
        int first = shaped_node(r, &p);
        if (first < 0) return false;
        p = skip(p);
        if (is_word(p, "as")) {
            p = skip(p + 2);
            const char *alias = take_name(r, &p);
            if (alias) r->uml->nodes[first].id = alias;
        }
        r->uml->nodes[first].shape = KINDS[i].shape;
        if (strchr(p, '{')) {
            snprintf(r->inside, sizeof r->inside, "%s", r->uml->nodes[first].text);
            r->depth++;
        }
        return true;
    }
    if (is_word(s, "rectangle") || is_word(s, "package") || is_word(s, "frame") || is_word(s, "folder")) {
        const char *p = s + strcspn(s, " \t");
        const char *name = take_name(r, &p);
        snprintf(r->inside, sizeof r->inside, "%s", name ? name : "");
        r->depth++;
        return true;
    }
    if (strcmp(s, "}") == 0) {
        if (r->depth > 0 && --r->depth == 0) r->inside[0] = '\0';
        return true;
    }
    if (is_word(s, "title") || is_word(s, "skinparam") || is_word(s, "note") || starts(s, "left to right") || is_word(s, "hide") ||
        is_word(s, "end") || starts(s, "top to bottom"))
        return true;
    const char *p = s;
    int from = shaped_node(r, &p);
    if (from < 0) return false;
    p = skip(p);
    if (is_word(p, "as")) {
        p = skip(p + 2);
        const char *alias = take_name(r, &p);
        if (alias) r->uml->nodes[from].id = alias;
        return true;
    }
    if (!*p) return true;
    bool dashed = false, left = *p == '<';
    size_t n = 0;
    while (p[n] && (strchr("-.<>|*o=#", p[n]) || (n && p[n] >= 'a' && p[n] <= 'z' && strchr(p + n, '-') && strchr(p + n, '-') < p + n + 6))) {
        if (p[n] == '.') dashed = true;
        n++;
    }
    if (n < 2) return false;
    p = skip(p + n);
    int to = shaped_node(r, &p);
    if (to < 0) return false;
    p = skip(p);
    const char *text = *p == ':' ? rest(r, p + 1) : "";
    edge(r, left ? to : from, left ? from : to, text, dashed);
    return true;
}

/* ---- classes ---- */

static int class_of_name(Reader *r, const char *name, const char *kind) {
    Uml *u = r->uml;
    for (int i = 0; i < u->class_count; i++) {
        if (strcmp(u->classes[i].name, name) != 0) continue;
        if (kind) u->classes[i].kind = kind;
        return i;
    }
    GROW(u->classes, u->class_count, u->class_capacity);
    UmlClass *c = &u->classes[u->class_count];
    memset(c, 0, sizeof *c);
    c->name = name;
    c->kind = kind ? kind : "class";
    c->line = r->line;
    return u->class_count++;
}

static int open_class = -1;

static void member(Reader *r, int klass, const char *text) {
    UmlClass *c = &r->uml->classes[klass];
    if (c->member_count < 64 && text[0] && strcmp(text, "--") != 0 && strcmp(text, "..") != 0 && strcmp(text, "==") != 0)
        c->members[c->member_count++] = text;
}

static bool class_line(Reader *r, const char *s) {
    if (open_class >= 0) {
        if (*s == '}') {
            open_class = -1;
            return true;
        }
        if (starts(s, "<<")) {
            if (strstr(s, "interface")) r->uml->classes[open_class].kind = "interface";
            else if (strstr(s, "abstract")) r->uml->classes[open_class].kind = "abstract";
            else if (strstr(s, "enum")) r->uml->classes[open_class].kind = "enum";
            return true;
        }
        member(r, open_class, rest(r, s));
        return true;
    }
    static const char *const KINDS[] = {"abstract class", "abstract", "class", "interface", "enum", "entity", "annotation"};
    for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++) {
        if (!is_word(s, KINDS[i])) continue;
        const char *p = skip(s + strlen(KINDS[i]));
        const char *name = take_name(r, &p);
        if (!name) return false;
        const char *kind = strcmp(KINDS[i], "abstract class") == 0 ? "abstract" : KINDS[i];
        int c = class_of_name(r, name, kind);
        const char *parent = strstr(p, "extends ");
        if (!parent) parent = strstr(p, "implements ");
        if (parent) {
            bool implements = parent[0] == 'i';
            parent += implements ? 11 : 8;
            const char *other = take_name(r, &parent);
            if (other) {
                Uml *u = r->uml;
                int to = class_of_name(r, other, NULL);
                GROW(u->relations, u->relation_count, u->relation_capacity);
                u->relations[u->relation_count++] = (UmlRelation){c, to, implements ? "implements" : "extends", "", NULL, NULL};
            }
        }
        const char *brace = strchr(p, '{');
        if (brace && !strchr(brace, '}')) open_class = c;
        return true;
    }
    if (is_word(s, "title") || is_word(s, "skinparam") || is_word(s, "hide") || is_word(s, "note") || is_word(s, "package") ||
        is_word(s, "namespace") || is_word(s, "direction") || is_word(s, "end") || *s == '}')
        return true;
    if (starts(s, "<<")) return true;
    const char *p = s, *from = take_name(r, &p);
    if (!from) return false;
    p = skip(p);
    if (*p == ':') {
        member(r, class_of_name(r, from, NULL), rest(r, p + 1));
        return true;
    }
    while (*p == '"') { /* multiplicity */
        const char *end = strchr(p + 1, '"');
        if (!end) return false;
        p = skip(end + 1);
    }
    size_t n = 0;
    while (p[n] && strchr("<>|-.*o+#^()", p[n])) {
        if (p[n] == 'o' && n > 0 && !strchr("-.", p[n - 1])) break;
        n++;
    }
    if (n < 2) return false;
    char mark[32];
    snprintf(mark, sizeof mark, "%.*s", (int)n, p);
    p = skip(p + n);
    while (*p == '"') {
        const char *end = strchr(p + 1, '"');
        if (!end) return false;
        p = skip(end + 1);
    }
    const char *to = take_name(r, &p);
    if (!to) return false;
    p = skip(p);
    const char *text = *p == ':' ? rest(r, p + 1) : "";
    bool dotted = strchr(mark, '.') != NULL, triangle = strstr(mark, "|>") || strstr(mark, "<|");
    const char *kind = triangle ? (dotted ? "implements" : "extends") : strchr(mark, '*') ? "owns" : strchr(mark, 'o') ? "has"
                     : dotted ? "uses" : "knows";
    /* the special end tells the direction: A <|-- B, A *-- B, A <-- B */
    bool reversed = triangle ? strstr(mark, "<|") != NULL : strchr(mark, '<') != NULL && !strchr(mark, '>');
    if (!triangle && (mark[n - 1] == '*' || mark[n - 1] == 'o')) reversed = true;
    int a = class_of_name(r, from, NULL), b = class_of_name(r, to, NULL);
    Uml *u = r->uml;
    GROW(u->relations, u->relation_count, u->relation_capacity);
    u->relations[u->relation_count++] = (UmlRelation){reversed ? b : a, reversed ? a : b, kind, text, NULL, NULL};
    return true;
}

/* ---- whole files ---- */

static bool ends(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    return a >= b && equals_ignore_case(path + a - b, suffix);
}

bool is_uml_path(const char *path) {
    return ends(path, ".puml") || ends(path, ".plantuml") || ends(path, ".iuml") || ends(path, ".pu") || ends(path, ".mmd") ||
           ends(path, ".mermaid") || ends(path, ".dot") || ends(path, ".gv");
}

typedef bool (*LineReader)(Reader *, const char *);

/* What kind of PlantUML this is, from the first lines that tell. */

#include "charts.inc"

static LineReader guess(const char *text) {
    for (const char *s = text; *s;) {
        size_t n = strcspn(s, "\n");
        char line[300];
        snprintf(line, sizeof line, "%.*s", (int)(n < 299 ? n : 299), s);
        const char *l = skip(line);
        if (is_word(l, "class") || is_word(l, "interface") || is_word(l, "abstract") || is_word(l, "enum") || strstr(l, "<|--") ||
            strstr(l, "--|>") || strstr(l, "<|..") || strstr(l, "..|>") || strstr(l, "*--") || strstr(l, "o--"))
            return class_line;
        if (strstr(l, "[*]") || is_word(l, "state")) return state_line;
        if (strcmp(l, "start") == 0 || (l[0] == ':' && strchr(l, ';')) || starts(l, "if (")) return activity_line;
        if (is_word(l, "usecase") || is_word(l, "component") || l[0] == '[' || (l[0] == ':' && !strchr(l, ';')) || l[0] == '(' ||
            is_word(l, "rectangle") || is_word(l, "node") || strstr(l, ") ") == l + 1 || strstr(l, "--> (") || strstr(l, "-- ("))
            return graph_line;
        if (is_word(l, "participant") || strstr(l, "->") || strstr(l, "<-")) {
            if (strstr(l, "-> (") || strstr(l, "--> [") || strstr(l, "-> [")) return graph_line;
            return sequence_line;
        }
        s += n;
        if (*s) s++;
    }
    return sequence_line;
}

bool looks_like_uml(const char *text) {
    static const char *const WORDS[] = {"sequenceDiagram", "classDiagram", "stateDiagram", "flowchart", "erDiagram", "gantt", "mindmap",
                                        "timeline", "journey", "pie", "digraph"};
    char line[300];
    const char *l = first_line(text, line, sizeof line);
    if (starts(l, "@start")) return true;
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
        if (is_word(l, WORDS[i]) || starts(l, "stateDiagram")) return true;
    return (is_word(l, "graph") && (strchr(l, '{') || strstr(l, " TD") || strstr(l, " LR") || strstr(l, " TB") || strstr(l, " BT") || strstr(l, " RL")));
}

Uml *uml_parse(const Source *source, Arena *arena, Diag *diag) {
    Uml *uml = ARENA_NEW(arena, Uml);
    Reader reader = {.source = source, .arena = arena, .diag = diag, .uml = uml};
    Reader *r = &reader;
    LineReader read_line = NULL;
    bool failed = false;
    if (charts(r, source->text, source->name, &failed)) return failed ? NULL : uml;
    r->line = 0;
    act = (Activity){0};
    open_class = -1;
    uml->mermaid = !strstr(source->text, "@start");
    if (!uml->mermaid) read_line = guess(source->text);

    bool comment = false, long_note = false;
    for (const char *s = source->text; *s;) {
        size_t n = strcspn(s, "\n");
        r->line++;
        char *line = arena_strndup(arena, s, n);
        s += n;
        if (*s) s++;
        while (n && strchr(" \t\r", line[n - 1])) line[--n] = '\0';
        const char *l = skip(line);
        if (starts(l, "/'")) comment = true;
        if (comment) {
            if (strstr(l, "'/")) comment = false;
            continue;
        }
        if (long_note) { /* note ... end note */
            if (starts(l, "end note") || starts(l, "endnote")) long_note = false;
            continue;
        }
        if (!*l || *l == '\'' || starts(l, "%%") || *l == '@' || *l == '!' || starts(l, "---")) continue;
        if ((is_word(l, "note") || is_word(l, "Note")) && !strchr(l, ':')) {
            long_note = true;
            continue;
        }
        if (is_word(l, "title")) uml->title = rest(r, l + 5);
        if (!read_line) { /* Mermaid says what it is */
            if (is_word(l, "sequenceDiagram")) read_line = sequence_line;
            else if (is_word(l, "classDiagram")) read_line = class_line;
            else if (starts(l, "stateDiagram")) read_line = state_line;
            else if (is_word(l, "flowchart") || is_word(l, "graph")) read_line = flow_line;
            else {
                diag_error(diag, (Loc){r->line, 1}, "this kind of diagram is not known: %.40s", l);
                return NULL;
            }
            continue;
        }
        if (!read_line(r, l)) problem(r, l);
    }
    uml->kind = read_line == sequence_line ? UML_SEQUENCE : read_line == class_line ? UML_CLASS
              : read_line == state_line ? UML_STATE : UML_GRAPH;
    bool empty = uml->row_count == 0 && uml->node_count == 0 && uml->class_count == 0;
    if (empty || !read_line) {
        diag_error(diag, (Loc){1, 1}, "there is no diagram in this file");
        return NULL;
    }
    return uml;
}

void uml_dump(const Uml *u, StrBuf *out) {
    static const char *const KINDS[] = {"?", "sequence", "state", "class", "graph", "plan", "entities", "tree", "pie"};
    strbuf_printf(out, "%s%s%s\n", u->kind == UML_TREE && u->tree_kind ? u->tree_kind : KINDS[u->kind], u->title ? " " : "",
                  u->title ? u->title : "");
    if (u->kind == UML_GANTT) {
        if (u->first_day >= 0) {
            int y, m, d;
            uml_date(u->first_day, &y, &m, &d);
            strbuf_printf(out, "  from %04d-%02d-%02d\n", y, m, d);
        }
        for (int i = 0; i < u->task_count; i++) {
            const UmlTask *t = &u->tasks[i];
            strbuf_printf(out, "  day %ld, %d day%s: %s%s%s%s%s\n", t->start + 1, t->days, t->days == 1 ? "" : "s", t->name,
                          t->section ? " [" : "", t->section ? t->section : "", t->section ? "]" : "",
                          t->milestone ? " (milestone)" : t->done ? " (done)" : t->active ? " (active)" : t->critical ? " (critical)" : "");
        }
        return;
    }
    if (u->kind == UML_TREE || u->kind == UML_PIE) {
        for (int i = 0; i < u->node_count; i++) {
            strbuf_printf(out, "  %*s%s", u->nodes[i].depth * 2, "", u->nodes[i].text);
            if (u->kind == UML_PIE) strbuf_printf(out, ": %g", u->nodes[i].value);
            strbuf_puts(out, "\n");
        }
        return;
    }
    if (u->kind == UML_ER) {
        for (int i = 0; i < u->class_count; i++) {
            strbuf_printf(out, "  entity %s\n", u->classes[i].name);
            for (int k = 0; k < u->classes[i].member_count; k++) strbuf_printf(out, "    %s\n", u->classes[i].members[k]);
        }
        for (int i = 0; i < u->relation_count; i++) {
            const UmlRelation *rel = &u->relations[i];
            strbuf_printf(out, "  %s %s -- %s %s%s%s\n", rel->from_many, u->classes[rel->from].name, rel->to_many, u->classes[rel->to].name,
                          rel->text[0] ? ": " : "", rel->text);
        }
        return;
    }
    for (int i = 0; i < u->people_count; i++) strbuf_printf(out, "  person %s\n", u->people[i].name);
    for (int i = 0; i < u->row_count; i++) {
        const UmlRow *row = &u->rows[i];
        switch (row->kind) {
            case UML_MESSAGE:
                strbuf_printf(out, "  %s %s %s: %s\n", u->people[row->from].name, row->dashed ? "-->" : "->", u->people[row->to].name, row->text);
                break;
            case UML_GROUP: strbuf_printf(out, "  %s %s\n", row->group, row->text); break;
            case UML_ELSE: strbuf_printf(out, "  else %s\n", row->text); break;
            case UML_END: strbuf_puts(out, "  end\n"); break;
            case UML_NOTE: strbuf_printf(out, "  note %s\n", row->text); break;
            case UML_DIVIDER: strbuf_printf(out, "  == %s\n", row->text); break;
            default: break;
        }
    }
    for (int i = 0; i < u->node_count; i++)
        strbuf_printf(out, "  node %s%s%s\n", u->nodes[i].text, u->nodes[i].inside ? " in " : "", u->nodes[i].inside ? u->nodes[i].inside : "");
    for (int i = 0; i < u->edge_count; i++)
        strbuf_printf(out, "  %s -> %s%s%s\n", u->nodes[u->edges[i].from].text, u->nodes[u->edges[i].to].text,
                      u->edges[i].text[0] ? ": " : "", u->edges[i].text);
    for (int i = 0; i < u->class_count; i++) {
        strbuf_printf(out, "  %s %s\n", u->classes[i].kind, u->classes[i].name);
        for (int k = 0; k < u->classes[i].member_count; k++) strbuf_printf(out, "    %s\n", u->classes[i].members[k]);
    }
    for (int i = 0; i < u->relation_count; i++)
        strbuf_printf(out, "  %s %s %s%s%s\n", u->classes[u->relations[i].from].name, u->relations[i].kind,
                      u->classes[u->relations[i].to].name, u->relations[i].text[0] ? ": " : "", u->relations[i].text);
}
