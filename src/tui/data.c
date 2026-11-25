#include "ui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The variables of the running subprogram, drawn as what they are: numbers as
 * chips, arrays as rows of cells, matrices as grids, linked nodes as chains. */

static const short TINTS[] = {209, 213, 221, 117, 150, 183, 180, 111};
#define TINT_COUNT ((int)(sizeof TINTS / sizeof TINTS[0]))

/* ---- index variables ---- */

typedef struct {
    Tint *items;
    int count, max;
} TintList;

static void note_index(void *context, const Expr *e) {
    TintList *list = context;
    if ((e->kind != EX_INDEX && e->kind != EX_APPLY) || e->is_call || !e->base->type ||
        e->base->type->kind != TY_ARRAY)
        return;
    for (int i = 0; i < e->args.count; i++) {
        const Expr *index = e->args.items[i];
        /* i, i + 1, i - 1 */
        if (index->kind == EX_BINARY && index->right->kind == EX_INT) index = index->base;
        if (index->kind != EX_NAME || !index->symbol || index->is_call) continue;
        bool known = false;
        for (int k = 0; k < list->count; k++) known |= list->items[k].symbol == index->symbol;
        if (!known && list->count < list->max) {
            list->items[list->count] = (Tint){index->symbol, TINTS[list->count % TINT_COUNT]};
            list->count++;
        }
    }
}

int run_tints(App *app, const Function *function, Tint *out, int max) {
    (void)app;
    TintList list = {out, 0, max};
    visit_statements(function->body, note_index, &list);
    return list.count;
}

/* Which variables index this dimension of this array, anywhere in the body. */
typedef struct {
    const Symbol *array;
    int dim;
    const Symbol *symbols[6];
    int count;
} IndexUse;

static void note_use(void *context, const Expr *e) {
    IndexUse *use = context;
    if ((e->kind != EX_INDEX && e->kind != EX_APPLY) || e->is_call) return;
    int dim = use->dim;
    const Expr *base = e->base;
    while ((base->kind == EX_INDEX || base->kind == EX_APPLY) && !base->is_call) { /* a[i][j] */
        dim -= base->args.count;
        base = base->base;
    }
    if (base->kind != EX_NAME || base->symbol != use->array || dim < 0 || dim >= e->args.count) return;
    const Expr *index = e->args.items[dim];
    if (index->kind == EX_BINARY && index->right->kind == EX_INT) index = index->base;
    if (index->kind != EX_NAME || !index->symbol || index->is_call) return;
    for (int i = 0; i < use->count; i++)
        if (use->symbols[i] == index->symbol) return;
    if (use->count < 6) use->symbols[use->count++] = index->symbol;
}

typedef struct {
    const char *name;
    long at;
    short color;
} Marker;

static int markers(App *app, int frame, const Symbol *array, int dim, Marker *out) {
    const Vm *vm = app->run.vm;
    const Frame *f = vm_frame(vm, frame);
    if (!f->function) return 0;
    IndexUse use = {.array = array, .dim = dim};
    visit_statements(f->function->body, note_use, &use);
    Tint tints[8];
    int tint_count = run_tints(app, f->function, tints, 8), n = 0;
    for (int i = 0; i < use.count; i++) {
        const Var *var = vm_find_var(vm, frame, use.symbols[i]);
        const Value *v = var ? vm_cell(vm, var->at) : NULL;
        if (!v || !v->known || v->kind != V_INT) continue;
        short color = C_POINTER;
        for (int k = 0; k < tint_count; k++)
            if (tints[k].symbol == use.symbols[i]) color = tints[k].color;
        out[n++] = (Marker){use.symbols[i]->name, v->as.i, color};
    }
    return n;
}

/* ---- cells ---- */

enum { STATE_IDLE, STATE_READ, STATE_WRITTEN };

static int cell_state(const Vm *vm, Pointer at) {
    int count, state = STATE_IDLE;
    const Touch *touched = vm_touched(vm, &count);
    for (int i = 0; i < count; i++) {
        if (touched[i].at.object != at.object || touched[i].at.offset != at.offset ||
            touched[i].at.serial != at.serial)
            continue;
        if (touched[i].write) return STATE_WRITTEN;
        state = STATE_READ;
    }
    return state;
}

static Style cell_style(const Vm *vm, Pointer at, const Value *v) {
    switch (cell_state(vm, at)) {
        case STATE_WRITTEN: return S(231, C_WRITE_BG, A_BOLD);
        case STATE_READ: return S(231, C_READ_BG, A_BOLD);
        default: return v && v->known ? S(C_TEXT, C_CELL, 0) : S(C_FAINT, C_PANEL, 0);
    }
}

static void cell_text(const Vm *vm, const Value *v, const Type *type, char *out, size_t size) {
    if (!v || !v->known) {
        snprintf(out, size, "·");
    } else if (v->kind == V_POINTER) {
        if (v->as.p.object == 0) snprintf(out, size, "∅");
        else {
            char name[40];
            vm_place_name(vm, v->as.p, name, sizeof name);
            snprintf(out, size, "→%s", name);
        }
    } else {
        vm_format(vm, v, type, out, size);
    }
}

static Pointer moved(Pointer p, int slots) {
    p.offset += slots;
    return p;
}

/* ---- blocks ---- */

typedef enum { BLOCK_SCALARS, BLOCK_ROW, BLOCK_GRID, BLOCK_TABLE, BLOCK_RECORD, BLOCK_CHAIN, BLOCK_TITLE } BlockKind;

typedef struct {
    BlockKind kind;
    int frame;
    const Var *vars[32];
    int count;
    int height;
    const char *title;
} Block;

static bool is_scalar_type(const Type *t) { return t->kind != TY_ARRAY && t->kind != TY_RECORD; }

/* How many fields of the record point to the same record. */
static int self_links(const Record *record, const VarDecl **first, const VarDecl **second) {
    int n = 0;
    for (const VarDecl *f = record->fields; f; f = f->next) {
        const Type *t = f->symbol->type;
        if (t->kind == TY_POINTER && t->base->kind == TY_RECORD && t->base->record == record) {
            if (n == 0) *first = f;
            if (n == 1) *second = f;
            n++;
        }
    }
    return n;
}

static int links_of(const Type *type) {
    const VarDecl *a = NULL, *b = NULL;
    if (type->kind != TY_POINTER || type->base->kind != TY_RECORD) return 0;
    return self_links(type->base->record, &a, &b);
}

static long extent(const Vm *vm, const Var *var, int dim) {
    const Bounds *b = vm_bounds(vm, var, dim);
    long n = b->hi - b->lo + 1;
    return n < 0 ? 0 : n;
}

static int tree_rows(const Vm *vm, Pointer p, const VarDecl *a, const VarDecl *b, int depth) {
    if (p.object == 0 || !vm_cell(vm, p) || depth > 5) return 0;
    const Value *l = vm_cell(vm, moved(p, a->symbol->slot)), *r = vm_cell(vm, moved(p, b->symbol->slot));
    int n = 1;
    if (l && l->known && l->kind == V_POINTER) n += tree_rows(vm, l->as.p, a, b, depth + 1);
    if (r && r->known && r->kind == V_POINTER) n += tree_rows(vm, r->as.p, a, b, depth + 1);
    return n;
}

static void measure(App *app, Block *block, int w) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    switch (block->kind) {
        case BLOCK_TITLE: block->height = 1; break;
        case BLOCK_SCALARS: {
            int x = 0, rows = 1;
            for (int i = 0; i < block->count; i++) {
                char text[64];
                cell_text(vm, vm_cell(vm, block->vars[i]->at), block->vars[i]->symbol->type, text, sizeof text);
                int need = text_width(block->vars[i]->symbol->name) + text_width(text) + 6;
                if (x > 0 && x + need > w) {
                    rows++;
                    x = 0;
                }
                x += need;
            }
            block->height = rows * 2;
            break;
        }
        case BLOCK_ROW: {
            Marker m[6];
            int n = markers(app, block->frame, var->symbol, 0, m);
            block->height = 1 + (n > 3 ? 3 : n > 0 ? n : 0) + (app->run.bars ? 6 : 1) + 1 + 1;
            break;
        }
        case BLOCK_GRID: {
            long rows = extent(vm, var, 0);
            block->height = 3 + (int)(rows > 12 ? 12 : rows) + 1;
            break;
        }
        case BLOCK_TABLE: {
            long rows = extent(vm, var, 0);
            block->height = 1 + (int)(rows > 8 ? 8 : rows) + 1;
            break;
        }
        case BLOCK_RECORD: {
            int slots = vm_slots(&app->doc->config, var->symbol->type);
            block->height = 1 + (slots > 8 ? 8 : slots) + 1;
            break;
        }
        case BLOCK_CHAIN: {
            const VarDecl *a = NULL, *b = NULL;
            const Value *v = vm_cell(vm, var->at);
            int n = self_links(var->symbol->type->base->record, &a, &b);
            int rows = 1;
            if (n >= 2 && v && v->known) rows = tree_rows(vm, v->as.p, a, b, 0);
            block->height = (rows < 1 ? 1 : rows > 12 ? 12 : rows) + 2;
            break;
        }
    }
}

static int collect(App *app, int frame, const Symbol *const *only, int only_count, Block *blocks, int max, int w) {
    const Vm *vm = app->run.vm;
    bool trial_end = frame == 0 && vm_trial_count(vm) > 0 && vm_frame_count(vm) == 1;
    int count = trial_end ? vm_trial_count(vm) : vm_frame_vars(vm, frame), n = 0;
    Block scalars = {.kind = BLOCK_SCALARS, .frame = frame};
    Block others[40];
    int other_count = 0;
    for (int i = 0; i < count; i++) {
        const Var *var = trial_end ? vm_trial_var(vm, i) : vm_var(vm, frame, i);
        if (!var->live || !var->symbol) continue;
        size_t named = strlen(var->symbol->name); /* the names of ours, made up to run what has none: not the writer's */
        if (app->doc->shown && named && var->symbol->name[named - 1] == '_') continue;
        if (only) {
            bool wanted = false;
            for (int k = 0; k < only_count; k++) wanted |= only[k] == var->symbol;
            if (!wanted) continue;
        }
        const Type *type = var->symbol->type;
        if (type->kind == TY_ARRAY && var->rank == 0) continue; /* on the way in: no bounds yet */
        Block block = {.frame = frame, .vars = {var}, .count = 1};
        if (is_scalar_type(type)) {
            if (links_of(type) == 0) {
                if (scalars.count < 32) scalars.vars[scalars.count++] = var;
                continue;
            }
            block.kind = BLOCK_CHAIN;
        } else if (type->kind == TY_RECORD) {
            block.kind = BLOCK_RECORD;
        } else if (!is_scalar_type(type->base)) {
            block.kind = BLOCK_TABLE;
        } else {
            block.kind = var->rank == 1 ? BLOCK_ROW : BLOCK_GRID;
        }
        if (other_count < 40) others[other_count++] = block;
    }
    if (scalars.count && n < max) blocks[n++] = scalars;
    for (int i = 0; i < other_count && n < max; i++) blocks[n++] = others[i];
    for (int i = 0; i < n; i++) measure(app, &blocks[i], w);
    return n;
}

/* ---- drawing ---- */

static void draw_title(const App *app, const Var *var, int x, int y, int w, bool selected) {
    char type[120] = "";
    describe_type(app->doc, var->symbol->type, type, sizeof type);
    const char *role = var->symbol->kind == SYM_RESULT ? T("  result") : "";
    print_at(x, y, w, S(C_BRIGHT, C_DEFAULT, A_BOLD), "%s%s" M_RESET M_FAINT "  %s" M_WRITE "%s",
             selected ? M_FOCUS "▸ " M_RESET M_BOLD : "", var->symbol->name, type, role);
}

static void draw_scalars(App *app, const Block *block, int x, int y, int w, int bottom) {
    const Vm *vm = app->run.vm;
    Tint tints[8];
    const Frame *frame = vm_frame(vm, block->frame);
    int tint_count = frame->function ? run_tints(app, frame->function, tints, 8) : 0;
    int cx = x, cy = y;
    for (int i = 0; i < block->count; i++) {
        const Var *var = block->vars[i];
        const Value *v = vm_cell(vm, var->at);
        char text[64];
        cell_text(vm, v, var->symbol->type, text, sizeof text);
        bool result = var->symbol->kind == SYM_RESULT;
        int need = text_width(var->symbol->name) + text_width(text) + 6;
        if (cx > x && cx + need > x + w) {
            cx = x;
            cy += 2;
        }
        if (cy >= bottom) return;
        short color = result ? C_WRITE : C_TEXT;
        for (int k = 0; k < tint_count; k++)
            if (tints[k].symbol == var->symbol) color = tints[k].color;
        int at = cx + print_at(cx, cy, w, S(color, C_DEFAULT, A_BOLD), "%s%s", result ? "⇒ " : "", var->symbol->name) + 1;
        print_at(at, cy, w, cell_style(vm, var->at, v), " %s ", text);
        cx += need + (result ? 2 : 0);
    }
}

/* The first index to show so that the markers (or what was touched) are visible. */
static int window_start(const Vm *vm, const Var *var, int per, long lo, long n, int fit, const Marker *m, int count) {
    if (n <= fit) return 0;
    long from = LONG_MAX, to = LONG_MIN;
    int touches;
    const Touch *touched = vm_touched(vm, &touches);
    for (int i = 0; i < touches; i++) {
        if (touched[i].at.object != var->at.object || touched[i].at.serial != var->at.serial) continue;
        long at = (touched[i].at.offset - var->at.offset) / per;
        if (at < 0 || at >= n) continue;
        if (at < from) from = at;
        if (at > to) to = at;
    }
    for (int i = 0; i < count && from == LONG_MAX; i++) {
        long at = m[i].at - lo;
        if (at >= 0 && at < n) from = to = at;
    }
    if (from == LONG_MAX) return 0;
    long start = (from + to) / 2 - fit / 2;
    if (start > n - fit) start = n - fit;
    return start < 0 ? 0 : (int)start;
}

static void draw_row(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    const Type *base = var->symbol->type->base;
    const Bounds *b = vm_bounds(vm, var, 0);
    long n = extent(vm, var, 0);
    Marker m[6];
    int count = markers(app, block->frame, var->symbol, 0, m), shown = count > 3 ? 3 : count;

    int cw = 4;
    double top = 0;
    for (long i = 0; i < n && i < 400; i++) {
        const Value *v = vm_cell(vm, moved(var->at, (int)i));
        char text[64];
        cell_text(vm, v, base, text, sizeof text);
        int need = text_width(text) + 2;
        if (need > cw) cw = need;
        double d = !v || !v->known ? 0 : v->kind == V_INT ? v->as.i : v->kind == V_FLOAT ? v->as.f : 0;
        if (d > top) top = d;
    }
    if (cw > 14) cw = 14;
    int fit = (w - 2) / (cw + 1);
    if (fit < 1) fit = 1;
    int start = window_start(vm, var, 1, b->lo, n, fit, m, count);
    int end = start + fit < n ? start + fit : (int)n;

    if (y < bottom) draw_title(app, var, x, y, w, selected);
    int row = y + 1;
    for (int k = 0; k < shown; k++) {
        long at = m[k].at - b->lo - start;
        if (row + k >= bottom) break;
        if (at >= 0 && at < end - start)
            print_at(x + 1 + (int)at * (cw + 1) + cw / 2 - 1, row + k, -1, S(m[k].color, C_DEFAULT, A_BOLD), "▼ %s", m[k].name);
        else
            print_at(x + w - 14, row + k, 14, S(C_FAINT, C_DEFAULT, 0), "%s = %ld", m[k].name, m[k].at);
    }
    row += shown;
    int bars = app->run.bars ? 6 : 1;
    for (int i = start; i < end; i++) {
        Pointer at = moved(var->at, i);
        const Value *v = vm_cell(vm, at);
        Style st = cell_style(vm, at, v);
        int cx = x + 1 + (i - start) * (cw + 1);
        char text[64];
        cell_text(vm, v, base, text, sizeof text);
        if (app->run.bars) {
            static const char *const EIGHTHS[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            double d = !v || !v->known ? 0 : v->kind == V_INT ? v->as.i : v->kind == V_FLOAT ? v->as.f : 0;
            int units = top > 0 && d > 0 ? (int)(d / top * 40 + 0.5) : 0;
            int state = cell_state(vm, at);
            short color = state == STATE_WRITTEN ? C_WRITE : state == STATE_READ ? C_READ : C_DIM;
            for (int r = 0; r < 5; r++) {
                int part = units - (4 - r) * 8;
                part = part < 0 ? 0 : part > 8 ? 8 : part;
                if (row + r < bottom) hline(cx, row + r, cw, S(color, C_DEFAULT, 0), EIGHTHS[part]);
            }
            if (row + 5 < bottom) print_centered(cx, row + 5, cw, st, "%s", text);
        } else if (row < bottom) {
            fill(cx, row, cw, 1, st);
            print_centered(cx, row, cw, st, "%s", text);
        }
        if (row + bars < bottom) {
            short color = C_FAINT;
            for (int k = 0; k < shown; k++)
                if (m[k].at - b->lo == i) color = m[k].color;
            print_centered(cx, row + bars, cw, S(color, C_DEFAULT, 0), "%ld", b->lo + i);
        }
    }
    if (start > 0 && row < bottom) put_text(x, row, S_DIM, "‹");
    if (end < n && row < bottom) put_text(x + 1 + (end - start) * (cw + 1) - 1, row, S_DIM, "›");
}

static void draw_grid(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    const Type *base = var->symbol->type->base;
    int rank = var->rank;
    long rows = extent(vm, var, rank - 2), cols = extent(vm, var, rank - 1);
    const Bounds *br = vm_bounds(vm, var, rank - 2), *bc = vm_bounds(vm, var, rank - 1);
    Marker mr[6], mc[6];
    int nr = markers(app, block->frame, var->symbol, rank - 2, mr), nc = markers(app, block->frame, var->symbol, rank - 1, mc);
    int cw = 4;
    for (long i = 0; i < rows * cols && i < 400; i++) {
        char text[64];
        cell_text(vm, vm_cell(vm, moved(var->at, (int)i)), base, text, sizeof text);
        if (text_width(text) + 2 > cw) cw = text_width(text) + 2;
    }
    if (cw > 14) cw = 14;
    int left = 9, fit = (w - left) / (cw + 1), tall = block->height - 4;
    if (fit < 1) fit = 1;
    /* follow the index variables */
    long row0 = 0, col0 = 0;
    if (rows > tall && nr) row0 = mr[0].at - br->lo - tall / 2;
    if (cols > fit && nc) col0 = mc[0].at - bc->lo - fit / 2;
    if (row0 > rows - tall) row0 = rows - tall;
    if (col0 > cols - fit) col0 = cols - fit;
    if (row0 < 0) row0 = 0;
    if (col0 < 0) col0 = 0;

    if (y < bottom) draw_title(app, var, x, y, w, selected);
    for (long c = col0; c < cols && c - col0 < fit; c++) {
        int cx = x + left + (int)(c - col0) * (cw + 1);
        short color = C_FAINT;
        char label[40] = "";
        for (int k = 0; k < nc; k++) {
            if (mc[k].at - bc->lo != c) continue;
            color = mc[k].color;
            snprintf(label + strlen(label), sizeof label - strlen(label), "%s%s", label[0] ? "," : "▼ ", mc[k].name);
        }
        if (y + 1 < bottom) print_at(cx + cw / 2 - 1, y + 1, -1, S(color, C_DEFAULT, A_BOLD), "%s", label);
        if (y + 2 < bottom) print_centered(cx, y + 2, cw, S(color, C_DEFAULT, 0), "%ld", bc->lo + c);
    }
    for (long r = row0; r < rows && r - row0 < tall; r++) {
        int cy = y + 3 + (int)(r - row0);
        if (cy >= bottom) return;
        short color = C_FAINT;
        char label[24] = "";
        for (int k = 0; k < nr; k++) {
            if (mr[k].at - br->lo != r) continue;
            color = mr[k].color;
            snprintf(label + strlen(label), sizeof label - strlen(label), "%s%s", label[0] ? "," : "", mr[k].name);
        }
        print_at(x, cy, left - 1, S(color, C_DEFAULT, A_BOLD), "%4.4s%s%3ld", label, label[0] ? "▶" : " ", br->lo + r);
        for (long c = col0; c < cols && c - col0 < fit; c++) {
            Pointer at = moved(var->at, (int)(r * cols + c));
            const Value *v = vm_cell(vm, at);
            Style st = cell_style(vm, at, v);
            int cx = x + left + (int)(c - col0) * (cw + 1);
            char text[64];
            cell_text(vm, v, base, text, sizeof text);
            fill(cx, cy, cw, 1, st);
            print_centered(cx, cy, cw, st, "%s", text);
        }
    }
}

/* "v.inner.r": a slot by the names of the fields that lead to it. */
static void slot_name(App *app, const Type *type, int slot, char *out, size_t size) {
    const Config *config = &app->doc->config;
    out[0] = '\0';
    while (type->kind == TY_RECORD || type->kind == TY_ARRAY) {
        size_t n = strlen(out);
        if (type->kind == TY_ARRAY) {
            int per = vm_slots(config, type->base);
            if (per <= 0) return;
            long long lo = config->index_base;
            if (type->dims.items[0].lo) const_int(type->dims.items[0].lo, &lo);
            snprintf(out + n, size - n, "[%lld]", lo + slot / per);
            slot %= per;
            type = type->base;
            continue;
        }
        const VarDecl *f = type->record->fields;
        while (f->next && slot >= vm_slots(config, f->symbol->type)) {
            slot -= vm_slots(config, f->symbol->type);
            f = f->next;
        }
        snprintf(out + n, size - n, "%s%s", n ? "." : "", f->name);
        type = f->symbol->type;
    }
}

/* The slots of one record on a line: "key 4   next →b". */
static void draw_fields(App *app, const Type *type, Pointer at, int x, int y, int w) {
    const Vm *vm = app->run.vm;
    int slots = vm_slots(&app->doc->config, type), cx = x;
    for (int i = 0; i < slots; i++) {
        char name[64], text[64];
        Pointer p = moved(at, i);
        const Value *v = vm_cell(vm, p);
        slot_name(app, type, i, name, sizeof name);
        cell_text(vm, v, vm_slot_type(&app->doc->config, type, i, NULL), text, sizeof text);
        int need = text_width(name) + text_width(text) + 5;
        if (cx + need > x + w) {
            put_text(cx, y, S_DIM, "…");
            return;
        }
        int k = cx + print_at(cx, y, -1, S_DIM, "%s", name) + 1;
        print_at(k, y, -1, cell_style(vm, p, v), " %s ", text);
        cx += need;
    }
}

static void draw_table(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    const Type *base = var->symbol->type->base;
    int per = vm_slots(&app->doc->config, base), rows = block->height - 2;
    long n = 1;
    for (int i = 0; i < var->rank; i++) n *= extent(vm, var, i);
    Marker m[6];
    int count = markers(app, block->frame, var->symbol, 0, m);
    const Bounds *b = vm_bounds(vm, var, 0);
    int start = window_start(vm, var, per, b->lo, n, rows, m, count);
    if (y < bottom) draw_title(app, var, x, y, w, selected);
    for (int r = 0; r < rows && start + r < n && y + 1 + r < bottom; r++) {
        char label[64];
        Pointer at = moved(var->at, (start + r) * per);
        vm_place_name(vm, at, label, sizeof label);
        const char *bracket = strchr(label, '[');
        short color = C_FAINT;
        for (int k = 0; k < count; k++)
            if (m[k].at - b->lo == start + r && var->rank == 1) color = m[k].color;
        if (bracket) print_at(x + 1, y + 1 + r, 8, S(color, C_DEFAULT, 0), "%s", bracket);
        draw_fields(app, base, at, x + 9, y + 1 + r, w - 9);
    }
}

static void draw_record_value(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    const Type *type = var->symbol->type;
    if (y < bottom) draw_title(app, var, x, y, w, selected);
    for (int i = 0; i < block->height - 2 && y + 1 + i < bottom; i++) {
        char name[64], text[64];
        Pointer at = moved(var->at, i);
        const Value *v = vm_cell(vm, at);
        slot_name(app, type, i, name, sizeof name);
        cell_text(vm, v, vm_slot_type(&app->doc->config, type, i, NULL), text, sizeof text);
        print_at(x + 2, y + 1 + i, 20, S_DIM, "%s", name);
        print_at(x + 22, y + 1 + i, w - 22, cell_style(vm, at, v), " %s ", text);
    }
}

static void node_label(App *app, const Record *record, Pointer p, char *out, size_t size) {
    const Vm *vm = app->run.vm;
    out[0] = '\0';
    for (const VarDecl *f = record->fields; f; f = f->next) {
        if (!is_scalar_type(f->symbol->type) || f->symbol->type->kind == TY_POINTER) continue;
        char text[40];
        cell_text(vm, vm_cell(vm, moved(p, f->symbol->slot)), f->symbol->type, text, sizeof text);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? " " : "", text);
    }
    if (!out[0]) snprintf(out, size, "•");
}

static Style node_style(App *app, const Record *record, Pointer p) {
    int state = STATE_IDLE;
    for (const VarDecl *f = record->fields; f; f = f->next) {
        int s = cell_state(app->run.vm, moved(p, f->symbol->slot));
        if (s > state) state = s;
    }
    return state == STATE_WRITTEN ? S(231, C_WRITE_BG, A_BOLD) : state == STATE_READ ? S(231, C_READ_BG, A_BOLD)
                                                                                     : S(C_TEXT, C_CELL, 0);
}

static int draw_tree(App *app, const Record *record, Pointer p, const VarDecl *a, const VarDecl *b, int x, int y,
                     int bottom, int depth, const char *edge) {
    const Vm *vm = app->run.vm;
    if (p.object == 0 || !vm_cell(vm, p) || depth > 5 || y >= bottom) return y;
    char label[60];
    node_label(app, record, p, label, sizeof label);
    int at = x + print_at(x, y, -1, S_FAINT, "%s", edge);
    print_at(at, y, -1, node_style(app, record, p), " %s ", label);
    const Value *l = vm_cell(vm, moved(p, a->symbol->slot)), *r = vm_cell(vm, moved(p, b->symbol->slot));
    char text[60];
    y++;
    if (l && l->known && l->kind == V_POINTER && l->as.p.object) {
        snprintf(text, sizeof text, "├─%.20s─ ", a->name);
        y = draw_tree(app, record, l->as.p, a, b, x + 4, y, bottom, depth + 1, text);
    }
    if (r && r->known && r->kind == V_POINTER && r->as.p.object) {
        snprintf(text, sizeof text, "└─%.20s─ ", b->name);
        y = draw_tree(app, record, r->as.p, a, b, x + 4, y, bottom, depth + 1, text);
    }
    return y;
}

static void draw_chain(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    const Vm *vm = app->run.vm;
    const Var *var = block->vars[0];
    const Record *record = var->symbol->type->base->record;
    const VarDecl *a = NULL, *b = NULL;
    int links = self_links(record, &a, &b);
    const Value *v = vm_cell(vm, var->at);
    if (y < bottom) draw_title(app, var, x, y, w, selected);
    if (++y >= bottom) return;
    if (!v || !v->known) {
        print_at(x + 1, y, w, S(C_FAINT, C_DEFAULT, 0), "· %s", T("points nowhere yet"));
        return;
    }
    if (links >= 2 && v->as.p.object) {
        draw_tree(app, record, v->as.p, a, b, x + 1, y, bottom, 0, "");
        return;
    }
    int cx = x + 1;
    Pointer p = v->as.p, seen[64];
    for (int n = 0;; n++) {
        cx += print_at(cx, y, -1, S(C_POINTER, C_DEFAULT, 0), "─▶ ");
        if (p.object == 0) {
            put_text(cx, y, S(C_DIM, C_DEFAULT, A_BOLD), "∅");
            return;
        }
        if (!vm_cell(vm, p)) {
            print_at(cx, y, w, S(C_ERROR, C_DEFAULT, A_BOLD), "%s", T("✗ gone"));
            return;
        }
        for (int k = 0; k < n && k < 64; k++) {
            if (seen[k].object == p.object && seen[k].offset == p.offset) {
                print_at(cx, y, w, S(C_FOCUS, C_DEFAULT, 0), "↻ %s", T("goes back"));
                return;
            }
        }
        if (n < 64) seen[n] = p;
        char label[60];
        node_label(app, record, p, label, sizeof label);
        if (cx + text_width(label) + 8 > x + w || n >= 60) {
            put_text(cx, y, S_DIM, "…");
            return;
        }
        cx += print_at(cx, y, -1, node_style(app, record, p), " %s ", label) + 1;
        const Value *next = vm_cell(vm, moved(p, a->symbol->slot));
        if (!next || !next->known || next->kind != V_POINTER) {
            put_text(cx, y, S_FAINT, "· ?");
            return;
        }
        p = next->as.p;
    }
}

static void draw_block(App *app, const Block *block, int x, int y, int w, int bottom, bool selected) {
    switch (block->kind) {
        case BLOCK_SCALARS: draw_scalars(app, block, x, y, w, bottom); break;
        case BLOCK_ROW: draw_row(app, block, x, y, w, bottom, selected); break;
        case BLOCK_GRID: draw_grid(app, block, x, y, w, bottom, selected); break;
        case BLOCK_TABLE: draw_table(app, block, x, y, w, bottom, selected); break;
        case BLOCK_RECORD: draw_record_value(app, block, x, y, w, bottom, selected); break;
        case BLOCK_CHAIN: draw_chain(app, block, x, y, w, bottom, selected); break;
        case BLOCK_TITLE: print_at(x, y, w, S_FAINT, "┄ %s", block->title); break;
    }
}

typedef struct {
    const Symbol *symbols[32];
    int count;
} Mentioned;

static void note_symbol(void *context, const Expr *e) {
    Mentioned *m = context;
    if (e->kind != EX_NAME || !e->symbol || e->symbol->kind == SYM_FUNCTION) return;
    for (int i = 0; i < m->count; i++)
        if (m->symbols[i] == e->symbol) return;
    if (m->count < 32) m->symbols[m->count++] = e->symbol;
}

void run_data_draw(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int frame = vm_frame_count(vm) - 1, n = 0;
    Block blocks[64];
    Mentioned m = {0};
    bool only = app->run.level == LEVEL_EXPRESSION && vm_frame(vm, frame)->stmt;
    if (only) { /* what the statement itself mentions, not its body */
        const Stmt *s = vm_frame(vm, frame)->stmt;
        visit_expr(s->target, note_symbol, &m);
        visit_expr(s->value, note_symbol, &m);
        visit_expr(s->limit, note_symbol, &m);
        visit_expr(s->step, note_symbol, &m);
        for (int i = 0; i < s->args.count; i++) visit_expr(s->args.items[i], note_symbol, &m);
    }
    n = collect(app, frame, only ? m.symbols : NULL, m.count, blocks, 40, w);
    if (frame > 0) {
        Block title = {.kind = BLOCK_TITLE, .title = T("globals")};
        Block globals[20];
        int g = collect(app, 0, only ? m.symbols : NULL, m.count, globals, 20, w);
        if (g > 0) {
            title.height = 1;
            blocks[n++] = title;
            for (int i = 0; i < g && n < 64; i++) blocks[n++] = globals[i];
        }
    }
    if (n == 0) {
        print_wrapped(x, y, w, 3, S_FAINT, "%s",
                      only ? T("This statement uses no variables.")
                             : T("There are no variables here."));
        return;
    }
    int selected = app->run.selected_var % n, first = 0, total = 0;
    while (blocks[selected].kind == BLOCK_TITLE) selected = (selected + 1) % n;
    for (int i = 0; i < n; i++) total += blocks[i].height;
    if (total > h) { /* scroll so that the selected one is in */
        int used = 0;
        for (int i = selected; i >= 0; i--) {
            used += blocks[i].height;
            if (used > h && i < selected) break;
            first = i;
        }
    }
    int cy = y;
    for (int i = first; i < n && cy < y + h; i++) {
        draw_block(app, &blocks[i], x, cy, w, y + h, i == selected && app->run.selected_var > 0);
        cy += blocks[i].height;
        if (cy >= y + h && i < n - 1) print_at(x + w - 14, y + h - 1, 14, S_DIM, "%s", T("▼ more (Tab)"));
    }
}

/* ---- memory ---- */

static void bytes_of(const Vm *vm, const Value *v, const Type *type, char *out, size_t size) {
    unsigned char raw[8];
    int n = 4;
    memset(raw, 0, sizeof raw);
    if (v->kind == V_POINTER) {
        uint64_t address = 0;
        const Object *o = vm_object(vm, v->as.p);
        if (o && o->type) address = o->address;
        n = 8;
        for (int i = 0; i < 8; i++) raw[i] = (unsigned char)(address >> (8 * i));
    } else if (v->kind == V_FLOAT) {
        memcpy(raw, &v->as.f, 4);
    } else {
        uint32_t u = (uint32_t)v->as.i;
        n = type && (type->kind == TY_BOOL || type->kind == TY_CHAR) ? 1 : 4;
        for (int i = 0; i < 4; i++) raw[i] = (unsigned char)(u >> (8 * i));
    }
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%02x", i ? " " : "", raw[i]);
    }
}

/* The stack as it is: newest frame on top, one line per slot. */
void run_memory_draw(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    print_at(x, y, 10, S_FAINT, "%s", T("address"));
    print_at(x + 11, y, 13, S_FAINT, "%s", T("name"));
    print_at(x + 25, y, 12, S_FAINT, "%s", T("value"));
    print_at(x + 38, y, w - 38, S_FAINT, "%s", T("byte"));
    int row = y + 1, frame = -1;
    for (int i = vm_object_count(vm) - 1; i > 0 && row < y + h; i--) {
        const Object *o = vm_object_at(vm, i);
        if (o->count == 0 || (!o->symbol && !o->is_temporary)) continue;
        if (o->frame != frame) {
            frame = o->frame;
            const Function *f = frame >= 0 && frame < vm_frame_count(vm) ? vm_frame(vm, frame)->function : NULL;
            if (row > y + 1) row++;
            if (row >= y + h) return;
            bool top = frame == vm_frame_count(vm) - 1;
            print_at(x, row++, w, S_TEXT, "%s%s %s ", top ? M_CHIP_CALL : M_CHIP_OFF, top ? " ▶" : " ",
                     f ? f->full_name : vm_trial_count(vm) ? T("try") : T("globals"));
        }
        Pointer base = vm_pointer_to(vm, i);
        int from = 0, limit = 8; /* long arrays: around what was touched */
        for (int k = 0; k < o->count && o->count > limit; k++) {
            if (cell_state(vm, moved(base, k)) == STATE_IDLE) continue;
            from = k - limit / 2 < 0 ? 0 : k - limit / 2;
            if (from > o->count - limit) from = o->count - limit;
            break;
        }
        if (from > 0 && row < y + h) print_at(x, row++, w, S_FAINT, T("           … %d more"), from);
        for (int k = from; k < o->count && k < from + limit && row < y + h; k++) {
            Pointer at = moved(base, k);
            const Value *v = vm_cell(vm, at);
            int byte = 0;
            const Type *type = o->type ? vm_slot_type(&app->doc->config, o->type, k, &byte) : NULL;
            char name[64], text[64], raw[40];
            vm_slot_name(vm, at, name, sizeof name);
            cell_text(vm, v, type, text, sizeof text);
            bytes_of(vm, v, type, raw, sizeof raw);
            int state = cell_state(vm, at);
            Style st = state == STATE_WRITTEN ? S(231, C_WRITE_BG, A_BOLD) : state == STATE_READ ? S(231, C_READ_BG, A_BOLD) : S_TEXT;
            if (state != STATE_IDLE) fill(x, row, w, 1, st);
            print_at(x, row, 10, S(state ? 231 : C_DIM, st.bg, 0), "%08x", o->address + (unsigned)byte);
            print_at(x + 11, row, 13, st, "%s", name);
            print_at(x + 25, row, 12, st, "%s", text);
            print_at(x + 38, row, w - 38, S(state ? 231 : v->known ? C_DIM : C_FAINT, st.bg, 0), "%s", v->known ? raw : "??");
            row++;
        }
        int rest = o->count - from - limit;
        if (rest > 0 && row < y + h) print_at(x, row++, w, S_FAINT, T("           … %d more"), rest);
    }
}
