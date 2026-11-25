/* Plans, entities, trees, pies: the diagrams that are not UML. */

typedef struct {
    Reader *r;
    const char *section;
    char format[24];
    int stack[40], indents[40], depth; /* trees: the last node at each depth */
    int open_entity;
    bool dates;
} Charts;

static Charts ch;

/* ---- dates ---- */

static long days_from_civil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void uml_date(long day, int *year, int *month, int *day_of_month) {
    long z = day + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *day_of_month = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (int)(y + (*month <= 2));
}

/* A date written as the format says (YYYY-MM-DD when it says nothing). */
static bool parse_date(const char *format, const char *s, long *day) {
    int y = 0, m = 0, d = 0;
    if (!format[0]) format = "YYYY-MM-DD";
    s = skip(s);
    for (const char *f = format; *f;) {
        if (*f == 'Y' || *f == 'M' || *f == 'D') {
            char kind = *f;
            int width = 0, value = 0, digits = 0;
            while (*f == kind) f++, width++;
            while (*s >= '0' && *s <= '9' && digits < (width > 2 ? 4 : 2)) value = value * 10 + (*s++ - '0'), digits++;
            if (!digits) return false;
            if (kind == 'Y') y = width == 2 ? 2000 + value : value;
            else if (kind == 'M') m = value;
            else d = value;
        } else {
            if (*s != *f) return false;
            s++, f++;
        }
    }
    if (m < 1 || m > 12 || d < 1 || d > 31 || *skip(s)) return false;
    *day = days_from_civil(y, m, d);
    return true;
}

/* "30d", "2w", "3 days", "1 week": days. */
static bool parse_days(const char *s, int *days) {
    s = skip(s);
    int n = 0, digits = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0'), digits++;
    if (!digits) return false;
    s = skip(s);
    if (*s == 'w' || *s == 'W') n *= 7;
    else if (*s == 'h' || *s == 'H') n = (n + 23) / 24;
    else if (*s == 'M' || starts(s, "month")) n *= 30;
    else if (*s != 'd' && *s != 'D') return false;
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) s++;
    if (*skip(s)) return false;
    *days = n;
    return true;
}

/* ---- plans ---- */

static int task_named(Reader *r, const char *name) {
    Uml *u = r->uml;
    for (int i = 0; i < u->task_count; i++)
        if ((u->tasks[i].id && strcmp(u->tasks[i].id, name) == 0) || strcmp(u->tasks[i].name, name) == 0) return i;
    return -1;
}

static int add_task(Reader *r, const char *name) {
    Uml *u = r->uml;
    GROW(u->tasks, u->task_count, u->task_capacity);
    u->tasks[u->task_count] = (UmlTask){.name = name, .section = ch.section, .start = -1, .end = -1, .days = 1, .after = -1,
                                        .previous = u->task_count - 1, .percent = -1, .line = r->line};
    return u->task_count++;
}

/* Mermaid: "A task :done, a1, 2024-01-01, 30d" */
static bool gantt_line(Reader *r, const char *s) {
    static const char *const SKIPPED[] = {"excludes", "includes", "axisFormat", "tickInterval", "todayMarker", "weekday", "title"};
    for (size_t i = 0; i < sizeof SKIPPED / sizeof SKIPPED[0]; i++)
        if (is_word(s, SKIPPED[i])) return true;
    if (is_word(s, "dateFormat")) {
        snprintf(ch.format, sizeof ch.format, "%s", skip(s + 10));
        return true;
    }
    if (is_word(s, "section")) {
        ch.section = rest(r, s + 7);
        return true;
    }
    const char *colon = strchr(s, ':');
    if (!colon) return false;
    int index = add_task(r, clean(r, s, (size_t)(colon - s)));
    UmlTask *t = &r->uml->tasks[index];
    bool timed = false;
    t->follows = true;
    for (const char *f = colon + 1; *f;) {
        size_t n = strcspn(f, ",");
        const char *field = clean(r, f, n);
        long day;
        int days;
        if (strcmp(field, "done") == 0) t->done = true;
        else if (strcmp(field, "active") == 0) t->active = true;
        else if (strcmp(field, "crit") == 0) t->critical = true;
        else if (strcmp(field, "milestone") == 0) t->milestone = true;
        else if (is_word(field, "after")) {
            const char *id = skip(field + 5);
            char first[64];
            snprintf(first, sizeof first, "%.*s", (int)strcspn(id, " "), id);
            t->after = task_named(r, first);
            timed = true;
        } else if (parse_date(ch.format, field, &day)) {
            if (!timed) t->start = day, t->follows = false;
            else t->end = day;
            timed = true;
            ch.dates = true;
        } else if (parse_days(field, &days)) {
            t->days = days;
            timed = true;
        } else if (*field && !t->id) {
            t->id = field;
        }
        f += n;
        if (*f) f++;
    }
    if (t->milestone) t->days = 0;
    return true;
}

/* PlantUML: "[Design] lasts 10 days", "[Test] starts at [Design]'s end" */
static bool plan_line(Reader *r, const char *s) {
    Uml *u = r->uml;
    long day;
    if (starts(s, "--")) { /* -- a phase -- */
        const char *end = strstr(s + 2, "--");
        ch.section = clean(r, s + 2, end ? (size_t)(end - s - 2) : strlen(s + 2));
        return true;
    }
    if (starts(s, "Project starts") || starts(s, "project starts")) {
        const char *date = s + 14;
        while (*date && !(*date >= '0' && *date <= '9')) date++;
        if (parse_date("YYYY-MM-DD", date, &day) || parse_date("YYYY/MM/DD", date, &day)) u->first_day = day, ch.dates = true;
        return true;
    }
    bool then = is_word(s, "then");
    if (then) s = skip(s + 4);
    if (*s != '[') return !strchr(s, '['); /* scales, colours, closed days */
    const char *close = strchr(s, ']');
    if (!close) return false;
    const char *name = clean(r, s + 1, (size_t)(close - s - 1));
    int index = task_named(r, name);
    if (index < 0) index = add_task(r, name);
    UmlTask *t = &u->tasks[index];
    if (then) t->follows = true;
    s = skip(close + 1);
    if (starts(s, "as [")) {
        const char *end = strchr(s, ']');
        if (end) {
            t->id = clean(r, s + 4, (size_t)(end - s - 4));
            s = skip(end + 1);
        }
    }
    if (starts(s, "->")) { /* [A] -> [B]: B waits for A */
        const char *open = strchr(s, '['), *end = open ? strchr(open, ']') : NULL;
        if (!end) return false;
        const char *other = clean(r, open + 1, (size_t)(end - open - 1));
        int to = task_named(r, other);
        if (to < 0) to = add_task(r, other);
        r->uml->tasks[to].after = index;
        return true;
    }
    for (int guard = 0; *s && guard < 8; guard++) {
        const char *and = strstr(s, " and ");
        size_t n = and ? (size_t)(and - s) : strlen(s);
        char part[200];
        snprintf(part, sizeof part, "%.*s", (int)(n < 199 ? n : 199), s);
        const char *p = part;
        int days;
        if (is_word(p, "lasts") || is_word(p, "requires")) {
            if (parse_days(skip(p + (p[0] == 'l' ? 5 : 8)), &days)) t->days = days;
        } else if (is_word(p, "starts") || is_word(p, "happens")) {
            const char *open = strchr(p, '[');
            const char *digits = p;
            while (*digits && !(*digits >= '0' && *digits <= '9')) digits++;
            if (p[0] == 'h') t->milestone = true, t->days = 0;
            if (open) {
                const char *end = strchr(open, ']');
                if (end) t->after = task_named(r, clean(r, open + 1, (size_t)(end - open - 1)));
                if (strstr(p, "'s start") && t->after >= 0) t->start = -2 - t->after, t->after = -1; /* together with it */
            } else if (strstr(p, "D+")) {
                t->start = (u->first_day > 0 ? u->first_day : 0) + atoi(strstr(p, "D+") + 2);
            } else if (parse_date("YYYY-MM-DD", digits, &day) || parse_date("YYYY/MM/DD", digits, &day)) {
                t->start = day;
                ch.dates = true;
            }
        } else if (is_word(p, "ends")) {
            const char *digits = p;
            while (*digits && !(*digits >= '0' && *digits <= '9')) digits++;
            if (parse_date("YYYY-MM-DD", digits, &day) || parse_date("YYYY/MM/DD", digits, &day)) t->end = day + 1, ch.dates = true;
        } else if (is_word(p, "is")) {
            const char *percent = strchr(p, '%');
            if (percent) t->percent = atoi(skip(p + 2));
            if (strstr(p, "completed") && !percent) t->percent = 100;
            t->done = t->percent >= 100;
            t->active = t->percent > 0 && t->percent < 100;
        }
        s += n;
        if (*s) s = skip(s + 5);
    }
    return true;
}

/* Everybody gets a first and a last day. */
static void resolve_plan(Uml *u) {
    long base = u->first_day > 0 ? u->first_day : 0;
    for (int pass = 0; pass <= u->task_count + 1; pass++) {
        for (int i = 0; i < u->task_count; i++) {
            UmlTask *t = &u->tasks[i];
            long start = t->start;
            if (t->after >= 0 && t->after != i) start = u->tasks[t->after].end; /* -1 until that one is known */
            else if (t->start <= -2) start = u->tasks[-2 - t->start].start >= 0 ? u->tasks[-2 - t->start].start : -1;
            else if (t->start < 0 && t->follows && t->previous >= 0) start = u->tasks[t->previous].end;
            else if (t->start < 0) start = base;
            if (start < 0) continue;
            if (t->start > -2 || start >= 0) {
                if (t->start <= -2) t->start = start;
                else if (t->start < 0 || t->after >= 0) t->start = start;
            }
            if (t->end >= 0 && t->end > t->start && pass == 0 && t->days == 1 && !t->milestone) t->days = (int)(t->end - t->start);
            t->end = t->start + t->days;
        }
    }
    long first = -1;
    for (int i = 0; i < u->task_count; i++) {
        UmlTask *t = &u->tasks[i];
        if (t->start < 0) t->start = base; /* waiting in a circle: from the beginning */
        t->end = t->start + t->days;
        if (first < 0 || t->start < first) first = t->start;
    }
    for (int i = 0; i < u->task_count; i++) u->tasks[i].start -= first, u->tasks[i].end -= first;
    u->first_day = ch.dates ? first : -1;
}

/* ---- entities ---- */

static const char *many_words(const char *mark, bool left) {
    bool zero = strchr(mark, 'o') != NULL;
    bool many = strchr(mark, left ? '}' : '{') != NULL;
    return many ? (zero ? "zero or more" : "one or more") : zero ? "zero or one" : "one";
}

static bool entity_name_char(char c) { return name_char(c) || c == '-'; }

static bool er_line(Reader *r, const char *s) {
    Uml *u = r->uml;
    if (ch.open_entity >= 0) { /* its attributes */
        if (*s == '}') {
            ch.open_entity = -1;
            return true;
        }
        const char *text = rest(r, s);
        if (*text == '*' || *text == '+' || *text == '-' || *text == '#') { /* PlantUML: a mandatory attribute */
            if (strcmp(text, "--") == 0) return true;
            text = arena_printf(r->arena, "%s%s", skip(text + 1), *text == '*' ? " (required)" : "");
        }
        member(r, ch.open_entity, text);
        return true;
    }
    if (is_word(s, "entity") || is_word(s, "table")) s = skip(s + (s[0] == 'e' ? 6 : 5));
    const char *p = s, *start = skip(p);
    const char *first = NULL, *alias = NULL;
    if (*start == '"') {
        first = take_name(r, &p);
    } else {
        size_t n = 0;
        while (entity_name_char(start[n]) && !(start[n] == '-' && (start[n + 1] == '-' || start[n + 1] == '|' || start[n + 1] == '}'))) n++;
        if (!n) return false;
        first = arena_strndup(r->arena, start, n);
        p = start + n;
    }
    if (!first) return false;
    p = skip(p);
    if (is_word(p, "as")) {
        p += 2;
        alias = take_name(r, &p);
        p = skip(p);
    }
    if (*p == '{' || !*p) { /* an entity, with or without attributes */
        int index = class_of_name(r, alias ? alias : first, "entity");
        if (alias) u->classes[index].members[0] = NULL, u->classes[index].name = alias;
        if (alias) { /* shown with its long name, found by the short one */
            u->classes[index].name = first;
            GROW(u->nodes, u->node_count, u->node_capacity);
            u->nodes[u->node_count++] = (UmlNode){alias, first, UML_PLAIN, NULL, r->line, index, 0, 0};
        }
        if (*p == '{') ch.open_entity = index;
        return true;
    }
    /* A ||--o{ B : label */
    size_t n = 0;
    while (p[n] && strchr("|o{}-.", p[n])) n++;
    if (n < 4) return false;
    char mark[16];
    snprintf(mark, sizeof mark, "%.*s", (int)(n < 15 ? n : 15), p);
    const char *line = strstr(mark, "--") ? strstr(mark, "--") : strstr(mark, "..");
    if (!line) return false;
    char left[8], right[8];
    snprintf(left, sizeof left, "%.*s", (int)(line - mark), mark);
    snprintf(right, sizeof right, "%s", line + 2);
    p = skip(p + n);
    const char *second;
    if (*p == '"') {
        second = take_name(r, &p);
    } else {
        size_t m = 0;
        while (entity_name_char(p[m])) m++;
        second = m ? arena_strndup(r->arena, p, m) : NULL;
        p += m;
    }
    if (!second) return false;
    p = skip(p);
    const char *label = *p == ':' ? rest(r, p + 1) : "";
    int a = -1, b = -1;
    for (int i = 0; i < u->node_count; i++) { /* short names */
        if (strcmp(u->nodes[i].id, first) == 0) a = u->nodes[i].parent;
        if (strcmp(u->nodes[i].id, second) == 0) b = u->nodes[i].parent;
    }
    if (a < 0) a = class_of_name(r, first, "entity");
    if (b < 0) b = class_of_name(r, second, "entity");
    GROW(u->relations, u->relation_count, u->relation_capacity);
    u->relations[u->relation_count++] = (UmlRelation){a, b, "relates", label, many_words(left, true), many_words(right, false)};
    return true;
}

/* ---- trees ---- */

static int tree_node(Reader *r, const char *text, int depth) {
    Uml *u = r->uml;
    if (depth > 39) depth = 39;
    if (depth > ch.depth + 1) depth = ch.depth + 1;
    int parent = depth > 0 ? ch.stack[depth - 1] : -1;
    GROW(u->nodes, u->node_count, u->node_capacity);
    u->nodes[u->node_count] = (UmlNode){text, text, depth == 0 ? UML_START : UML_PLAIN, NULL, r->line, parent, depth, 0};
    if (parent >= 0) edge(r, parent, u->node_count, "", false);
    ch.stack[depth] = u->node_count;
    ch.depth = depth;
    return u->node_count++;
}

/* The text of a Mermaid node: id[text], id(text), id((text)), or all of it. */
static const char *node_text(Reader *r, const char *s) {
    const char *open = strpbrk(s, "[({");
    if (open && open > s && s[strlen(s) - 1] != ':') {
        const char *from = open, *to = s + strlen(s);
        while (*from && strchr("[({", *from)) from++;
        while (to > from && strchr("])} ", to[-1])) to--;
        if (*open == ')' || to <= from) return rest(r, s);
        return clean(r, from, (size_t)(to - from));
    }
    if (open == s) {
        const char *from = s, *to = s + strlen(s);
        while (*from && strchr("[({", *from)) from++;
        while (to > from && strchr("])} ", to[-1])) to--;
        return clean(r, from, (size_t)(to - from));
    }
    return rest(r, s);
}

/* Mermaid mind map: depth is how far the line is from the margin. */
static bool mindmap_line(Reader *r, const char *s) {
    if (starts(s, "::")) return true; /* icons and classes */
    int depth = 0;
    while (depth < ch.depth + 1 && depth < 39 && r->indent > ch.indents[depth]) depth++;
    if (r->uml->node_count == 0) depth = 0;
    else if (depth == 0) depth = 1;
    ch.indents[depth] = r->indent;
    const char *text = *s == ')' ? rest(r, s) : node_text(r, s);
    if (starts(s, "))")) { /* id))text(( */
        const char *end = strstr(s + 2, "((");
        text = clean(r, s + 2, end ? (size_t)(end - s - 2) : strlen(s + 2));
    } else {
        const char *bang = strstr(s, "))");
        if (bang && bang > s && strstr(bang, "((")) text = clean(r, bang + 2, (size_t)(strstr(bang, "((") - bang - 2));
    }
    tree_node(r, text, depth);
    return true;
}

/* PlantUML mind maps and work breakdowns: "** a branch", "--- to the left" */
static bool stars_line(Reader *r, const char *s) {
    int depth = 0;
    while (s[depth] == '*' || s[depth] == '+' || s[depth] == '-' || s[depth] == '#') depth++;
    if (!depth) return is_word(s, "left") || is_word(s, "right") || is_word(s, "top") || is_word(s, "skinparam") || *s == '<';
    s += depth;
    if (*s == '_' || *s == '<' || *s == '>') s++;
    if (*s == '[') { /* a colour */
        const char *end = strchr(s, ']');
        if (end) s = end + 1;
    }
    if (*s == ':') s++; /* the first line of a long text */
    tree_node(r, rest(r, s), depth - 1);
    return true;
}

/* Mermaid timeline: "2004 : Facebook : Google", then ": more" */
static bool timeline_line(Reader *r, const char *s) {
    Uml *u = r->uml;
    if (u->node_count == 0) tree_node(r, u->title ? u->title : "timeline", 0);
    if (is_word(s, "title")) {
        u->nodes[0].text = u->nodes[0].id = rest(r, s + 5);
        return true;
    }
    if (is_word(s, "section")) {
        ch.section = rest(r, s + 7);
        tree_node(r, ch.section, 1);
        return true;
    }
    int base = ch.section ? 2 : 1;
    const char *colon = strchr(s, ':');
    if (*s != ':') tree_node(r, clean(r, s, colon ? (size_t)(colon - s) : strlen(s)), base);
    while (colon) {
        const char *next = strchr(colon + 1, ':');
        tree_node(r, clean(r, colon + 1, next ? (size_t)(next - colon - 1) : strlen(colon + 1)), base + 1);
        colon = next;
    }
    return true;
}

/* Mermaid journey: "Make tea: 5: Me" */
static bool journey_line(Reader *r, const char *s) {
    Uml *u = r->uml;
    if (u->node_count == 0) tree_node(r, u->title ? u->title : "journey", 0);
    if (is_word(s, "title")) {
        u->nodes[0].text = u->nodes[0].id = rest(r, s + 5);
        return true;
    }
    if (is_word(s, "section")) {
        ch.section = rest(r, s + 7);
        tree_node(r, ch.section, 1);
        return true;
    }
    const char *colon = strchr(s, ':');
    if (!colon) return false;
    const char *who = strchr(colon + 1, ':');
    const char *task = clean(r, s, (size_t)(colon - s));
    int score = atoi(skip(colon + 1));
    const char *text = who ? arena_printf(r->arena, "%s  %d/5  %s", task, score, rest(r, who + 1))
                           : arena_printf(r->arena, "%s  %d/5", task, score);
    int index = tree_node(r, text, ch.section ? 2 : 1);
    u->nodes[index].value = score;
    return true;
}

/* ---- pies ---- */

static bool pie_line(Reader *r, const char *s) {
    Uml *u = r->uml;
    if (is_word(s, "title")) return true;
    if (is_word(s, "showData")) return true;
    const char *colon = strrchr(s, ':');
    if (!colon) return false;
    const char *label = clean(r, s, (size_t)(colon - s));
    GROW(u->nodes, u->node_count, u->node_capacity);
    u->nodes[u->node_count] = (UmlNode){label, label, UML_PLAIN, NULL, r->line, -1, 0, 0};
    u->nodes[u->node_count++].value = atof(colon + 1);
    return true;
}

/* ---- Graphviz ---- */

/* One word of DOT: a name, a quoted text, or a sign. */
static const char *dot_token(Reader *r, const char **at) {
    const char *s = *at;
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == ';' || *s == ',') {
            if (*s == '\n') r->line++;
            s++;
        }
        if (s[0] == '/' && s[1] == '/') s += strcspn(s, "\n");
        else if (s[0] == '#') s += strcspn(s, "\n");
        else if (s[0] == '/' && s[1] == '*') {
            const char *end = strstr(s, "*/");
            s = end ? end + 2 : s + strlen(s);
        } else break;
    }
    if (!*s) {
        *at = s;
        return NULL;
    }
    if (*s == '"') {
        const char *end = s + 1;
        while (*end && (*end != '"' || end[-1] == '\\')) end++;
        *at = *end ? end + 1 : end;
        return clean(r, s + 1, (size_t)(end - s - 1));
    }
    if (s[0] == '-' && (s[1] == '>' || s[1] == '-')) {
        *at = s + 2;
        return "->";
    }
    if (strchr("{}[]=", *s)) {
        *at = s + 1;
        return arena_strndup(r->arena, s, 1);
    }
    size_t n = 0;
    while (s[n] && !strchr(" \t\n\r;,{}[]=\"", s[n]) && !(s[n] == '-' && (s[n + 1] == '>' || s[n + 1] == '-'))) n++;
    *at = s + n;
    const char *colon = memchr(s, ':', n); /* a port: a:p */
    return arena_strndup(r->arena, s, colon && colon > s ? (size_t)(colon - s) : n);
}

static void dot_parse(Reader *r, const char *text) {
    Uml *u = r->uml;
    const char *s = strchr(text, '{');
    if (!s) return;
    s++;
    r->line = 1;
    for (const char *c = text; c < s; c++) r->line += *c == '\n';
    int last = -1, edge_from = -1, first_edge = 0;
    bool arrow_before = false;
    const char *word;
    while ((word = dot_token(r, &s)) != NULL) {
        if (strcmp(word, "}") == 0 || strcmp(word, "{") == 0) {
            if (word[0] == '}') r->inside[0] = '\0';
            last = -1;
            continue;
        }
        if (strcmp(word, "[") == 0) { /* what is said of the last node, or of the last arrows */
            const char *key;
            while ((key = dot_token(r, &s)) != NULL && strcmp(key, "]") != 0) {
                const char *save = s, *eq = dot_token(r, &s);
                if (!eq || strcmp(eq, "=") != 0) {
                    s = save;
                    continue;
                }
                const char *v = dot_token(r, &s);
                if (!v) break;
                if (strcmp(key, "label") == 0 || strcmp(key, "xlabel") == 0) {
                    if (edge_from >= 0)
                        for (int i = first_edge; i < u->edge_count; i++) u->edges[i].text = v;
                    else if (last >= 0) u->nodes[last].text = v;
                } else if (strcmp(key, "shape") == 0 && last >= 0 && edge_from < 0) {
                    if (strcmp(v, "diamond") == 0) u->nodes[last].shape = UML_DECISION;
                    if (strstr(v, "circle") || strcmp(v, "point") == 0) u->nodes[last].shape = UML_START;
                } else if (strcmp(key, "style") == 0 && edge_from >= 0 && (strstr(v, "dashed") || strstr(v, "dotted"))) {
                    for (int i = first_edge; i < u->edge_count; i++) u->edges[i].dashed = true;
                }
            }
            edge_from = -1;
            last = -1;
            continue;
        }
        if (strcmp(word, "->") == 0) {
            arrow_before = last >= 0;
            continue;
        }
        const char *save = s, *next = dot_token(r, &s);
        if (next && strcmp(next, "=") == 0) { /* rankdir=LR, label="..." */
            const char *v = dot_token(r, &s);
            if (v && strcmp(word, "label") == 0 && !r->inside[0] && !u->title) u->title = v;
            else if (v && strcmp(word, "label") == 0) snprintf(r->inside, sizeof r->inside, "%s", v);
            continue;
        }
        s = save;
        if (strcmp(word, "subgraph") == 0) {
            const char *name = dot_token(r, &s);
            if (name && strcmp(name, "{") != 0) snprintf(r->inside, sizeof r->inside, "%s", starts(name, "cluster_") ? name + 8 : name);
            last = -1;
            continue;
        }
        if (strcmp(word, "node") == 0 || strcmp(word, "edge") == 0 || strcmp(word, "graph") == 0) {
            last = edge_from = -1; /* what follows in [ ] is for everybody */
            continue;
        }
        int here_ = node(r, word, NULL, UML_PLAIN);
        if (arrow_before && last >= 0) {
            if (edge_from < 0) first_edge = u->edge_count;
            edge(r, last, here_, "", false);
            edge_from = last;
        } else {
            edge_from = -1;
        }
        arrow_before = false;
        last = here_;
    }
}

/* ---- which one ---- */

static const char *first_line(const char *text, char *line, size_t size) {
    for (const char *s = text; *s;) {
        size_t n = strcspn(s, "\n");
        snprintf(line, size, "%.*s", (int)(n < size - 1 ? n : size - 1), s);
        const char *l = skip(line);
        if (*l && *l != '\r' && !starts(l, "%%") && *l != '\'' && !starts(l, "---") && !starts(l, "//") && *l != '#') return l;
        s += n;
        if (*s) s++;
    }
    line[0] = '\0';
    return line;
}

static bool has_crow_feet(const char *text) {
    static const char *const MARKS[] = {"||--", "}o--", "}|--", "|o--", "--o{", "--|{", "--||", "--o|", "||..", "}o..", "}|..", "..o{", "..|{"};
    for (size_t i = 0; i < sizeof MARKS / sizeof MARKS[0]; i++)
        if (strstr(text, MARKS[i])) return true;
    return false;
}

/* Reads the diagrams of this file's kinds. False: it is one of the others. */
static bool charts(Reader *r, const char *text, const char *path, bool *failed) {
    Uml *u = r->uml;
    char line[300];
    const char *l = first_line(text, line, sizeof line);
    LineReader read_line = NULL;
    ch = (Charts){.r = r, .open_entity = -1};
    u->first_day = -1;
    bool dot = ends(path, ".dot") || ends(path, ".gv") ||
               ((is_word(l, "digraph") || is_word(l, "strict") || (is_word(l, "graph") && strchr(l, '{'))) && !strstr(text, "@start"));
    if (dot) {
        dot_parse(r, text);
        u->kind = UML_GRAPH;
        u->mermaid = false;
        *failed = u->node_count == 0;
        if (*failed) diag_error(r->diag, (Loc){1, 1}, "there is no diagram in this file");
        return true;
    }
    if (strstr(text, "@startgantt")) read_line = plan_line, u->kind = UML_GANTT;
    else if (strstr(text, "@startmindmap")) read_line = stars_line, u->kind = UML_TREE, u->tree_kind = "mind map";
    else if (strstr(text, "@startwbs")) read_line = stars_line, u->kind = UML_TREE, u->tree_kind = "work breakdown";
    else if (strstr(text, "@start") && has_crow_feet(text)) read_line = er_line, u->kind = UML_ER;
    else if (strstr(text, "@start")) return false;
    else if (is_word(l, "gantt")) read_line = gantt_line, u->kind = UML_GANTT;
    else if (is_word(l, "erDiagram")) read_line = er_line, u->kind = UML_ER;
    else if (is_word(l, "mindmap")) read_line = mindmap_line, u->kind = UML_TREE, u->tree_kind = "mind map";
    else if (is_word(l, "timeline")) read_line = timeline_line, u->kind = UML_TREE, u->tree_kind = "timeline";
    else if (is_word(l, "journey")) read_line = journey_line, u->kind = UML_TREE, u->tree_kind = "journey";
    else if (is_word(l, "pie")) read_line = pie_line, u->kind = UML_PIE;
    else return false;
    u->mermaid = !strstr(text, "@start");
    if (u->kind == UML_PIE && strstr(l, "title")) u->title = rest(r, strstr(l, "title") + 5);

    bool first = u->mermaid, comment = false;
    for (const char *s = text; *s;) {
        size_t n = strcspn(s, "\n");
        r->line++;
        char *raw = arena_strndup(r->arena, s, n);
        s += n;
        if (*s) s++;
        while (n && strchr(" \t\r", raw[n - 1])) raw[--n] = '\0';
        const char *t = skip(raw);
        r->indent = 0;
        for (const char *c = raw; c < t; c++) r->indent += *c == '\t' ? 4 : 1;
        if (starts(t, "/'")) comment = true;
        if (comment) {
            if (strstr(t, "'/")) comment = false;
            continue;
        }
        if (!*t || starts(t, "%%") || *t == '@' || *t == '!' || (*t == '\'' && !u->mermaid)) continue;
        if (starts(t, "---") && u->mermaid) continue;
        if (first) { /* the line that says what it is */
            first = false;
            continue;
        }
        if (is_word(t, "title") && u->kind != UML_TREE) u->title = rest(r, t + 5);
        if (is_word(t, "skinparam") || is_word(t, "hide") || is_word(t, "scale") || is_word(t, "accTitle") || is_word(t, "accDescr") ||
            is_word(t, "caption") || is_word(t, "header") || is_word(t, "footer") || is_word(t, "legend"))
            continue;
        if (!read_line(r, t)) problem(r, t);
    }
    if (u->kind == UML_GANTT) resolve_plan(u);
    bool empty = u->task_count == 0 && u->node_count == 0 && u->class_count == 0;
    *failed = empty;
    if (empty) diag_error(r->diag, (Loc){1, 1}, "there is no diagram in this file");
    return true;
}
