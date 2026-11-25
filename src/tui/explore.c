#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Item *selected_item(const App *app) {
    if (!app->doc || app->doc->item_count == 0) return NULL;
    return &app->doc->items[app->explore.selected];
}

void explore_open(App *app, const char *path) {
    Document *doc = document_load(path, &app->config);
    if (!doc) {
        snprintf(app->notice, sizeof app->notice, T("Cannot read %s"), path);
        return;
    }
    run_close(app);
    trial_close(app);
    document_free(app->doc);
    app->doc = doc;
    app->explore = (Explore){0};
    if (doc->ok && doc->main_item >= 0) app->explore.selected = doc->main_item;
    if (doc->program && doc->program->classes) app->explore.selected = 0;
    tests_close(app);
    app->walk = (UmlWalk){0};
    app->view = VIEW_EXPLORE;
    if (doc->uml && doc->ok) {
        app->view = VIEW_UML;
        uml_key(app, '0');
    }
}

void explore_reload(App *app) {
    if (!app->doc) return;
    char *path = strdup(app->doc->path);
    Explore keep = app->explore;
    explore_open(app, path);
    free(path);
    app->explore = keep;
    if (app->doc->uml) return;
    if (app->explore.selected >= app->doc->item_count) app->explore.selected = 0;
    if (!app->doc->ok) app->explore.level = 0;
}

/* ---- cards ---- */

static const char *const IN_ARROW = "───▶", *const BOTH_ARROW = "◀──▶";

/* The name of what is selected, and what it is. In a narrow window, or in a
 * language of longer words, what it is goes below. Gives the lines it took
 * beyond the first. */
static int heading(int x, int y, int w, const char *colour, const char *name, const char *what) {
    if (text_width(name) + 2 + text_width(what) <= w) {
        print_at(x, y, w, S_TEXT, "%s" M_BOLD "%s" M_RESET "  " M_DIM "%s", colour, name, what);
        return 0;
    }
    print_at(x, y, w, S_TEXT, "%s" M_BOLD "%s", colour, name);
    return print_wrapped(x, y + 1, w, 2, S_DIM, "%s", what);
}

/* A subprogram seen from outside: what goes in, what comes out. */
static int draw_black_box(const App *app, const Item *item, int x, int y, int w) {
    const Function *f = item->function;
    const Symbol *sym = f->symbol;
    int left = 0, right = sym->result ? 1 : 0, skip = f->owner ? 1 : 0;
    for (int i = skip; i < sym->param_count; i++) {
        if (sym->params[i]->mode == MODE_OUT) right++;
        else left++;
    }
    int rows = left > right ? left : right;
    if (rows < 1) rows = 1;
    /* the names of what goes in and comes out are read whole: the name in the box, already above, gives way */
    int widest = sym->result ? text_width(T("result")) : 0;
    for (int i = skip; i < sym->param_count; i++) {
        int needed = text_width(sym->params[i]->name);
        if (needed > widest) widest = needed;
    }
    int box = text_width(item->name) + 6;
    if (box < 18) box = 18;
    if (box > w - 2 * (widest + 6)) box = w - 2 * (widest + 6);
    if (box < 12) box = 12;
    if (box > w - 8) box = w - 8;
    int side = (w - box) / 2, bx = x + side, height = rows * 2 + 1;
    draw_box(bx, y, box, height, C_CALL, NULL);
    print_centered(bx + 1, y + height / 2, box - 2, S(C_CALL, C_DEFAULT, A_BOLD), "%s", item->name);

    int l = 0, r = 0;
    for (int i = skip; i < sym->param_count; i++) {
        const Symbol *p = sym->params[i];
        char type[80] = "", label[180];
        describe_type(app->doc, p->type, type, sizeof type);
        bool out = p->mode == MODE_OUT, both = p->mode == MODE_INOUT;
        snprintf(label, sizeof label, "%s " M_FAINT "%s", p->name, type);
        Style st = S(out || both ? C_WRITE : C_READ, C_DEFAULT, A_BOLD);
        /* where there is no room for both, the type goes on the line below, which is free */
        int room = side - 6, named = text_width(p->name), typed = text_width(type);
        bool stacked = named + 1 + typed > room;
        if (named > room) named = room;
        if (typed > room) typed = room;
        if (out) {
            int row = y + 1 + 2 * r++;
            put_text(bx + box, row, S(C_WRITE, C_DEFAULT, 0), IN_ARROW);
            print_at(bx + box + 5, row, room, st, "%s", stacked ? p->name : label);
            if (stacked) print_at(bx + box + 5, row + 1, room, S_FAINT, "%s", type);
        } else {
            int row = y + 1 + 2 * l++;
            put_text(bx - 4, row, S(both ? C_WRITE : C_READ, C_DEFAULT, 0), both ? BOTH_ARROW : IN_ARROW);
            print_at(bx - 5 - (stacked ? named : named + 1 + typed), row, room, st, "%s", stacked ? p->name : label);
            if (stacked) print_at(bx - 5 - typed, row + 1, room, S_FAINT, "%s", type);
        }
    }
    if (sym->result) {
        char type[80] = "";
        describe_type(app->doc, sym->type, type, sizeof type);
        int row = y + 1 + 2 * r;
        put_text(bx + box, row, S(C_WRITE, C_DEFAULT, 0), IN_ARROW);
        if (text_width(T("result")) + 1 + text_width(type) <= side - 6) {
            print_at(bx + box + 5, row, side - 6, S(C_WRITE, C_DEFAULT, A_BOLD), "%s " M_FAINT "%s", T("result"), type);
        } else {
            print_at(bx + box + 5, row, side - 6, S(C_WRITE, C_DEFAULT, A_BOLD), "%s", T("result"));
            print_at(bx + box + 5, row + 1, side - 6, S_FAINT, "%s", type);
        }
    }
    return height;
}

static void draw_names(int x, int y, int w, const char *label, const Function **names, int count) {
    int at = x + print_at(x, y, w, S_DIM, "%s", label) + 1;
    if (at < x + 12) at = x + 12;
    if (count == 0) {
        print_at(at, y, w, S_FAINT, "%s", T("none"));
        return;
    }
    for (int i = 0; i < count; i++) {
        int need = text_width(names[i]->name) + 3;
        if (at + need > x + w) {
            put_text(at, y, S_DIM, "…");
            return;
        }
        at += print_at(at, y, -1, S_TEXT, M_CHIP_OFF M_CALL " %s ", names[i]->name) + 1;
    }
}

static void function_card(const App *app, const Item *item, int x, int y, int w, int h) {
    const Function *f = item->function;
    bool is_function = !f->is_procedure;
    int more = heading(x, y, w, M_CALL, f->full_name,
                       f->is_handler ? T("reacts to a message")
                       : is_function ? T("function: computes a result")
                                     : T("procedure: performs actions"));
    y += more, h -= more;
    int used = draw_black_box(app, item, x, y + 2, w);
    int row = y + 3 + used;
    const Function *callers[64];
    int count = 0;
    for (int i = 0; i < app->doc->item_count; i++) {
        const Item *other = &app->doc->items[i];
        for (int k = 0; k < other->call_count; k++)
            if (other->calls[k] == f && count < 64) callers[count++] = other->function;
    }
    if (row < y + h) draw_names(x, row++, w, T("uses"), item->calls, item->call_count);
    if (row < y + h) draw_names(x, row++, w, T("used by"), callers, count);
    bool recursive = false;
    for (int k = 0; k < item->call_count; k++) recursive |= item->calls[k] == f;
    if (recursive && row < y + h)
        print_at(x, row++, w, S_TEXT, M_FOCUS "↻ " M_RESET "%s",
                 T("recursive: calls itself"));
    row++;
    if (row + 1 < y + h) {
        bool main_item = app->doc->main_item >= 0 && item == &app->doc->items[app->doc->main_item];
        print_at(x, row, w, S_DIM, M_KEY " r " M_RESET " %s",
                 main_item ? T("run the program")
                           : T("try it with values of your choice"));
    }
}

/* A class as UML draws it; a component with the messages it gets and sends. */
typedef struct {
    const char *names[24];
    int count;
} Names;

static void add_name(Names *n, const char *name) {
    for (int i = 0; i < n->count; i++)
        if (strcmp(n->names[i], name) == 0) return;
    if (n->count < 24) n->names[n->count++] = name;
}

static void find_sent(const Stmt *s, Names *out) {
    for (; s; s = s->next) {
        for (const Stmt *b = s; b; b = b->otherwise) {
            bool self = b->target && b->target->kind == EX_NAME && strcmp(b->target->text, "self") == 0;
            if (b->kind == ST_SEND && !self) add_name(out, b->name);
            find_sent(b->body, out);
        }
    }
}

static void class_card(const App *app, const Item *item, int x, int y, int w, int h) {
    const Class *c = item->klass;
    int more = heading(x, y, w, M_TYPE, c->name,
                       c->is_component ? T("component: lives on its own and exchanges messages")
                       : c->is_interface ? T("interface: what its classes must be able to do")
                       : c->is_abstract || c->pending ? T("abstract class: those that extend it complete it")
                                                      : T("class: data and operations together"));
    y += more, h -= more;
    if (c->bases) { /* what it extends, and so what it also is */
        char bases[200] = "";
        for (const BaseName *b = c->bases; b; b = b->next) {
            size_t n = strlen(bases);
            snprintf(bases + n, sizeof bases - n, "%s%s", n ? ", " : "", b->name);
        }
        print_at(x, y + 1, w, S_TEXT, M_DIM "%s " M_RESET M_TYPE "%s", T("extends"), bases);
    }
    Names in = {0}, out = {0}, methods = {0};
    int fields = 0;
    for (const VarDecl *f = c->record->fields; f; f = f->next) fields += !f->symbol->type->states;
    for (const Function *f = app->doc->program->functions; f; f = f->next) {
        if (f->owner != c) continue;
        if (f->is_handler && strcmp(f->name, "start") != 0) add_name(&in, f->name);
        if (!f->is_handler) add_name(&methods, f->name);
        find_sent(f->body, &out);
    }
    int rows = fields + methods.count + (methods.count ? 1 : 0);
    int ports = in.count > out.count ? in.count : out.count;
    if (ports > rows) rows = ports;
    if (rows < 1) rows = 1;
    if (rows > h - 8) rows = h - 8;
    int box = c->is_component ? w / 3 : w / 2, bx = x + (c->is_component ? w / 3 : 0);
    if (box < 24) box = 24;
    draw_box(bx, y + 2, box, rows + 4, C_TYPE, NULL);
    print_centered(bx + 1, y + 3, box - 2, S(C_TYPE, C_DEFAULT, A_BOLD), "%s", c->name);
    hline(bx + 1, y + 4, box - 2, S(C_TYPE, C_DEFAULT, 0), "─");
    int row = y + 5, bottom = y + 5 + rows;
    for (const VarDecl *f = c->record->fields; f && row < bottom; f = f->next) {
        if (f->symbol->type->states) continue;
        char type[80] = "";
        describe_type(app->doc, f->symbol->type, type, sizeof type);
        print_at(bx + 2, row++, box - 4, S(C_READ, C_DEFAULT, 0), "%s " M_FAINT "%s", f->name, type);
    }
    if (methods.count && row < bottom) hline(bx + 1, row++, box - 2, S(C_FAINT, C_DEFAULT, 0), "─");
    for (int i = 0; i < methods.count && row < bottom; i++)
        print_at(bx + 2, row++, box - 4, S(C_CALL, C_DEFAULT, 0), "%s()", methods.names[i]);
    for (int i = 0; i < in.count && i < rows; i++) {
        int used = text_width(in.names[i]), room = w / 3 - 7;
        if (used > room) used = room;
        print_at(bx - 5 - used, y + 5 + i, room, S(C_READ, C_DEFAULT, A_BOLD), "%s", in.names[i]);
        put_text(bx - 4, y + 5 + i, S(C_READ, C_DEFAULT, 0), IN_ARROW);
    }
    for (int i = 0; i < out.count && i < rows && c->is_component; i++) {
        put_text(bx + box, y + 5 + i, S(C_WRITE, C_DEFAULT, 0), IN_ARROW);
        print_at(bx + box + 5, y + 5 + i, w / 3 - 6, S(C_WRITE, C_DEFAULT, A_BOLD), "%s", out.names[i]);
    }
    int below = bottom + 2;
    if (c->states && below + 4 < y + h) {
        const char *names[64];
        StateArrow arrows[96];
        StateView view = {.states = names, .arrows = arrows, .current = -1, .last = -1, .selected = -1};
        for (const StateName *s = c->states; s && view.state_count < 64; s = s->next) names[view.state_count++] = s->name;
        view.arrow_count = class_transitions(app->doc, c, arrows, 96);
        print_at(x, below, w, S_BOLD, "%s", T("Its states"));
        draw_states(x, below + 1, w, y + h - below - 1, &view);
    }
}

static void test_card(const App *app, const Item *item, int x, int y, int w, int h) {
    (void)app;
    print_wrapped(x, y, w, 2, S_BOLD, "%s", item->name);
    int row = y + 3;
    for (const Stmt *s = item->function->body; s && row < y + h - 4; s = s->next) {
        if (s->kind == ST_STEP) print_at(x, row++, w, S(C_FOCUS, C_DEFAULT, 0), "● %s", s->text);
        if (s->kind == ST_EXPECT || s->kind == ST_CHECK) print_at(x + 3, row++, w - 3, S_DIM, "%s", s->kind == ST_EXPECT ? s->name : s->text ? s->text : "check");
    }
    print_at(x, row + 1, w, S_DIM, M_KEY " r " M_RESET " %s", T("watch it run"));
    print_at(x, row + 3, w, S_DIM, M_KEY " t " M_RESET " %s", T("run all the tests"));
}

static int record_height(const Record *record) {
    int h = 2;
    for (const VarDecl *f = record->fields; f; f = f->next) {
        Type *type = f->symbol->type;
        while (type->kind == TY_ARRAY) type = type->base;
        h += type->kind == TY_RECORD && !type->record->name ? record_height(type->record) : 1;
    }
    return h;
}

/* Boxes inside boxes; records with a name of their own are only mentioned. */
static int draw_record(const App *app, const Record *record, const char *title, int x, int y, int w, int bottom) {
    int h = record_height(record);
    if (y + h > bottom || w < 16) {
        if (y < bottom) put_text(x, y, S_DIM, "…");
        return 1;
    }
    draw_box(x, y, w, h, C_TYPE, title);
    int row = y + 1;
    for (const VarDecl *f = record->fields; f; f = f->next) {
        Type *type = f->symbol->type, *inner = type;
        while (inner->kind == TY_ARRAY) inner = inner->base;
        if (inner->kind == TY_RECORD && !inner->record->name) {
            char label[80];
            snprintf(label, sizeof label, "%s%s", f->name, type->kind == TY_ARRAY ? " []" : "");
            row += draw_record(app, inner->record, label, x + 2, row, w - 4, bottom);
            continue;
        }
        char text[100] = "";
        describe_type(app->doc, type, text, sizeof text);
        bool pointer = type->kind == TY_POINTER;
        if (pointer) text[strlen(text) - 1] = '\0';
        print_at(x + 2, row, 16, S(C_READ, C_DEFAULT, A_BOLD), "%s", f->name);
        print_at(x + 18, row, w - 20, S_DIM, "%s%s", pointer ? M_POINTER "──▶ " M_RESET : "", text);
        row++;
    }
    return h;
}

static void record_card(const App *app, const Item *item, int x, int y, int w, int h) {
    int more = heading(x, y, w, M_TYPE, item->name, T("record: several values together, each with a name"));
    y += more, h -= more;
    int width = w < 56 ? w : 56;
    int used = draw_record(app, item->record, item->name, x, y + 2, width, y + h - 2);
    const Function *users[64];
    int count = 0;
    for (const Function *f = app->doc->program->functions; f; f = f->next) {
        bool uses = false;
        const VarDecl *lists[] = {f->params, f->locals};
        for (int k = 0; k < 2; k++) {
            for (const VarDecl *v = lists[k]; v; v = v->next) {
                Type *type = v->symbol ? v->symbol->type : NULL;
                while (type && (type->kind == TY_ARRAY || type->kind == TY_POINTER)) type = type->base;
                uses |= type && type->kind == TY_RECORD && type->record == item->record;
            }
        }
        if (uses && count < 64) users[count++] = f;
    }
    if (y + used + 3 < y + h) draw_names(x, y + used + 3, w, TC("masculine", "used by"), users, count);
}

static void globals_card(const App *app, int x, int y, int w, int h) {
    print_at(x, y, w, S_TEXT, M_BOLD "%s" M_RESET "  " M_DIM "%s", T("Global variables"),
             T("visible from the whole program"));
    int row = y + 2;
    for (const VarDecl *v = app->doc->program->globals; v && row < y + h; v = v->next) {
        char type[100] = "";
        if (v->symbol) describe_type(app->doc, v->symbol->type, type, sizeof type);
        print_at(x, row, 18, S(C_READ, C_DEFAULT, A_BOLD), "%s", v->name);
        print_at(x + 18, row++, w - 18, S_DIM, "%s", type);
    }
}

/* ---- levels ---- */

/* Methods stay with their class. */
static ItemKind section_of(const Item *item) {
    if (item->function && item->function->owner) return item->function->owner->is_component ? ITEM_COMPONENT : ITEM_CLASS;
    return item->kind;
}

static const char *group_title(ItemKind kind) {
    switch (kind) {
        case ITEM_COMPONENT: return T("Components");
        case ITEM_CLASS: return T("Classes");
        case ITEM_TEST: return T("Tests");
        case ITEM_FUNCTION: return T("Subprograms");
        case ITEM_RECORD: return T("Structures (records)");
        default: return T("Shared data");
    }
}

static void draw_map(App *app, int w, int h) {
    const Document *doc = app->doc;
    int list = w / 3 < 28 ? 28 : w / 3;
    if (list > 40) list = 40;
    draw_box(0, 2, list, h - 4, C_READ, T("Map"));
    int row = 3, kind = -1, rows = 0, at = 0;
    for (int i = 0; i < doc->item_count; i++) { /* where the selected one falls */
        if ((int)section_of(&doc->items[i]) != kind) {
            rows += kind >= 0 ? 2 : 1;
            kind = (int)section_of(&doc->items[i]);
        }
        if (i == app->explore.selected) at = rows;
        rows++;
    }
    int top = scroll_to(app->explore.top, at, 0, rows - 1, h - 6);
    app->explore.top = top;
    kind = -1;
    row = 3 - top;
    for (int i = 0; i < doc->item_count; i++) {
        const Item *item = &doc->items[i];
        if ((int)section_of(item) != kind) {
            if (kind >= 0) row++;
            kind = (int)section_of(item);
            if (row >= 3 && row < h - 3) print_at(2, row, list - 4, S_DIM, "%s", group_title(section_of(item)));
            row++;
        }
        if (row >= 3 && row < h - 3) {
            const char *icon = item->kind == ITEM_COMPONENT || item->kind == ITEM_CLASS ? M_TYPE "▣"
                             : item->kind == ITEM_TEST ? M_WRITE "◇"
                             : item->function && item->function->is_handler ? M_READ "  →"
                             : item->function && item->function->owner ? M_CALL "  ■" : item->kind == ITEM_RECORD ? M_TYPE "▦" : item->kind == ITEM_GLOBALS ? M_READ "●"
                             : item->function->is_procedure ? M_CALL "■" : M_CALL "ƒ";
            draw_table_row(1, row, list - 2, i == app->explore.selected, "%s" M_RESET " %s" M_WRITE "%s%s", icon,
                           item->kind == ITEM_GLOBALS ? T("global variables") : item->name,
                           i == doc->main_item ? "  " : "", i == doc->main_item ? T("starts here") : "");
        }
        row++;
    }
    const Item *item = selected_item(app);
    draw_box(list, 2, w - list, h - 4, C_BORDER, NULL);
    if (!item) {
        print_at(list + 2, 4, w - list - 4, S_DIM, "%s", T("The program is empty."));
        return;
    }
    int x = list + 3, cw = w - list - 6, ch = h - 7;
    if (item->kind == ITEM_FUNCTION) function_card(app, item, x, 4, cw, ch);
    else if (item->kind == ITEM_COMPONENT || item->kind == ITEM_CLASS) class_card(app, item, x, 4, cw, ch);
    else if (item->kind == ITEM_TEST) test_card(app, item, x, 4, cw, ch);
    else if (item->kind == ITEM_RECORD) record_card(app, item, x, 4, cw, ch);
    else globals_card(app, x, 4, cw, ch);
}

static const Stmt *statement_at(const Stmt *s, int line) {
    for (; s; s = s->next) {
        for (const Stmt *b = s; b; b = b->otherwise) {
            if (b->loc.line == line) return b;
            const Stmt *inner = statement_at(b->body, line);
            if (inner) return inner;
        }
    }
    return NULL;
}

static void draw_item_code(App *app, int x, int y, int w, int h) {
    const Item *item = selected_item(app);
    Explore *e = &app->explore;
    if (e->line < item->first) e->line = item->first;
    if (e->line > item->last) e->line = item->last;
    e->code_top = scroll_to(e->code_top, e->line, item->first, item->last, h);
    Tint tints[8];
    CodeView view = {.doc = app->doc, .first = item->first, .last = item->last, .top = e->code_top,
                     .current = e->line, .current_bg = C_SELECT, .tints = tints};
    if (item->function) view.tint_count = run_tints(app, item->function, tints, 8);
    draw_code(x, y, w, h, &view);
}

/* How a record lies in memory. */
static const char *bytes_text(int n) {
    static char text[32];
    snprintf(text, sizeof text, TN("%d byte", "%d bytes", n), n);
    return text;
}

static void draw_layout(const App *app, const Record *record, int x, int y, int w, int h) {
    const Config *config = &app->doc->config;
    int offset = 0, row = y + 2, size, align;
    Type whole = {.kind = TY_RECORD, .record = (Record *)record};
    size = vm_sizeof(config, &whole, &align);
    print_at(x, y, w, S_DIM, TN("%d byte in all", "%d bytes in all", size), size);
    for (const VarDecl *f = record->fields; f && row < y + h; f = f->next) {
        int a, n = vm_sizeof(config, f->symbol->type, &a), at = (offset + a - 1) / a * a;
        if (at > offset && row < y + h)
            print_at(x, row++, w, S_FAINT, "%4d  " M_FAINT "┄┄ %s", offset, T("padding"));
        int bar = n * 2 > w - 34 ? w - 34 : n * 2;
        if (bar < 1) bar = 1;
        print_at(x, row, 6, S_DIM, "%4d", at);
        fill(x + 6, row, bar, 1, S(231, C_TYPE_BG, 0));
        print_at(x + 8 + bar, row++, w - bar - 8, S_TEXT, "%s " M_DIM "%s", f->name, bytes_text(n));
        offset = at + n;
    }
    if (offset < size && row < y + h)
        print_at(x, row, w, S_FAINT, "%4d  ┄┄ %s", offset, T("padding"));
}

static void first_problem(void *context, Loc loc, bool warning, const char *message) {
    App *app = context;
    if (!warning && !app->problem[0]) snprintf(app->problem, sizeof app->problem, "%s %d: %s", T("line"), loc.line, message);
}

const char *translation_problem(App *app) {
    if (!app->doc || !app->doc->ok || !app->doc->program) return NULL;
    if (app->problem_doc != app->doc || app->problem_target != app->target) {
        Diag diag = {.source = &app->doc->source, .out = stderr, .sink = first_problem, .sink_context = app};
        app->problem[0] = '\0';
        app->problem_doc = app->doc;
        app->problem_target = app->target;
        translatable(app->doc->program, &app->doc->config, app->target, &diag);
    }
    return app->problem[0] ? app->problem : NULL;
}

/* Instead of a translation that would be wrong: why there is none. */
static bool draw_problem(App *app, int x, int y, int w, int h) {
    const char *problem = translation_problem(app);
    if (!problem) return false;
    print_wrapped(x + 1, y + 1, w - 2, h - 4, S(C_FOCUS, C_DEFAULT, 0), "%s", problem);
    print_at(x + 1, y + h - 2, w - 2, S_DIM, M_KEY " T " M_RESET " %s", T("another language"));
    return true;
}

static void draw_translation(App *app, int w, int h) {
    const Item *item = selected_item(app);
    Arena arena = {0};
    StrBuf c = {0};
    LineMap map = {0};
    int half = w / 2;
    strbuf_puts(&c, "");
    draw_box(0, 2, half, h - 4, C_BORDER, T("Pseudocode"));
    draw_item_code(app, 2, 3, half - 4, h - 6);
    if (item->record) {
        /* bytes and offsets are those of C: the other languages lay objects out their own way */
        bool layout = app->target == LANG_C;
        int top = layout ? (h - 4) / 2 : h - 4;
        draw_box(half, 2, w - half, top, C_BORDER, lang_name(app->target));
        if (!draw_problem(app, half + 1, 3, w - half - 3, top - 2)) {
            translate_record(app->doc->program, &app->doc->config, &arena, app->target, item->record, &c);
            draw_c_code(half + 1, 3, w - half - 3, top - 2, c.data, 0, -1, -1);
        }
        if (layout) {
            draw_box(half, 2 + top, w - half, h - 4 - top, C_BORDER, T("In memory"));
            draw_layout(app, item->record, half + 2, 3 + top, w - half - 4, h - 6 - top);
        }
    } else {
        draw_box(half, 2, w - half, h - 4, C_BORDER, lang_name(app->target));
        if (draw_problem(app, half + 1, 3, w - half - 3, h - 6)) {
            strbuf_free(&c);
            arena_free(&arena);
            return;
        }
        translate_function(app->doc->program, &app->doc->config, &arena, app->target, item->function, &c, &map);
        const Stmt *stmt = statement_at(item->function->body, app->explore.line);
        int first = -1, last = -1, total = 0;
        for (size_t i = 0; i < c.length; i++) total += c.data[i] == '\n';
        for (int i = 0; i < map.count; i++) {
            if (map.items[i].stmt != stmt) continue;
            first = last = map.items[i].first;
            if (stmt->kind != ST_IF && stmt->kind != ST_FOR && stmt->kind != ST_WHILE &&
                stmt->kind != ST_REPEAT && stmt->kind != ST_BLOCK)
                last = map.items[i].last;
            if (stmt->kind == ST_REPEAT) first = last = map.items[i].last;
        }
        int rows = h - 6, top = first >= 0 ? scroll_to(0, first, 0, total - 1, rows) : 0;
        draw_c_code(half + 1, 3, w - half - 3, rows, c.data, top, first, last);
    }
    strbuf_free(&c);
    arena_free(&arena);
}

static void draw_errors(App *app, int w, int h) {
    const Document *doc = app->doc;
    Explore *e = &app->explore;
    if (e->selected >= doc->message_count) e->selected = 0;
    int rows = doc->message_count < (h - 6) / 2 ? doc->message_count : (h - 6) / 2;
    draw_box(0, 2, w, rows + 2, C_ERROR, T("To fix before going on"));
    int top = scroll_to(0, e->selected, 0, doc->message_count - 1, rows);
    for (int i = 0; i < rows; i++) {
        const Message *m = &doc->messages[top + i];
        fit_quiet(true); /* a long one is cut here on purpose */
        draw_table_row(1, 3 + i, w - 2, top + i == e->selected, "%s" M_RESET M_DIM " %s %d" M_RESET "  %s",
                       m->warning ? M_FOCUS "⚠" : M_ERROR "✗", T("line"), m->loc.line, m->text);
        fit_quiet(false);
    }
    int y = 4 + rows, ch = h - 4 - rows - 2;
    draw_box(0, y, w, ch, C_BORDER, NULL);
    if (doc->message_count == 0) return;
    const Message *chosen = &doc->messages[e->selected];
    if (text_width(chosen->text) > w - 16 && ch > 6) { /* it did not fit in its line above */
        int used = print_wrapped(2, y + 1, w - 4, 3, S(chosen->warning ? C_FOCUS : C_ERROR, C_DEFAULT, 0), "%s", chosen->text);
        y += used, ch -= used;
    }
    Loc loc = doc->messages[e->selected].loc;
    CodeView view = {.doc = doc, .first = 1, .last = doc->line_count, .current = loc.line, .current_bg = 52};
    view.top = scroll_to(loc.line - ch / 2, loc.line, 1, doc->line_count, ch - 2);
    draw_code(2, y + 1, w - 4, ch - 2, &view);
}

static void where(const App *app, char *out, size_t size) {
    const char *name = strrchr(app->doc->path, '/');
    const Item *item = selected_item(app);
    snprintf(out, size, M_TEXT "%s%s%s", name ? name + 1 : app->doc->path,
             item && app->doc->ok && app->explore.level > 0 ? M_FAINT " ▸ " M_CALL : "",
             item && app->doc->ok && app->explore.level > 0 ? item->function ? item->function->full_name : item->name : "");
}

void explore_draw(App *app) {
    int w = screen_width(), h = screen_height();
    char path[200];
    where(app, path, sizeof path);
    if (!app->doc->ok) {
        draw_header(path, NULL, 0, 0, 0);
        draw_errors(app, w, h);
        const Hint hints[] = {{"↑↓", T("choose")}, {"E", T("edit")},
                              {"Esc", T("files")}, {"?", T("help")}, {"q", T("quit")}};
        draw_footer(hints, 5);
        return;
    }
    const char *levels[] = {T("Map"), T("Code"), lang_name(app->target)};
    draw_header(path, levels, 3, app->explore.level, C_READ);
    const Item *item = selected_item(app);
    if (app->explore.level == 0 || !item) {
        draw_map(app, w, h);
    } else if (app->explore.level == 1) {
        draw_box(0, 2, w, h - 4, C_BORDER, item->kind == ITEM_GLOBALS ? T("global variables") : item->function ? item->function->full_name : item->name);
        draw_item_code(app, 2, 3, w - 4, h - 6);
    } else {
        draw_translation(app, w, h);
    }
    bool tests = app->doc->ok && has_tests(app->doc->program);
    const Hint hints[] = {{"↑↓", T("move")},
                          {T("Enter"), T("more detail")},
                          {"Esc", T("less detail")},
                          {"r", T("run")},
                          {tests ? "t" : "E", tests ? T("tests") : T("edit")},
                          {tests ? "E" : "q", tests ? T("edit") : T("quit")},
                          {"?", T("help")}};
    draw_footer(hints, 7);
}

static void run_selected(App *app) {
    const Item *item = selected_item(app);
    if (!item || !app->doc->ok) return;
    bool main_item = app->doc->main_item >= 0 && item == &app->doc->items[app->doc->main_item];
    if (item->kind == ITEM_TEST) {
        run_open_test(app, item->function);
    } else if (main_item || item->kind != ITEM_FUNCTION) {
        if (app->doc->main_item < 0 && !has_components(app->doc->program))
            snprintf(app->notice, sizeof app->notice, "%s",
                     T("There is no main: choose a subprogram to try"));
        else
            run_open(app, NULL, NULL, 0);
    } else if (item->function->symbol->param_count == 0) {
        run_open(app, item->function, NULL, 0);
    } else {
        trial_open(app, item->function);
    }
}

void explore_key(App *app, int key) {
    Explore *e = &app->explore;
    const Item *item = selected_item(app);
    int count = app->doc->ok ? app->doc->item_count : app->doc->message_count;
    bool lines = app->doc->ok && e->level > 0;
    int *at = lines ? &e->line : &e->selected;
    int lo = lines && item ? item->first : 0, hi = lines && item ? item->last : count - 1;
    switch (key) {
        case K_UP: case 'k': (*at)--; break;
        case K_DOWN: case 'j': (*at)++; break;
        case K_PAGE_UP: *at -= 10; break;
        case K_PAGE_DOWN: *at += 10; break;
        case K_HOME: *at = lo; break;
        case K_END: *at = hi; break;
        case K_ENTER: case '+': case K_RIGHT:
            if (!app->doc->ok || !item) return;
            if (e->level == 0) e->line = e->code_top = item->first;
            if (e->level < (item->kind == ITEM_GLOBALS || item->kind == ITEM_TEST || item->kind == ITEM_COMPONENT ||
                            (item->function && item->function->owner && item->function->owner->is_component) ? 1 : 2))
                e->level++;
            return;
        case K_ESC: case '-': case K_LEFT: case K_BACKSPACE:
            if (e->level > 0) e->level--;
            else if (key != K_LEFT) {
                char *path = strdup(app->doc->path);
                files_back(app, path);
                free(path);
            }
            return;
        case 'r': run_selected(app); return;
        case 't':
            if (app->doc->ok && has_tests(app->doc->program)) tests_open(app);
            else snprintf(app->notice, sizeof app->notice, "%s", T("This program has no tests"));
            return;
        case 'q': app->quit = true; return;
        default: return;
    }
    if (*at > hi) *at = hi;
    if (*at < lo) *at = lo;
}
