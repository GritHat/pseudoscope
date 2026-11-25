#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int scroll_to(int top, int line, int first, int last, int h) {
    int margin = h > 8 ? 2 : 0;
    if (line < top + margin) top = line - margin;
    if (line > top + h - 1 - margin) top = line - h + 1 + margin;
    if (top > last - h + 1) top = last - h + 1;
    return top < first ? first : top;
}

static short tint_of(const CodeView *view, const char *word, size_t n) {
    for (int i = 0; i < view->tint_count; i++) {
        const char *name = view->tints[i].symbol->name;
        if (strlen(name) == n && strncmp(name, word, n) == 0) return view->tints[i].color;
    }
    return 0;
}

static bool is_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

void draw_code(int x, int y, int w, int h, const CodeView *view) {
    const Document *doc = view->doc;
    for (int row = 0; row < h; row++) {
        int line = view->top + row;
        if (line > view->last || line > doc->line_count || line < 1) break;
        bool current = line == view->current;
        short bg = current ? view->current_bg : C_DEFAULT;
        if (current) fill(x, y + row, w, 1, S(C_TEXT, bg, 0));
        bool stop = view->breakpoints && view->breakpoints[line];
        put_text(x, y + row, S(stop ? C_ERROR : C_FOCUS, bg, A_BOLD), stop ? "●" : current ? "▶" : " ");
        char number[16];
        snprintf(number, sizeof number, "%4d", line);
        put_text(x + 1, y + row, S(current ? C_FOCUS : C_FAINT, bg, 0), number);

        int col = x + 6, end = x + w;
        int i = doc->lines[line - 1], stop_at = doc->lines[line] - 1;
        if (stop_at > (int)doc->text.length) stop_at = (int)doc->text.length;
        short tint = 0;
        while (i < stop_at && col < end) {
            char c = doc->text.data[i];
            if (c == '\r') break;
            if (c == '\t') {
                col += 4 - (col - x - 6) % 4;
                i++;
                continue;
            }
            int kind = doc->kinds[i];
            if (is_word(c) && (i == doc->lines[line - 1] || !is_word(doc->text.data[i - 1]))) {
                int n = 1;
                while (i + n < stop_at && is_word(doc->text.data[i + n])) n++;
                tint = kind == H_PLAIN ? tint_of(view, doc->text.data + i, (size_t)n) : 0;
            } else if (!is_word(c)) {
                tint = 0;
            }
            Style st = highlight_style(kind);
            if (tint) st = S(tint, C_DEFAULT, A_BOLD);
            st.bg = bg;
            char glyph[5] = "";
            int n = char_bytes(doc->text.data + i);
            memcpy(glyph, doc->text.data + i, (size_t)n);
            put_text(col++, y + row, st, glyph);
            i += n;
        }
        if (col >= end && i < stop_at) put_text(end - 1, y + row, S(C_DIM, bg, 0), "…");
    }
    draw_scrollbar(x + w, y, h, view->top - view->first, h, view->last - view->first + 1);
}

static bool c_keyword(const char *word, size_t n) {
    static const char *const WORDS[] = {"int", "float", "bool", "char", "void", "struct", "const",
                                        "if", "else", "for", "while", "do", "return", "true", "false"};
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
        if (strlen(WORDS[i]) == n && strncmp(WORDS[i], word, n) == 0) return true;
    return false;
}

void draw_c_code(int x, int y, int w, int h, const char *text, int top, int hot_first, int hot_last) {
    int line = 0;
    for (const char *s = text; *s; line++) {
        size_t n = strcspn(s, "\n");
        int row = line - top;
        if (row >= h) break;
        if (row >= 0) {
            bool hot = line >= hot_first && line <= hot_last;
            short bg = hot ? C_SELECT : C_DEFAULT;
            if (hot) fill(x, y + row, w, 1, S(C_TEXT, bg, 0));
            put_text(x, y + row, S(C_FOCUS, bg, A_BOLD), hot ? "▶" : " ");
            int col = x + 2;
            for (size_t i = 0; i < n && col < x + w;) {
                Style st = S(C_TEXT, bg, 0);
                size_t k = 1;
                if (is_word(s[i])) {
                    while (i + k < n && is_word(s[i + k])) k++;
                    if (c_keyword(s + i, k)) st = S(111, bg, A_BOLD);
                    else if (s[i] >= '0' && s[i] <= '9') st = S(C_NUMBER, bg, 0);
                    else if (i + k < n && s[i + k] == '(') st = S(C_CALL, bg, 0);
                } else if (s[i] == '"') {
                    while (i + k < n && s[i + k] != '"') k += s[i + k] == '\\' ? 2 : 1;
                    if (i + k < n) k++;
                    st = S(C_STRING, bg, 0);
                }
                for (size_t j = 0; j < k && i + j < n && col < x + w; j++) {
                    char glyph[2] = {s[i + j], 0};
                    put_text(col++, y + row, st, glyph);
                }
                i += k;
            }
        }
        s += n;
        if (*s) s++;
    }
}

void draw_footer(const Hint *hints, int count) {
    int y = screen_height() - 1, x = 1, w = screen_width();
    /* the last one (help) always has its place */
    int last = count ? text_width(hints[count - 1].key) + text_width(hints[count - 1].label) + 5 : 0;
    for (int i = 0; i < count; i++) {
        int need = text_width(hints[i].key) + text_width(hints[i].label) + 5;
        if (i < count - 1 && x + need + last > w) continue;
        x += print_at(x, y, -1, S_DIM, M_KEY " %s " M_RESET " %s", hints[i].key, hints[i].label) + 2;
    }
}

void draw_header(const char *where, const char *const *levels, int level_count, int level, short color) {
    int w = screen_width(), room = w - 4;
    int x = w - 1;
    for (int i = level_count - 1; i >= 0; i--) {
        int pill = text_width(levels[i]) + 4;
        x -= pill;
        draw_pill(x, 0, pill, levels[i], i == level, color);
        if (i > 0) {
            x -= 1;
            put_text(x, 0, S_FAINT, "▸");
        }
    }
    if (level_count) room = x - 3;
    /* the name of the program gives way to where we are */
    bool narrow = room < 15 + text_width(where);
    print_at(1, 0, room, S_DIM, narrow ? M_READ M_BOLD "◆" M_RESET " %s" : M_READ M_BOLD "◆ PSEUDOSCOPE" M_RESET "  %s", where);
}

void draw_table_row(int x, int y, int w, bool selected, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    Style st = selected ? S(C_BRIGHT, C_SELECT, A_BOLD) : S_TEXT;
    if (selected) fill(x, y, w, 1, st);
    put_text(x, y, S(C_FOCUS, selected ? C_SELECT : C_DEFAULT, A_BOLD), selected ? "▸" : " ");
    print_at(x + 2, y, w - 3, st, "%s", text);
}
