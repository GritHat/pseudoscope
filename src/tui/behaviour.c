#include "ui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Other UML diagrams that live while the program runs: timing, communication,
 * objects, activity. */

static bool same_place(Pointer a, Pointer b) { return a.object == b.object && a.offset == b.offset && a.serial == b.serial; }

static const short STATE_COLORS[] = {31, 97, 28, 130, 25, 125, 64, 60};
#define STATE_COLOR_COUNT ((int)(sizeof STATE_COLORS / sizeof STATE_COLORS[0]))

/* ---- timing: which state, when ---- */

void draw_timing(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    long now = vm_time(vm);
    int label = 14, room = w - label - 2;
    long span = now < room ? room : now + 1, from = 0;
    long per = span > room ? (span + room - 1) / room : 1; /* time of one character */
    int scale = now > 0 && now * 3 < room ? 3 : now > 0 && now * 2 < room ? 2 : 1;
    int row = y + 2;
    (void)span;
    long tick = 5 * per;
    while (tick * scale / per < 8) tick += 5 * per;
    for (long t = from; t * scale / per < room; t += tick) {
        int c = (int)(t * scale / per);
        print_at(x + label + c, y, 8, S_FAINT, "%ld", t);
        put_text(x + label + c, y + 1, S_FAINT, "╷");
    }
    int count = vm_instance_count(vm);
    for (int i = 0; i < count && row + 1 < y + h; i++) {
        const Class *klass;
        Pointer at = vm_instance(vm, i, &klass);
        if (!klass->states) continue;
        char name[40];
        vm_place_name(vm, at, name, sizeof name);
        print_at(x, row, label - 1, S(C_TEXT, C_DEFAULT, A_BOLD), "%s", name);
        /* the changes, in order */
        const char *state = klass->states->name;
        long since = 0;
        int rows = vm_message_count(vm);
        for (int k = 0; k <= rows; k++) {
            const SequenceRow *m = k < rows ? vm_message_at(vm, k) : NULL;
            bool change = m && m->kind == MSG_STATE && m->to > 0 && same_place(vm_life(vm, m->to)->at, at);
            if (m && !change) continue;
            long until = m ? m->time : now + 1;
            int a = (int)(since * scale / per), b = (int)(until * scale / per), index = 0;
            for (const StateName *s = klass->states; s && strcmp(s->name, state) != 0; s = s->next) index++;
            if (b > room) b = room;
            if (b > a) {
                Style st = S(231, STATE_COLORS[index % STATE_COLOR_COUNT], A_BOLD);
                fill(x + label + a, row, b - a, 1, st);
                print_at(x + label + a, row, b - a, st, "%s", b - a >= text_width(state) ? state : "");
            }
            if (m) {
                state = m->name;
                since = m->time;
            }
        }
        row += 2;
    }
    int edge = (int)((now + 1) * scale / per);
    if (edge < room)
        for (int k = y + 1; k < row && k < y + h; k++) put_text(x + label + edge, k, S(C_FOCUS, C_DEFAULT, 0), "│");
    /* what the colours mean */
    row++;
    for (const Class *c = app->doc->program->classes; c && row < y + h; c = c->next) {
        if (!c->is_component || !c->states) continue;
        int cx = x, index = 0;
        cx += print_at(cx, row, label, S_DIM, "%s", c->name) + 2;
        for (const StateName *s = c->states; s && cx < x + w - 12; s = s->next, index++)
            cx += print_at(cx, row, -1, S(231, STATE_COLORS[index % STATE_COLOR_COUNT], 0), " %s ", s->name) + 1;
        row++;
    }
}

/* ---- communication: who said what, numbered ---- */

void draw_communication(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    struct {
        int a, b, count, last;
        char text[200];
    } pairs[40];
    int n = 0, number = 0, total = vm_message_count(vm), newest = -1;
    for (int i = 0; i < total; i++) {
        const SequenceRow *m = vm_message_at(vm, i);
        if (m->kind != MSG_DELIVER && m->kind != MSG_CALL && m->kind != MSG_LOST) continue;
        if (m->from == m->to) continue;
        number++;
        int k = 0;
        while (k < n && (pairs[k].a != m->from || pairs[k].b != m->to)) k++;
        if (k == 40) continue;
        if (k == n) {
            memset(&pairs[n], 0, sizeof pairs[n]);
            pairs[n].a = m->from;
            pairs[n++].b = m->to;
        }
        char one[60];
        snprintf(one, sizeof one, "%d:%s%s", number, m->name, m->kind == MSG_LOST ? "✗" : "");
        size_t used = strlen(pairs[k].text);
        if (used > 90) { /* only the latest */
            memmove(pairs[k].text, pairs[k].text + used - 60, 61);
            char *blank = strchr(pairs[k].text, ' ');
            if (blank) memmove(pairs[k].text, blank, strlen(blank) + 1);
            used = strlen(pairs[k].text);
        }
        snprintf(pairs[k].text + used, sizeof pairs[k].text - used, "%s%s", used ? "  " : "", one);
        pairs[k].count++;
        newest = k;
    }
    if (n == 0) print_at(x, y + 1, w, S_DIM, "%s", L("Ancora nessuno ha parlato con nessuno.", "Nobody has talked to anybody yet."));
    for (int k = 0; k < n && y + k * 3 + 1 < y + h; k++) {
        int row = y + k * 3 + 1, left = x + 18, right = x + w - 20;
        bool hot = k == newest;
        Style st = hot ? S(C_READ, C_DEFAULT, A_BOLD) : S_DIM;
        print_at(x, row, 16, S(C_TEXT, C_DEFAULT, A_BOLD), "%16.16s", vm_life(vm, pairs[k].a)->name);
        hline(left, row, right - left, st, "─");
        put_text(right, row, st, "▶");
        print_at(right + 2, row, 17, S(C_TEXT, C_DEFAULT, A_BOLD), "%s", vm_life(vm, pairs[k].b)->name);
        int width = text_width(pairs[k].text), room = right - left - 2;
        const char *text = pairs[k].text;
        if (width > room) text += width - room;
        print_at(left + 1, row - 1, room, hot ? S_BOLD : S_DIM, "%s", text);
    }
}

/* ---- objects ---- */

void draw_objects(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int per = w / 34 < 1 ? 1 : w / 34, width = w / per, tops[8] = {0}, drawn = 0;
    if (per > 8) per = 8;
    int total = vm_life_count(vm);
    for (int i = 1; i < total; i++) {
        const Life *l = vm_life(vm, i);
        if (!l->klass || !vm_cell(vm, l->at)) continue; /* gone */
        int rows = 0;
        for (const VarDecl *f = l->klass->record->fields; f; f = f->next) rows++;
        int best = 0;
        for (int k = 1; k < per; k++)
            if (tops[k] < tops[best]) best = k;
        int cx = x + best * width, cy = y + tops[best], height = rows + 2 < 8 ? rows + 2 : 8;
        tops[best] += height + 1;
        if (cy + height > y + h) continue;
        bool acting = i == vm_current_life(vm);
        draw_box(cx, cy, width - 2, height, acting ? C_CALL : C_TYPE, NULL);
        print_at(cx + 2, cy, width - 6, S(acting ? C_CALL : C_TEXT, C_DEFAULT, A_BOLD | A_UNDER), " %s: %s ", l->name, l->klass->name);
        drawn++;
        int row = cy + 1;
        for (const VarDecl *f = l->klass->record->fields; f && row < cy + height - 1; f = f->next, row++) {
            const Type *type = f->symbol->type;
            Pointer p = l->at;
            p.offset += f->symbol->slot;
            char text[100] = "";
            if (type->kind == TY_ARRAY) {
                int n = vm_slots(&app->doc->config, type);
                for (int k = 0; k < n && k < 14; k++) {
                    Pointer q = p;
                    q.offset += k;
                    const Value *v = vm_cell(vm, q);
                    char one[24] = "?";
                    if (v) vm_format(vm, v, vm_slot_type(&app->doc->config, type, k, NULL), one, sizeof one);
                    size_t used = strlen(text);
                    snprintf(text + used, sizeof text - used, "%s%s", k ? " " : "", one);
                }
            } else {
                const Value *v = vm_cell(vm, p);
                if (v && v->kind == V_POINTER && v->known && v->as.p.object) {
                    char name[40];
                    vm_place_name(vm, v->as.p, name, sizeof name);
                    snprintf(text, sizeof text, M_POINTER "──▶ %s", name);
                } else if (v) {
                    vm_format(vm, v, type, text, sizeof text);
                }
            }
            int touches;
            const Touch *touched = vm_touched(vm, &touches);
            bool written = false;
            for (int k = 0; k < touches; k++)
                written |= touched[k].write && touched[k].at.object == p.object && touched[k].at.offset == p.offset;
            int used = print_at(cx + 2, row, width - 6, S_DIM, "%s ", f->name);
            print_at(cx + 2 + used, row, width - used - 6, written ? S(231, C_WRITE_BG, A_BOLD) : S_TEXT, written ? " %s " : "%s", text);
        }
    }
    if (!drawn) print_at(x, y + 1, w, S_DIM, "%s", L("Nessun oggetto, per ora.", "No objects, for now."));
}

/* ---- activity: the subprogram as a flow ---- */

typedef struct {
    char text[90];
    char edge[16];
    int indent;
    char shape; /* a action, d decision, s start, e end, b back, l label */
    const Stmt *stmt;
    int phase;  /* of a for: -1 any */
} Flow;

typedef struct {
    Flow *items;
    int count, max;
} FlowList;

static void flow(FlowList *list, int indent, char shape, const char *edge, const Stmt *stmt, const char *format, const char *text) {
    if (list->count == list->max) return;
    Flow *f = &list->items[list->count++];
    memset(f, 0, sizeof *f);
    f->indent = indent;
    f->shape = shape;
    f->stmt = stmt;
    f->phase = -1;
    snprintf(f->edge, sizeof f->edge, "%s", edge);
    snprintf(f->text, sizeof f->text, format, text);
}

static void expr_words(const Expr *e, char *out, size_t size) {
    StrBuf text = {0};
    strbuf_puts(&text, "");
    if (e) print_expr(NULL, e, NULL, 0, "", "", "", "", &text);
    snprintf(out, size, "%s", text.data);
    strbuf_free(&text);
}

static void flow_statements(FlowList *list, const Stmt *s, int indent, const char *edge) {
    char a[200], b[200], both[90];
    if (!s) flow(list, indent, 'l', edge, NULL, "%s", "");
    for (; s; s = s->next, edge = "") {
        switch (s->kind) {
            case ST_IF:
                for (const Stmt *k = s; k; k = k->otherwise) {
                    if (k->kind != ST_IF) {
                        flow_statements(list, k->body, indent + 1, L("no", "no"));
                        break;
                    }
                    expr_words(k->value, a, sizeof a);
                    flow(list, indent, 'd', k == s ? edge : L("no", "no"), k, "%s ?", a);
                    flow_statements(list, k->body, indent + 1, L("sì", "yes"));
                }
                break;
            case ST_WHILE:
                expr_words(s->value, a, sizeof a);
                flow(list, indent, 'd', edge, s, "%s ?", a);
                flow_statements(list, s->body, indent + 1, L("sì", "yes"));
                flow(list, indent + 1, 'b', "", NULL, "%s", L("↺ di nuovo alla domanda", "↺ back to the question"));
                break;
            case ST_FOR:
                expr_words(s->value, a, sizeof a);
                snprintf(both, sizeof both, "%.30s := %.40s", s->target->text, a);
                flow(list, indent, 'a', edge, s, "%s", both);
                list->items[list->count - 1].phase = PHASE_WHOLE;
                expr_words(s->limit, b, sizeof b);
                snprintf(both, sizeof both, "%.30s <= %.40s ?", s->target->text, b);
                flow(list, indent, 'd', "", s, "%s", both);
                list->items[list->count - 1].phase = PHASE_CONDITION;
                flow_statements(list, s->body, indent + 1, L("sì", "yes"));
                if (s->step) expr_words(s->step, a, sizeof a);
                else snprintf(a, sizeof a, "1");
                snprintf(both, sizeof both, "%.22s := %.22s + %.20s  ↺", s->target->text, s->target->text, a);
                flow(list, indent + 1, 'a', "", s, "%s", both);
                list->items[list->count - 1].phase = PHASE_STEP;
                break;
            case ST_REPEAT:
                flow(list, indent, 'l', edge, NULL, "%s", "◇");
                flow_statements(list, s->body, indent + 1, "");
                expr_words(s->value, a, sizeof a);
                flow(list, indent, 'd', "", s, "%s ?", a);
                flow(list, indent + 1, 'b', L("no", "no"), NULL, "%s", L("↺ da capo", "↺ again"));
                break;
            case ST_BLOCK: flow_statements(list, s->body, indent, edge); break;
            case ST_VAR: break;
            case ST_ASSIGN:
                expr_words(s->target, a, sizeof a);
                expr_words(s->value, b, sizeof b);
                if (s->name && strcmp(s->name, "become") == 0) snprintf(both, sizeof both, "◆ %.60s", b);
                else snprintf(both, sizeof both, "%.38s := %.46s", a, b);
                flow(list, indent, 'a', edge, s, "%s", both);
                break;
            case ST_RETURN:
                expr_words(s->value, a, sizeof a);
                flow(list, indent, 'e', edge, s, s->value ? L("◉ restituisce %s", "◉ returns %s") : "◉%s", a);
                break;
            case ST_PRINT: case ST_READ:
                a[0] = '\0';
                for (int i = 0; i < s->args.count; i++) {
                    expr_words(s->args.items[i], b, sizeof b);
                    size_t n = strlen(a);
                    snprintf(a + n, sizeof a - n, "%s%.60s", i ? ", " : "", b);
                }
                flow(list, indent, 'a', edge, s, s->kind == ST_PRINT ? L("≡ stampa %s", "≡ print %s") : L("» legge %s", "» read %s"), a);
                break;
            case ST_SEND: flow(list, indent, 'a', edge, s, L("▷ manda %s", "▷ send %s"), s->name); break;
            case ST_TODO: flow(list, indent, 'a', edge, s, "%s", "..."); break;
            case ST_STEP: flow(list, indent, 'l', edge, s, "● %s", s->text); break;
            default:
                expr_words(s->value, a, sizeof a);
                flow(list, indent, 'a', edge, s, "%s", a);
        }
    }
}

void draw_activity(App *app, const Function *function, int x, int y, int w, int h) {
    static Flow items[1500];
    const Vm *vm = app->run.vm;
    FlowList list = {items, 0, 1500};
    if (!function) return;
    flow(&list, 0, 's', "", NULL, "%s", "●");
    flow_statements(&list, function->body, 0, "");
    flow(&list, 0, 'e', "", NULL, "%s", "◉");
    const Frame *frame = vm_frame(vm, vm_frame_count(vm) - 1);
    int current = -1;
    for (int i = 0; i < list.count; i++)
        if (frame->function == function && items[i].stmt && items[i].stmt == frame->stmt &&
            (items[i].phase < 0 || items[i].phase == frame->phase))
            current = i;
    int top = current > h / 2 ? current - h / 2 : 0;
    if (top > list.count - h) top = list.count - h;
    if (top < 0) top = 0;
    for (int i = top; i < list.count && i - top < h; i++) {
        const Flow *f = &items[i];
        int row = y + i - top, cx = x + f->indent * 3;
        bool here = i == current;
        for (int k = 0; k < f->indent; k++) put_text(x + k * 3 + 1, row, S_FAINT, "│");
        if (f->edge[0]) {
            put_text(cx - 2, row, S_FAINT, "├");
            cx += print_at(cx - 1, row, -1, S(C_WRITE, C_DEFAULT, 0), "%s▶ ", f->edge) - 1;
        }
        Style st = here ? S(16, C_FOCUS_BG, A_BOLD) : f->shape == 'd' ? S(C_FOCUS, C_DEFAULT, 0) : f->shape == 'b' ? S_DIM
                 : f->shape == 'a' ? S(C_TEXT, C_CELL, 0) : S(C_CALL, C_DEFAULT, A_BOLD);
        print_at(cx, row, x + w - cx, st, f->shape == 'd' ? " ◇ %s " : f->shape == 'l' || f->shape == 'b' || f->shape == 's' ? "%s" : " %s ", f->text);
    }
    draw_scrollbar(x + w, y, h, top, h, list.count);
}
