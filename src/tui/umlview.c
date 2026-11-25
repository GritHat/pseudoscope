#include "ui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Diagrams that were only drawn, walked through as if they ran. */

/* ---- sequence ---- */

/* The rows reached after `limit` steps, with one branch of each alternative. */
static int unfold(const Uml *u, unsigned branches, SequenceItem *rows, int max, int limit, int *alternative) {
    int n = 0, taken[16], depth = 0, alt = 0, branch[16], wanted[16];
    bool skipping[16];
    *alternative = -1;
    for (int i = 0; i < u->row_count && n < max && n < limit; i++) {
        const UmlRow *r = &u->rows[i];
        bool skip = false;
        for (int d = 0; d < depth; d++) skip |= skipping[d];
        if (r->kind == UML_GROUP && depth < 16) {
            bool choice = strcmp(r->group, "alt") == 0;
            branch[depth] = 0;
            wanted[depth] = choice ? (int)((branches >> (alt * 2)) & 3) : -1;
            skipping[depth] = choice && wanted[depth] != 0;
            taken[depth] = alt;
            if (choice && alt < 15) alt++;
            if (!skip && !skipping[depth]) {
                rows[n++] = (SequenceItem){.kind = SEQ_FRAME, .label = r->text[0] ? r->text : r->group, .time = -1, .from = 0, .to = 0};
                if (choice) *alternative = taken[depth];
            }
            depth++;
            continue;
        }
        if (r->kind == UML_ELSE && depth) {
            int d = depth - 1;
            branch[d]++;
            bool was = skipping[d];
            skipping[d] = wanted[d] >= 0 && branch[d] != wanted[d];
            bool outer = false;
            for (int k = 0; k < d; k++) outer |= skipping[k];
            if (!outer && !skipping[d] && (was || wanted[d] < 0)) {
                rows[n++] = (SequenceItem){.kind = SEQ_FRAME, .label = r->text, .time = -1};
                if (wanted[d] >= 0) *alternative = taken[d];
            }
            continue;
        }
        if (r->kind == UML_END && depth) {
            depth--;
            continue;
        }
        if (skip) continue;
        switch (r->kind) {
            case UML_MESSAGE:
                rows[n] = (SequenceItem){.kind = r->dashed ? SEQ_RETURN : SEQ_MESSAGE, .from = r->from, .to = r->to, .label = r->text,
                                           .time = -1, .dashed = r->dashed, .activates = !r->dashed, .end = n + 1};
                n++;
                break;
            case UML_NOTE: rows[n++] = (SequenceItem){.kind = SEQ_NOTE, .from = r->from, .to = r->to, .label = r->text, .time = -1}; break;
            case UML_DIVIDER: rows[n++] = (SequenceItem){.kind = SEQ_STEP, .label = r->text, .time = -1}; break;
            default: break;
        }
    }
    /* a call lasts until its answer */
    for (int i = 0; i < n; i++) {
        if (rows[i].kind != SEQ_MESSAGE || rows[i].from == rows[i].to) continue;
        rows[i].end = INT_MAX;
        for (int k = i + 1; k < n; k++) {
            if (rows[k].kind == SEQ_RETURN && rows[k].from == rows[i].to) {
                rows[i].end = k;
                break;
            }
        }
    }
    return n;
}

static void draw_uml_sequence(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    static SequenceItem rows[600];
    const char *lives[64];
    int alternative;
    for (int i = 0; i < u->people_count; i++) lives[i] = u->people[i].name;
    int n = unfold(u, app->walk.branches, rows, 600, app->walk.at, &alternative);
    int current = -1;
    for (int i = 0; i < n; i++)
        if (rows[i].kind == SEQ_MESSAGE || rows[i].kind == SEQ_RETURN) current = rows[i].to;
    SequenceView view = {.lives = lives, .life_count = u->people_count, .current = current, .rows = rows, .row_count = n,
                         .back = app->walk.top};
    draw_sequence(x, y, w, h, &view);
}

/* ---- graphs ---- */

static void draw_uml_graph(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    const char **names = malloc((size_t)(u->node_count + 1) * sizeof *names);
    StateArrow *arrows = malloc((size_t)(u->edge_count + 1) * sizeof *arrows);
    if (!names || !arrows) abort();
    int out = 0, selected = -1;
    for (int i = 0; i < u->node_count; i++) names[i] = u->nodes[i].text;
    for (int i = 0; i < u->edge_count; i++) {
        arrows[i] = (StateArrow){u->edges[i].from, u->edges[i].to, u->edges[i].text};
        if (u->edges[i].from == app->walk.at && out++ == app->walk.choice) selected = i;
    }
    int limit = u->node_count > 128 ? 128 : u->node_count;
    StateView view = {.states = names, .state_count = limit, .current = app->walk.at, .arrows = arrows, .arrow_count = u->edge_count,
                      .last = app->walk.depth ? app->walk.arrows[app->walk.depth - 1] : -1, .selected = selected};
    int side = w > 90 ? 34 : 0;
    draw_states(x, y, w - side, h, &view);
    if (side) { /* the way done so far */
        int sx = x + w - side + 2, row = y;
        vline(sx - 2, y, h, S_FAINT, "│");
        print_at(sx, row, side - 2, S_BOLD, "%s", L("Il cammino fatto", "The way so far"));
        row += 2;
        int first = app->walk.depth > h - 6 ? app->walk.depth - (h - 6) : 0;
        for (int i = first; i <= app->walk.depth && row < y + h; i++) {
            int node = i < app->walk.depth ? app->walk.history[i] : app->walk.at;
            print_at(sx, row++, side - 2, i == app->walk.depth ? S(16, C_FOCUS_BG, A_BOLD) : S_DIM, " %s ", u->nodes[node].text);
        }
    }
    free(names);
    free(arrows);
}

/* ---- classes ---- */

static int class_height(const UmlClass *c) { return c->member_count + 3 + (c->member_count ? 0 : 0); }

static void class_box(const UmlClass *c, int x, int y, int w, bool selected, int bottom) {
    int h = class_height(c);
    if (y + h > bottom) h = bottom - y;
    if (h < 3) return;
    draw_box(x, y, w, h, selected ? C_FOCUS : C_TYPE, NULL);
    bool special = strcmp(c->kind, "class") != 0;
    print_centered(x + 1, y + 1, w - 2, S(C_TYPE, C_DEFAULT, A_BOLD | (special ? A_ITALIC : 0)), special ? "%s " M_FAINT "«%s»" : "%s", c->name, c->kind);
    for (int i = 0; i < c->member_count && y + 2 + i < y + h - 1; i++) {
        bool method = strchr(c->members[i], '(') != NULL;
        print_at(x + 2, y + 2 + i, w - 4, S(method ? C_CALL : C_TEXT, C_DEFAULT, 0), "%s", c->members[i]);
    }
}

static const char *many_words(const char *many) {
    if (!many) return "";
    if (strcmp(many, "zero or more") == 0) return L("zero o più", "zero or more");
    if (strcmp(many, "one or more") == 0) return L("uno o più", "one or more");
    if (strcmp(many, "zero or one") == 0) return L("zero o uno", "zero or one");
    return L("uno", "one");
}

const char *uml_kind_name(const Uml *u) {
    static const char *const IT[] = {"", "diagramma di sequenza", "diagramma di stato", "diagramma delle classi", "diagramma",
                                     "piano (Gantt)", "entità e relazioni", "albero", "torta"};
    static const char *const EN[] = {"", "sequence diagram", "state diagram", "class diagram", "diagram", "plan (Gantt)",
                                     "entities and relationships", "tree", "pie"};
    if (u->kind == UML_TREE && u->tree_kind) {
        if (strcmp(u->tree_kind, "mind map") == 0) return L("mappa mentale", "mind map");
        if (strcmp(u->tree_kind, "work breakdown") == 0) return L("scomposizione del lavoro (WBS)", "work breakdown (WBS)");
        if (strcmp(u->tree_kind, "timeline") == 0) return L("linea del tempo", "timeline");
        if (strcmp(u->tree_kind, "journey") == 0) return L("percorso", "journey");
        if (strcmp(u->tree_kind, "outline") == 0) return L("indice", "outline");
    }
    return L(IT[u->kind], EN[u->kind]);
}

static const char *relation_words(const char *kind) {
    if (strcmp(kind, "extends") == 0) return L("è un tipo di", "is a kind of");
    if (strcmp(kind, "implements") == 0) return L("realizza", "implements");
    if (strcmp(kind, "owns") == 0) return L("è fatta di", "is made of");
    if (strcmp(kind, "has") == 0) return L("contiene", "contains");
    if (strcmp(kind, "uses") == 0) return L("usa", "uses");
    return L("conosce", "knows");
}

static void draw_uml_classes(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    int side = 38, per = (w - side) / 30;
    if (per < 1) per = 1;
    int width = (w - side) / per, tops[8] = {0};
    if (per > 8) per = 8;
    for (int i = 0; i < u->class_count; i++) { /* each in the shortest column */
        int best = 0;
        for (int k = 1; k < per; k++)
            if (tops[k] < tops[best]) best = k;
        if (tops[best] + 3 <= h) class_box(&u->classes[i], x + best * width, y + tops[best], width - 2, i == app->walk.selected, y + h);
        tops[best] += class_height(&u->classes[i]) + 1;
    }
    int sx = x + w - side + 2, row = y;
    vline(sx - 2, y, h, S_FAINT, "│");
    const UmlClass *c = &u->classes[app->walk.selected];
    print_at(sx, row, side - 3, S(C_FOCUS, C_DEFAULT, A_BOLD), "%s", c->name);
    row += 2;
    for (int i = 0; i < u->relation_count && row + 1 < y + h; i++) {
        const UmlRelation *r = &u->relations[i];
        if (r->from != app->walk.selected && r->to != app->walk.selected) continue;
        bool out = r->from == app->walk.selected;
        if (r->from_many) { /* entities: how many of each */
            const UmlClass *other = &u->classes[out ? r->to : r->from];
            print_at(sx, row++, side - 3, S_TEXT, M_FOCUS "%s " M_RESET M_DIM "%s", many_words(out ? r->from_many : r->to_many), L("di questa", "of this"));
            print_at(sx, row++, side - 3, S_TEXT, M_DIM "  %s", r->text[0] ? r->text : "—");
            print_at(sx, row++, side - 3, S_TEXT, M_FOCUS "%s " M_TYPE M_BOLD "%s", many_words(out ? r->to_many : r->from_many), other->name);
            row++;
            continue;
        }
        if (out) print_at(sx, row++, side - 3, S_TEXT, M_DIM "%s " M_TYPE M_BOLD "%s", relation_words(r->kind), u->classes[r->to].name);
        else print_at(sx, row++, side - 3, S_TEXT, M_TYPE M_BOLD "%s" M_RESET M_DIM " %s %s", u->classes[r->from].name, relation_words(r->kind), L("questa", "this"));
    }
}

/* ---- keys and frame ---- */

static int out_edges(const Uml *u, int node) {
    int n = 0;
    for (int i = 0; i < u->edge_count; i++) n += u->edges[i].from == node;
    return n;
}

void uml_draw(App *app) {
    const Uml *u = app->doc->uml;
    int w = screen_width(), h = screen_height();
    char path[300];
    const char *name = strrchr(app->doc->path, '/');
    snprintf(path, sizeof path, M_TEXT "%s" M_FAINT " ▸ " M_TYPE "%s%s%s", name ? name + 1 : app->doc->path, uml_kind_name(u),
             u->title ? M_FAINT " ▸ " M_TEXT : "", u->title ? u->title : "");
    draw_header(path, NULL, 0, 0, 0);
    draw_box(0, 2, w, h - 4, C_TYPE, u->title ? u->title : NULL);
    int x = 2, y = 4, iw = w - 4, ih = h - 7;
    const char *play = L("va da solo", "play"), *space = L("Spazio", "Space"), *files = L("file", "files"), *help = L("aiuto", "help");
    switch (u->kind) {
        case UML_SEQUENCE: {
            draw_uml_sequence(app, x, y, iw, ih);
            const Hint hints[] = {{"→", L("messaggio", "message")}, {"←", L("indietro", "back")}, {space, play}, {"g", L("tutto", "all")},
                                  {"Tab", L("altra alternativa", "other branch")}, {"0", L("da capo", "restart")},
                                  {"Esc", files}, {"?", help}};
            draw_footer(hints, 8);
            break;
        }
        case UML_CLASS: case UML_ER: {
            draw_uml_classes(app, x, y, iw, ih);
            const Hint hints[] = {{"↑↓", u->kind == UML_ER ? L("entità", "entity") : L("classe", "class")}, {"E", L("modifica", "edit")},
                                  {"Esc", files}, {"?", help}};
            draw_footer(hints, 4);
            break;
        }
        case UML_GANTT: {
            draw_uml_plan(app, x, y, iw, ih);
            const Hint hints[] = {{"→", L("avanti nel tempo", "on in time")}, {"←", L("indietro", "back")}, {space, play},
                                  {"↑↓", L("attività", "task")}, {"0", L("da capo", "restart")}, {"Esc", files}, {"?", help}};
            draw_footer(hints, 7);
            break;
        }
        case UML_TREE: {
            draw_uml_tree(app, x, y, iw, ih);
            const Hint hints[] = {{"↑↓", L("scorri", "move")}, {"→", L("dentro", "into")}, {"←", L("fuori", "out")}, {space, play},
                                  {"0", L("da capo", "restart")}, {"Esc", files}, {"?", help}};
            draw_footer(hints, 7);
            break;
        }
        case UML_PIE: {
            draw_uml_pie(app, x, y, iw, ih);
            const Hint hints[] = {{"↑↓", L("parte", "slice")}, {"Esc", files}, {"?", help}};
            draw_footer(hints, 3);
            break;
        }
        default: {
            draw_uml_graph(app, x, y, iw, ih);
            const Hint hints[] = {{"↑↓", L("quale freccia", "which arrow")}, {"→", L("segui", "follow")}, {"←", L("indietro", "back")},
                                  {space, play}, {"0", L("da capo", "restart")}, {"Esc", files}, {"?", help}};
            draw_footer(hints, 7);
        }
    }
}

static void restart(App *app) {
    const Uml *u = app->doc->uml;
    app->walk = (UmlWalk){0};
    if (u->kind == UML_SEQUENCE) app->walk.at = 1;
    if (u->kind == UML_GANTT || u->kind == UML_TREE || u->kind == UML_PIE || u->kind == UML_ER) return;
    for (int i = 0; u->kind != UML_SEQUENCE && i < u->node_count; i++) {
        if (u->nodes[i].shape == UML_START && out_edges(u, i)) {
            app->walk.at = i;
            break;
        }
    }
}

/* On its own, by the arrow taken least so far: so that it all gets seen. */
static void follow_least(App *app) {
    const Uml *u = app->doc->uml;
    UmlWalk *k = &app->walk;
    int best = -1, fewest = INT_MAX, n = 0;
    for (int i = 0; i < u->edge_count; i++) {
        if (u->edges[i].from != k->at) continue;
        int times = 0;
        for (int d = 0; d < k->depth; d++) times += k->arrows[d] == i;
        if (times < fewest) {
            fewest = times;
            best = n;
        }
        n++;
    }
    if (best < 0) {
        k->playing = false;
        return;
    }
    k->choice = best;
    uml_key(app, K_RIGHT);
}

int uml_tick(App *app) {
    UmlWalk *k = &app->walk;
    if (!k->playing || !app->doc->uml) return 1000;
    long long now = now_ms();
    if (now >= k->next_tick) {
        k->next_tick = now + 900;
        UmlKind kind = app->doc->uml->kind;
        if (kind == UML_SEQUENCE || kind == UML_GANTT || kind == UML_TREE) {
            int before = k->at;
            uml_key(app, kind == UML_TREE ? K_DOWN : K_RIGHT);
            k->playing = k->at != before;
        } else {
            follow_least(app);
        }
    }
    long long wait = k->next_tick - now_ms();
    return !k->playing ? 1000 : wait < 0 ? 0 : (int)wait;
}

void uml_key(App *app, int key) {
    const Uml *u = app->doc->uml;
    UmlWalk *k = &app->walk;
    static SequenceItem rows[600];
    int alternative;
    switch (key) {
        case K_ESC: case K_BACKSPACE: {
            char *path = strdup(app->doc->path);
            files_back(app, path);
            free(path);
            return;
        }
        case 'q': app->quit = true; return;
        case ' ':
            if (u->kind == UML_CLASS || u->kind == UML_ER || u->kind == UML_PIE) return;
            k->playing = !k->playing;
            k->next_tick = now_ms();
            return;
        case '0': case K_HOME: restart(app); return;
        default: break;
    }
    if (u->kind == UML_SEQUENCE) {
        int total = unfold(u, k->branches, rows, 600, 600, &alternative);
        if ((key == K_RIGHT || key == K_ENTER || key == K_DOWN) && k->at < total) k->at++;
        else if ((key == K_LEFT || key == K_UP) && k->at > 0) k->at--;
        else if (key == 'g') k->at = total;
        else if (key == K_TAB) {
            unfold(u, k->branches, rows, 600, k->at, &alternative);
            if (alternative < 0) alternative = 0;
            unsigned shift = (unsigned)alternative * 2, now = (k->branches >> shift) & 3, most = 0;
            int seen = -1; /* how many branches it has */
            for (int i = 0; i < u->row_count; i++) {
                if (u->rows[i].kind == UML_GROUP && strcmp(u->rows[i].group, "alt") == 0) seen++;
                if (u->rows[i].kind == UML_ELSE && seen == alternative) most++;
            }
            now = now >= most || now >= 3 ? 0 : now + 1;
            k->branches = (k->branches & ~(3u << shift)) | (now << shift);
        }
        return;
    }
    if (u->kind == UML_GANTT) {
        long last = 0;
        for (int i = 0; i < u->task_count; i++)
            if (u->tasks[i].end > last) last = u->tasks[i].end;
        if (key == K_RIGHT && k->at < last) k->at = (int)plan_step(u, k->at, 1);
        else if (key == K_LEFT && k->at > 0) k->at = (int)plan_step(u, k->at, -1);
        else if (key == K_DOWN && k->selected < u->task_count - 1) k->selected++;
        else if (key == K_UP && k->selected > 0) k->selected--;
        else if (key == 'g' || key == K_END) k->at = (int)last;
        return;
    }
    if (u->kind == UML_TREE) {
        if (key == K_DOWN && k->at < u->node_count - 1) k->at++;
        else if (key == K_UP && k->at > 0) k->at--;
        else if (key == K_LEFT && u->nodes[k->at].parent >= 0) k->at = u->nodes[k->at].parent;
        else if ((key == K_RIGHT || key == K_ENTER) && k->at + 1 < u->node_count && u->nodes[k->at + 1].parent == k->at) k->at++;
        else if (key == K_TAB) { /* the next branch at the same depth */
            for (int i = k->at + 1; i < u->node_count; i++) {
                if (u->nodes[i].depth > u->nodes[k->at].depth) continue;
                k->at = i;
                break;
            }
        } else if (key == 'g' || key == K_END) k->at = u->node_count - 1;
        return;
    }
    if (u->kind == UML_PIE) {
        if (key == K_DOWN && k->selected < u->node_count - 1) k->selected++;
        else if (key == K_UP && k->selected > 0) k->selected--;
        return;
    }
    if (u->kind == UML_CLASS || u->kind == UML_ER) {
        if ((key == K_DOWN || key == K_RIGHT || key == K_TAB) && k->selected < u->class_count - 1) k->selected++;
        if ((key == K_UP || key == K_LEFT) && k->selected > 0) k->selected--;
        return;
    }
    int out = out_edges(u, k->at);
    if (key == K_DOWN && k->choice < out - 1) k->choice++;
    else if (key == K_UP && k->choice > 0) k->choice--;
    else if ((key == K_RIGHT || key == K_ENTER) && out) {
        int n = 0;
        for (int i = 0; i < u->edge_count; i++) {
            if (u->edges[i].from != k->at || n++ != k->choice) continue;
            if (k->depth == 255) { /* forget the oldest */
                memmove(k->history, k->history + 1, 254 * sizeof(int));
                memmove(k->arrows, k->arrows + 1, 254 * sizeof(int));
                k->depth--;
            }
            k->history[k->depth] = k->at;
            k->arrows[k->depth++] = i;
            k->at = u->edges[i].to;
            k->choice = 0;
            break;
        }
    } else if (key == K_LEFT && k->depth) {
        k->at = k->history[--k->depth];
        k->choice = 0;
    } else if (key == K_TAB) { /* diagrams without a beginning: any node */
        k->at = (k->at + 1) % u->node_count;
        k->choice = k->depth = 0;
    }
}
