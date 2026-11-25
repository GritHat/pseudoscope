#include "ui.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* A sequence diagram, whatever it comes from: a program that runs, or a
 * drawing. */

static int column(const SequenceView *v, int life, int x, int w, int first, int shown) {
    (void)v;
    int gap = (w - 8) / shown;
    return x + 8 + gap * (life - first) + gap / 2;
}

/* Which columns to show when there is no room for all of them. */
static void choose_columns(const SequenceView *v, int w, int *first, int *shown) {
    int fit = (w - 8) / 16;
    if (fit < 2) fit = 2;
    *shown = v->life_count < fit ? v->life_count : fit;
    *first = 0;
    if (*shown < 1) *shown = 1;
    if (v->life_count <= fit || v->row_count == 0) return;
    const SequenceItem *last = &v->rows[v->row_count - 1];
    int lo = last->from < last->to ? last->from : last->to, hi = last->from > last->to ? last->from : last->to;
    if (lo < 0) lo = hi < 0 ? 0 : hi;
    *first = hi - *shown + 1 > lo ? lo : (lo + hi) / 2 - *shown / 2;
    if (*first > v->life_count - *shown) *first = v->life_count - *shown;
    if (*first < 0) *first = 0;
}

static bool is_active(const SequenceView *v, int life, int row) {
    for (int i = 0; i <= row && i < v->row_count; i++) {
        const SequenceItem *r = &v->rows[i];
        if (r->activates && r->to == life && r->end > row && i <= row) return true;
    }
    return false;
}

static void draw_lifelines(const SequenceView *v, int x, int y, int w, int first, int shown, int row) {
    for (int i = first; i < first + shown; i++) {
        bool on = row >= 0 && is_active(v, i, row);
        put_text(column(v, i, x, w, first, shown), y, S(on ? C_CALL : C_FAINT, C_DEFAULT, on ? A_BOLD : 0), on ? "┃" : "│");
    }
}

static void draw_message(const SequenceView *v, const SequenceItem *r, int x, int y, int w, int first, int shown, bool newest) {
    int a = column(v, r->from, x, w, first, shown), b = column(v, r->to, x, w, first, shown);
    int left_edge = x + 8, right_edge = x + w - 1;
    if (a < left_edge) a = left_edge;
    if (a > right_edge) a = right_edge;
    if (b < left_edge) b = left_edge;
    if (b > right_edge) b = right_edge;
    short color = r->kind == SEQ_LOST ? C_ERROR : r->kind == SEQ_RETURN ? C_WRITE : r->kind == SEQ_IGNORED ? C_DIM : C_READ;
    short chip = r->kind == SEQ_LOST ? C_ERROR_BG : r->kind == SEQ_RETURN ? C_WRITE_BG : r->kind == SEQ_IGNORED ? C_SELECT : C_READ_BG;
    Style line = S(color, C_DEFAULT, 0), label = newest ? S(231, chip, A_BOLD) : S(color, C_DEFAULT, 0);
    if (r->from == r->to) { /* to oneself */
        put_text(a + 1, y, line, r->kind == SEQ_LOST ? "─✗" : "↺");
        print_at(a + 3, y, right_edge - a - 3, label, newest ? " %s " : "%s", r->label);
        return;
    }
    bool right = b > a;
    int lo = right ? a + 1 : b + 1, hi = right ? b - 1 : a - 1;
    int stop = r->kind == SEQ_LOST ? (lo + hi) / 2 : -1;
    const char *dash = r->dashed ? "┄" : "─";
    for (int i = lo; i <= hi; i++) {
        if (stop >= 0 && (right ? i > stop : i < stop)) continue;
        put_text(i, y, line, dash);
    }
    if (r->kind == SEQ_LOST) put_text(stop, y, S(C_ERROR, C_DEFAULT, A_BOLD), "✗");
    else put_text(right ? hi : lo, y, S(color, C_DEFAULT, A_BOLD), r->kind == SEQ_IGNORED ? "⊘" : right ? "▶" : "◀");
    int room = hi - lo - 3;
    if (room > 4) {
        int used = text_width(r->label) + 2;
        if (used > room) used = room;
        int at = (lo + hi) / 2 - used / 2;
        if (r->kind == SEQ_LOST) at = right ? lo + 1 : hi - used;
        print_at(at, y, used, label, " %s ", r->label);
    }
}

void draw_sequence(int x, int y, int w, int h, const SequenceView *v) {
    int first, shown;
    if (v->life_count == 0) return;
    choose_columns(v, w, &first, &shown);
    int gap = (w - 8) / shown;
    for (int i = first; i < first + shown; i++) { /* who takes part */
        int width = text_width(v->lives[i]) + 4, at = column(v, i, x, w, first, shown);
        if (width > gap - 1) width = gap - 1;
        bool on = i == v->current;
        draw_box(at - width / 2, y, width, 3, on ? C_CALL : v->kinds && v->kinds[i] ? C_TYPE : C_BORDER, NULL);
        print_centered(at - width / 2 + 1, y + 1, width - 2, S(on ? C_CALL : C_TEXT, C_DEFAULT, A_BOLD), "%s", v->lives[i]);
    }
    if (first > 0) put_text(x + 1, y + 1, S_DIM, "‹");
    if (first + shown < v->life_count) put_text(x + w - 1, y + 1, S_DIM, "›");

    int top = y + 3, rows = h - 3 - v->flying_count, start = v->row_count > rows ? v->row_count - rows - v->back : 0;
    if (start < 0) start = 0;
    for (int k = 0; k < rows; k++) {
        int i = start + k;
        draw_lifelines(v, x, top + k, w, first, shown, i < v->row_count ? i : v->row_count - 1);
        if (i >= v->row_count) continue;
        const SequenceItem *r = &v->rows[i];
        bool newest = i == v->row_count - 1 - v->back;
        if (r->time >= 0) print_at(x, top + k, 7, S(newest ? C_FOCUS : C_FAINT, C_DEFAULT, 0), "t=%ld", r->time);
        bool from_in = r->from >= first && r->from < first + shown, to_in = r->to >= first && r->to < first + shown;
        switch (r->kind) {
            case SEQ_STEP:
                hline(x + 8, top + k, w - 9, S(C_FOCUS, C_DEFAULT, 0), "─");
                print_at(x + 10, top + k, w - 12, S(C_FOCUS, C_DEFAULT, A_BOLD), " %s ", r->label);
                break;
            case SEQ_CHECK:
                print_at(x + 10, top + k, w - 12, S_TEXT, "%s %s ", r->ok ? M_CHIP_WRITE " ✓" : M_CHIP_ERROR " ✗", r->label);
                break;
            case SEQ_STATE:
                if (from_in)
                    print_at(column(v, r->from, x, w, first, shown) - 1, top + k, gap + 8, S(newest ? 16 : C_FOCUS, newest ? C_FOCUS_BG : C_DEFAULT, A_BOLD),
                             newest ? " ◆ %s " : " ◆ %s", r->label);
                break;
            case SEQ_NOTE:
                print_at(column(v, from_in ? r->from : first, x, w, first, shown) + 2, top + k, w / 2, S(C_TEXT, C_SELECT, A_ITALIC), " %s ", r->label);
                break;
            case SEQ_FRAME:
                hline(x + 8, top + k, w - 9, S(C_TYPE, C_DEFAULT, 0), "┄");
                print_at(x + 9, top + k, w - 11, S_TEXT, M_CHIP_TYPE " %s ", r->label);
                break;
            default:
                if (from_in || to_in) draw_message(v, r, x, top + k, w, first, shown, newest);
        }
    }
    /* what is on its way, under the line of the present */
    for (int k = 0; k < v->flying_count; k++) {
        const SequenceFlying *f = &v->flying[k];
        int row = top + rows + k;
        draw_lifelines(v, x, row, w, first, shown, v->row_count - 1);
        int a = column(v, f->from, x, w, first, shown), b = column(v, f->to, x, w, first, shown);
        print_at(x, row, 8, S(C_POINTER, C_DEFAULT, 0), "→%ld", f->arrives);
        bool visible = f->from >= first && f->from < first + shown && f->to >= first && f->to < first + shown;
        if (!visible) continue;
        if (f->from == f->to) {
            print_at(a + 1, row, gap, S(C_POINTER, C_DEFAULT, 0), "◷ %s", f->label);
            continue;
        }
        int head = a + (int)((b - a) * f->progress), step = b > a ? 1 : -1;
        for (int i = a + step; i != b; i += step) put_text(i, row, S(C_FAINT, C_DEFAULT, 0), "┄");
        for (int i = a + step; i != head && i != b; i += step) put_text(i, row, S(C_POINTER, C_DEFAULT, 0), "─");
        int used = text_width(f->label) + 2, at = step > 0 ? head - used / 2 : head - used / 2;
        print_at(at, row, -1, S(16, C_POINTER, A_BOLD), " %s ", f->label);
    }
}

/* ---- states ---- */

void draw_states(int x, int y, int w, int h, const StateView *v) {
    int row = y, name = 0;
    for (int i = 0; i < v->state_count; i++)
        if (text_width(v->states[i]) > name) name = text_width(v->states[i]);
    if (name > w / 3) name = w / 3;
    int box = name + 4;
    int heights[128], top = 0, total = 0;
    for (int i = 0; i < v->state_count && i < 128; i++) {
        int out = 0;
        for (int k = 0; k < v->arrow_count; k++) out += v->arrows[k].from == i;
        heights[i] = out > 3 ? out + 1 : 4;
        if (out <= 1) heights[i] = 3;
    }
    for (int i = 0; i < v->state_count && i < 128; i++) total += heights[i];
    if (total > h) { /* keep the current one in sight */
        int used = 0;
        for (int i = 0; i < v->state_count && i <= v->current; i++) used += heights[i];
        while (used > h - 4 && top < v->state_count - 1) used -= heights[top++];
    }
    for (int i = top; i < v->state_count && i < 128 && row + 3 <= y + h; i++) {
        bool here = i == v->current;
        draw_box(x, row, box, 3, here ? C_FOCUS : C_BORDER, NULL);
        if (here) fill(x + 1, row + 1, box - 2, 1, S(16, C_FOCUS_BG, 0));
        print_centered(x + 1, row + 1, box - 2, here ? S(16, C_FOCUS_BG, A_BOLD) : S_TEXT, "%s", v->states[i]);
        int line = row;
        bool single = heights[i] == 3;
        if (single) line++;
        for (int k = 0; k < v->arrow_count && line < y + h; k++) {
            const StateArrow *a = &v->arrows[k];
            if (a->from != i) continue;
            bool last = k == v->last, choice = k == v->selected;
            Style st = last ? S(C_WRITE, C_DEFAULT, A_BOLD) : here ? S(C_READ, C_DEFAULT, 0) : S_FAINT;
            put_text(x + box, line, st, "──");
            int label = text_width(a->label);
            int at = x + box + 2;
            if (label) at += print_at(at, line, w - box - name - 12, choice ? S(231, C_READ_BG, A_BOLD) : st, " %s ", a->label);
            put_text(at, line, st, "──▶ ");
            print_at(at + 4, line, name + 2, last ? S(C_WRITE, C_DEFAULT, A_BOLD) : here ? S_TEXT : S_DIM, "%s", v->states[a->to]);
            line++;
        }
        row += heights[i];
    }
}
