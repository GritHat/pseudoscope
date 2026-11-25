#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Plans, trees and pies: the diagrams that are walked along time, or along
 * their branches. */

/* ---- plans ---- */

static long plan_length(const Uml *u) {
    long last = 1;
    for (int i = 0; i < u->task_count; i++)
        if (u->tasks[i].end > last) last = u->tasks[i].end;
    return last;
}

static void day_text(const Uml *u, long day, char *out, size_t size) {
    if (u->first_day < 0) {
        snprintf(out, size, "%s %ld", T("day"), day + 1);
        return;
    }
    int y, m, d;
    uml_date(u->first_day + day, &y, &m, &d);
    snprintf(out, size, "%04d-%02d-%02d", y, m, d);
}

/* The next day on which something begins or ends, in either direction. */
long plan_step(const Uml *u, long day, int direction) {
    long best = -1, last = plan_length(u);
    for (int i = 0; i < u->task_count; i++) {
        long marks[2] = {u->tasks[i].start, u->tasks[i].end};
        for (int k = 0; k < 2; k++) {
            long m = marks[k];
            if (direction > 0 ? m <= day : m >= day) continue;
            if (best < 0 || (direction > 0 ? m < best : m > best)) best = m;
        }
    }
    if (best < 0) best = direction > 0 ? last : 0;
    return best;
}

void draw_uml_plan(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    long total = plan_length(u), today = app->walk.at;
    int label = 8;
    for (int i = 0; i < u->task_count; i++)
        if (text_width(u->tasks[i].name) + 4 > label) label = text_width(u->tasks[i].name) + 4;
    if (label > w / 3) label = w / 3;
    int room = w - label - 2;
    if (room < 10) return;
    long per = total > room ? (total + room - 1) / room : 1; /* days in a character */
    int scale = total * 3 <= room ? 3 : total * 2 <= room ? 2 : 1;
#define COLUMN(day) ((int)((day) * scale / per))

    /* the days */
    long tick = 5 * per;
    while (COLUMN(tick) < 11) tick += 5 * per;
    for (long d = 0; COLUMN(d) < room - 9; d += tick) {
        char text[32];
        day_text(u, d, text, sizeof text);
        print_at(x + label + COLUMN(d), y, 12, S_FAINT, "%s", u->first_day < 0 ? text : text + 5);
        put_text(x + label + COLUMN(d), y + 1, S_FAINT, "╷");
    }

    int row = y + 2, bottom = y + h - 5;
    const char *section = NULL;
    int top = 0, rows = 0;
    for (int i = 0; i < u->task_count; i++) rows += 1 + (u->tasks[i].section && (!i || u->tasks[i - 1].section != u->tasks[i].section));
    if (rows > bottom - row) top = scroll_to(app->walk.top, app->walk.selected, 0, u->task_count - 1, (bottom - row) / 2);
    app->walk.top = top;
    for (int i = top; i < u->task_count && row < bottom; i++) {
        const UmlTask *t = &u->tasks[i];
        if (t->section && t->section != section) {
            section = t->section;
            print_at(x, row++, w, S(C_TYPE, C_DEFAULT, A_BOLD), "%s", section);
            if (row >= bottom) break;
        }
        bool over = t->end <= today && !(t->milestone && t->start > today), running = t->start <= today && today < t->end;
        if (t->milestone) over = t->start <= today, running = false;
        bool selected = i == app->walk.selected;
        Style name = selected ? S(C_BRIGHT, C_SELECT, A_BOLD) : over ? S_DIM : running ? S(C_FOCUS, C_DEFAULT, A_BOLD) : S_TEXT;
        print_at(x, row, label - 1, name, "%s%s", over ? "✓ " : running ? "▶ " : "  ", t->name);
        int a = COLUMN(t->start), b = COLUMN(t->end);
        if (b <= a) b = a + 1;
        if (b > room) b = room;
        if (t->milestone) {
            put_text(x + label + a, row, S(over ? C_WRITE : C_FOCUS, C_DEFAULT, A_BOLD), "◆");
        } else {
            short color = over ? 28 : running ? C_FOCUS_BG : t->critical ? 160 : 31;
            for (int c = a; c < b; c++) {
                bool done = running && c < COLUMN(today);
                put_text(x + label + c, row, S(running ? 16 : 231, done ? 28 : color, 0), " ");
            }
            char days[24];
            snprintf(days, sizeof days, "%d", t->days);
            if (b - a > text_width(days) + 1) print_at(x + label + a + 1, row, b - a - 1, S(running ? 16 : 231, over ? 28 : running ? C_FOCUS_BG : color, A_BOLD), "%s", days);
        }
        if (t->after >= 0 && selected) { /* what it waits for */
            int from = COLUMN(u->tasks[t->after].end);
            for (int c = from; c < a; c++) put_text(x + label + c, row, S(C_POINTER, C_DEFAULT, 0), "┄");
        }
        row++;
    }
    if (COLUMN(today) < room) put_text(x + label + COLUMN(today), y + 1, S(C_FOCUS, C_DEFAULT, A_BOLD), "▼");
#undef COLUMN

    /* what is going on today */
    char date[32], line[400] = "";
    day_text(u, today, date, sizeof date);
    int running = 0, done = 0;
    for (int i = 0; i < u->task_count; i++) {
        const UmlTask *t = &u->tasks[i];
        done += t->milestone ? t->start <= today : t->end <= today;
        if (t->milestone || !(t->start <= today && today < t->end)) continue;
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, "%s%s", running++ ? ", " : "", t->name);
    }
    hline(x, y + h - 4, w, S_FAINT, "┄");
    print_at(x, y + h - 3, w, S_TEXT, M_CHIP_FOCUS " %s " M_RESET "  " M_DIM "%s %d / %d", date, T("done"), done, u->task_count);
    print_at(x, y + h - 2, w, S_TEXT, M_DIM "%s " M_RESET "%s", T("in progress:"), running ? line : T("nothing"));
    const UmlTask *sel = app->walk.selected < u->task_count ? &u->tasks[app->walk.selected] : NULL;
    if (sel) {
        char from[32], to[32];
        day_text(u, sel->start, from, sizeof from);
        day_text(u, sel->end > sel->start ? sel->end - 1 : sel->start, to, sizeof to);
        if (sel->after >= 0)
            print_at(x, y + h - 1, w, S_TEXT, M_BOLD "%s" M_RESET M_DIM "  %s → %s   %s " M_POINTER "%s", sel->name, from, to,
                     T("waits for"), u->tasks[sel->after].name);
        else
            print_at(x, y + h - 1, w, S_TEXT, M_BOLD "%s" M_RESET M_DIM "  %s → %s%s", sel->name, from, to,
                     sel->critical ? T("   critical") : "");
    }
}

/* ---- trees ---- */

static bool is_last_child(const Uml *u, int node) {
    for (int i = node + 1; i < u->node_count; i++) {
        if (u->nodes[i].depth < u->nodes[node].depth) return true;
        if (u->nodes[i].depth == u->nodes[node].depth) return u->nodes[i].parent != u->nodes[node].parent;
    }
    return true;
}

void draw_uml_tree(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    int side = w > 90 ? 36 : 0, at = app->walk.at;
    int top = scroll_to(app->walk.top, at, 0, u->node_count - 1, h);
    app->walk.top = top;
    /* the branches that lead to where we are */
    bool on_path[4096] = {false};
    for (int n = at; n >= 0 && n < 4096; n = u->nodes[n].parent) on_path[n] = true;
    for (int i = top; i < u->node_count && i - top < h; i++) {
        const UmlNode *n = &u->nodes[i];
        int row = y + i - top;
        /* the lines of its ancestors that still have children below */
        int ancestor = n->parent;
        for (int d = n->depth - 1; d >= 1 && ancestor >= 0; d--) {
            if (!is_last_child(u, ancestor)) put_text(x + (d - 1) * 3 + 1, row, S_FAINT, "│");
            ancestor = u->nodes[ancestor].parent;
        }
        int cx = x + (n->depth > 0 ? (n->depth - 1) * 3 + 1 : 0);
        bool path = i < 4096 && on_path[i];
        if (n->depth > 0) {
            put_text(cx, row, path ? S(C_FOCUS, C_DEFAULT, 0) : S_FAINT, is_last_child(u, i) ? "└─" : "├─");
            cx += 3;
        }
        Style st = i == at ? S(16, C_FOCUS_BG, A_BOLD) : path ? S(C_FOCUS, C_DEFAULT, A_BOLD)
                 : n->depth == 0 ? S(C_TYPE, C_DEFAULT, A_BOLD) : n->depth == 1 ? S(C_TEXT, C_DEFAULT, A_BOLD) : S_TEXT;
        print_at(cx, row, x + w - side - cx - 1, st, i == at ? " %s " : "%s", n->text);
    }
    draw_scrollbar(x + w - side - 1, y, h, top, h, u->node_count);
    if (!side) return;
    int sx = x + w - side + 2, row = y;
    vline(sx - 2, y, h, S_FAINT, "│");
    print_at(sx, row, side - 2, S_BOLD, "%s", T("How to get here"));
    row += 2;
    int chain[40], n = 0;
    for (int i = at; i >= 0 && n < 40; i = u->nodes[i].parent) chain[n++] = i;
    for (int i = n - 1; i >= 0 && row < y + h - 3; i--)
        print_at(sx + (n - 1 - i < 6 ? n - 1 - i : 6), row++, side - 4, i == 0 ? S(16, C_FOCUS_BG, A_BOLD) : S_DIM, i == 0 ? " %s " : "%s",
                 u->nodes[chain[i]].text);
    int children = 0, leaves = 0;
    for (int i = at + 1; i < u->node_count && u->nodes[i].depth > u->nodes[at].depth; i++) {
        children += u->nodes[i].parent == at;
        leaves++;
    }
    if (row + 2 < y + h) {
        char branches[48];
        snprintf(branches, sizeof branches, TN("%d branch", "%d branches", children), children);
        print_at(sx, row + 1, side - 2, S_DIM, "%s, %d %s", branches, leaves, T("below in all"));
    }
}

/* ---- pies ---- */

void draw_uml_pie(App *app, int x, int y, int w, int h) {
    const Uml *u = app->doc->uml;
    static const short COLORS[] = {31, 97, 28, 130, 25, 125, 64, 60};
    double total = 0, before = 0;
    int label = 8;
    for (int i = 0; i < u->node_count; i++) {
        total += u->nodes[i].value;
        if (text_width(u->nodes[i].text) + 2 > label) label = text_width(u->nodes[i].text) + 2;
    }
    if (label > w / 3) label = w / 3;
    if (total <= 0) return;
    int room = w - label - 16;
    /* the whole, as one bar */
    int cx = x;
    for (int i = 0; i < u->node_count; i++) {
        int cells = (int)(u->nodes[i].value / total * w + 0.5);
        if (i == u->node_count - 1) cells = x + w - cx;
        for (int c = 0; c < cells && cx < x + w; c++, cx++)
            put_text(cx, y, S(231, COLORS[i % 8], i == app->walk.selected ? A_BOLD : 0), i == app->walk.selected ? "▄" : " ");
    }
    for (int i = 0; i < u->node_count && y + 2 + i * 2 < y + h; i++) {
        const UmlNode *n = &u->nodes[i];
        int row = y + 2 + i * 2, cells = (int)(n->value / total * room + 0.5);
        bool selected = i == app->walk.selected;
        print_at(x, row, label - 1, selected ? S(C_BRIGHT, C_SELECT, A_BOLD) : S_TEXT, "%s", n->text);
        for (int c = 0; c < cells; c++) put_text(x + label + c, row, S(231, COLORS[i % 8], 0), " ");
        print_at(x + label + cells + 1, row, 15, selected ? S_BOLD : S_DIM, "%.1f%%  %g", n->value / total * 100, n->value);
        if (i < app->walk.selected) before += n->value;
    }
    if (app->walk.selected < u->node_count) {
        const UmlNode *n = &u->nodes[app->walk.selected];
        print_at(x, y + h - 1, w, S_DIM, "%s " M_RESET M_BOLD "%s" M_RESET M_DIM ": %g %s %g   %s %.1f%%", T("up to"), n->text,
                 before + n->value, T("of"), total, T("that is"), (before + n->value) / total * 100);
    }
}
