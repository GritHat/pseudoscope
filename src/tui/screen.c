#include "screen.h"

#include "plike/i18n.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* PSEUDOSCOPE_FIT=FILE: every text that did not fit where it was put is written
 * there, a line each: what happened, the frame, where, the room, the room it
 * needed, the text. tests/fit.py reads it, language by language. */
static FILE *fit_log;
static int fit_frame;
static bool fit_silent;

typedef struct {
    char ch[5];
    Style st;
} Cell;

static bool active, headless, full_redraw, term_saved;
static bool ascii, poor, linux_console, alternate = true;
static int colors = 256;
static int width, height;
static Cell *cur, *prev;
static struct termios original;
static volatile sig_atomic_t resized, quit;

static char *out;
static size_t out_length, out_capacity;

static void out_add(const char *s, size_t n) {
    if (out_length + n > out_capacity) {
        size_t capacity = out_capacity ? out_capacity : 65536;
        while (capacity < out_length + n) capacity *= 2;
        char *p = realloc(out, capacity);
        if (!p) return;
        out = p;
        out_capacity = capacity;
    }
    memcpy(out + out_length, s, n);
    out_length += n;
}

static void out_str(const char *s) { out_add(s, strlen(s)); }

static void out_flush(void) {
    size_t sent = 0;
    while (sent < out_length) {
        ssize_t n = write(STDOUT_FILENO, out + sent, out_length - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        sent += (size_t)n;
    }
    out_length = 0;
}

static void on_resize(int sig) { (void)sig; resized = 1; }
static void on_quit(int sig) { (void)sig; quit = 1; }

static void allocate(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    free(cur);
    free(prev);
    cur = calloc((size_t)w * (size_t)h, sizeof(Cell));
    prev = calloc((size_t)w * (size_t)h, sizeof(Cell));
    if (!cur || !prev) {
        screen_close();
        fputs(T("pseudoscope: out of memory\n"), stderr);
        exit(3);
    }
    width = w;
    height = h;
    full_redraw = true;
}

static void detect(void);

static void measure(void) {
    struct winsize ws;
    int w = 80, h = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
    allocate(w > 1000 ? 1000 : w, h > 500 ? 500 : h);
}

static void enter_screen(void) {
    if (tcgetattr(STDIN_FILENO, &original) == 0) {
        struct termios t = original;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        term_saved = true;
    }
    if (alternate) out_str("\033[?1049h");
    out_str("\033[?25l\033[?7l\033[0m\033[2J\033[H");
    out_flush();
}

static void leave_screen(void) {
    out_str("\033[0m\033[?7h\033[?25h");
    if (alternate) out_str("\033[?1049l");
    else out_str("\033[2J\033[H");
    out_flush();
    if (term_saved) tcsetattr(STDIN_FILENO, TCSANOW, &original);
    term_saved = false;
}

bool screen_open(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) return false;
    detect();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_quit;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = on_resize;
    sigaction(SIGWINCH, &sa, NULL);
    active = true;
    atexit(screen_close);
    enter_screen();
    measure();
    return true;
}

void screen_open_headless(int w, int h) {
    if (getenv("PSEUDOSCOPE_FIT")) fit_log = fopen(getenv("PSEUDOSCOPE_FIT"), "a");
    headless = active = true;
    const char *glyphs = getenv("PSEUDOSCOPE_GLYPHS"), *depth = getenv("PSEUDOSCOPE_COLORS");
    if (glyphs && *glyphs) {
        ascii = strcmp(glyphs, "ascii") == 0;
        poor = ascii || strcmp(glyphs, "console") == 0;
    }
    if (depth && *depth) colors = strcmp(depth, "256") == 0 ? 256 : strcmp(depth, "0") == 0 ? 0 : 16;
    allocate(w, h);
}

void screen_close(void) {
    if (!active) return;
    active = false;
    if (!headless) leave_screen();
    free(cur);
    free(prev);
    free(out);
    cur = prev = NULL;
    out = NULL;
    out_length = out_capacity = 0;
}

void screen_suspend(void) {
    if (!headless) leave_screen();
}

void screen_resume(void) {
    if (headless) return;
    enter_screen();
    measure();
}

bool screen_should_quit(void) { return quit; }
int screen_width(void) { return width; }
int screen_height(void) { return height; }

/* ---- keyboard ---- */

static int read_byte(int timeout_ms) {
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    unsigned char c;
    if (poll(&p, 1, timeout_ms) <= 0) return -1;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 0 || (n < 0 && errno != EINTR)) quit = 1; /* the terminal is gone */
    return n == 1 ? c : -1;
}

int screen_key(int timeout_ms) {
    int c = read_byte(timeout_ms);
    if (c < 0) return K_NONE;
    if (c == '\n') return K_ENTER;
    if (c == 8) return K_BACKSPACE;
    if (c != K_ESC) return c;
    int next = read_byte(25);
    if (next < 0) return K_ESC;
    if (next != '[' && next != 'O') return K_NONE; /* Alt and a letter */
    int number = 0;
    for (;;) {
        c = read_byte(25);
        if (c < 0) return K_NONE;
        if (c >= '0' && c <= '9') number = number * 10 + c - '0';
        else if (c == ';') number = 0;
        else break;
    }
    switch (c) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case 'Z': return K_SHIFT_TAB;
        case '~':
            switch (number) {
                case 1: case 7: return K_HOME;
                case 3: return K_DELETE;
                case 4: case 8: return K_END;
                case 5: return K_PAGE_UP;
                case 6: return K_PAGE_DOWN;
                default: return K_NONE;
            }
        default: return K_NONE;
    }
}

/* ---- cells ---- */

int char_bytes(const char *s) {
    unsigned char c = (unsigned char)*s;
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    for (int i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80) return 1;
    return n;
}

static void set_cell(int x, int y, const char *s, int n, Style st) {
    if (x < 0 || y < 0 || x >= width || y >= height || n <= 0 || n > 4) return;
    unsigned char first = (unsigned char)s[0];
    Cell *c = &cur[y * width + x];
    if (n == 1 && (first < 32 || first >= 127)) { /* control characters, broken UTF-8 */
        s = "?";
        if (first == '\t') s = " ";
    }
    memcpy(c->ch, s, (size_t)n);
    c->ch[n] = '\0';
    c->st = st;
}

void frame_begin(void) {
    fit_frame++;
    if (resized) {
        resized = 0;
        measure();
        out_str("\033[0m\033[2J");
    }
    for (int i = 0; i < width * height; i++) {
        cur[i].ch[0] = ' ';
        cur[i].ch[1] = '\0';
        cur[i].st = S(C_DEFAULT, C_DEFAULT, 0);
    }
}

/* ---- poorer terminals ---- */

/* Glyphs for the Linux console (code page 437) and, last, for terminals that
 * only know ASCII. */
static const char *poor_glyph(const char *ch) {
    static const char *const TABLE[][3] = {
        {"╭", "┌", "+"}, {"╮", "┐", "+"}, {"╰", "└", "+"}, {"╯", "┘", "+"}, {"─", "─", "-"}, {"│", "│", "|"},
        {"├", "├", "+"}, {"└", "└", "`"}, {"┄", "·", "."}, {"╷", "│", "|"}, {"━", "═", "="}, {"┃", "║", "#"},
        {"▶", "►", ">"}, {"◀", "◄", "<"}, {"▸", "►", ">"}, {"▷", "►", ">"}, {"‹", "<", "<"}, {"›", ">", ">"},
        {"▼", "▼", "v"}, {"▲", "▲", "^"}, {"→", "→", ">"}, {"←", "←", "<"}, {"↑", "↑", "^"}, {"↓", "↓", "v"},
        {"⇒", "→", ">"}, {"↻", "»", "@"}, {"↺", "«", "@"}, {"✓", "√", "v"}, {"✗", "x", "x"}, {"✎", "=", "="},
        {"…", "~", "~"}, {"●", "•", "*"}, {"◉", "◙", "@"}, {"◆", "♦", "*"}, {"◇", "♦", "?"}, {"◷", "○", "o"},
        {"■", "■", "#"}, {"▪", "■", "*"}, {"▣", "■", "#"}, {"▦", "▒", "#"}, {"ƒ", "f", "f"}, {"∅", "0", "0"},
        {"⊘", "ø", "/"}, {"≡", "≡", "="}, {"»", "»", ">"}, {"«", "«", "<"}, {"‖", "║", "="}, {"▮", "■", "#"},
        {"▁", "_", "_"}, {"▂", "_", "_"}, {"▃", "▄", "."}, {"▄", "▄", "o"}, {"▅", "▄", "o"}, {"▆", "█", "O"},
        {"▇", "█", "O"}, {"█", "█", "#"}, {"▐", " ", " "}, {"▌", " ", " "}, {"·", "·", "."}, {"⚠", "!", "!"},
        {"≤", "≤", "<"}, {"≥", "≥", ">"}, {"▤", "≡", "="}, {"▀", "▀", "#"}, {"à", "à", "a"}, {"è", "è", "e"}, {"é", "é", "e"}, {"ì", "ì", "i"},
        {"ò", "ò", "o"}, {"ù", "ù", "u"}, {"È", "E", "E"}, {"À", "A", "A"},
    };
    if ((unsigned char)ch[0] < 0x80) return ch;
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (strcmp(ch, TABLE[i][0]) == 0) return ascii ? TABLE[i][2] : TABLE[i][1];
    return ascii ? "?" : ch;
}

/* The nearest of 16 colours; those of the interface are chosen by hand, to
 * remain distinguishable. */
static int color16(int c, bool background) {
    static const short MAP[][3] = { /* colour, foreground, background */
        {255, 15, 7}, {252, 7, 7}, {246, 7, 0}, {240, 8, 0}, {235, 8, 0}, {237, 8, 4}, {238, 7, 0},
        {75, 12, 4}, {25, 12, 4}, {114, 10, 2}, {28, 10, 2}, {221, 11, 3}, {178, 11, 3},
        {177, 13, 5}, {97, 13, 5}, {117, 14, 6}, {31, 14, 6}, {209, 9, 1}, {203, 9, 1}, {160, 9, 1},
        {150, 10, 2}, {180, 3, 3}, {16, 0, 0}, {231, 15, 7}, {213, 13, 5}, {250, 7, 7}, {111, 12, 4},
        {183, 13, 5}, {58, 11, 3}, {52, 9, 1}, {130, 3, 3}, {125, 5, 5}, {64, 2, 2}, {60, 4, 4},
    };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        if (MAP[i][0] == c) return MAP[i][background ? 2 : 1];
    return background ? 0 : 7;
}

static void emit_style(Style s) {
    char b[64];
    int n = snprintf(b, sizeof b, "\033[0");
    if (colors == 0) { /* bold, and reverse for what stands out */
        bool strong = s.bg >= 0 && s.bg != C_PANEL && s.bg != C_CELL;
        if (s.attr & A_BOLD) n += snprintf(b + n, sizeof b - (size_t)n, ";1");
        if (s.attr & A_UNDER) n += snprintf(b + n, sizeof b - (size_t)n, ";4");
        if (strong) n += snprintf(b + n, sizeof b - (size_t)n, ";7");
        out_add(b, (size_t)n);
        out_add("m", 1);
        return;
    }
    if (s.attr & A_BOLD) n += snprintf(b + n, sizeof b - (size_t)n, ";1");
    if (colors == 16) {
        int bg = s.bg >= 0 ? color16(s.bg, true) : -1, fg = s.fg >= 0 ? color16(s.fg, false) : -1;
        if (bg >= 0 && (fg == bg || fg == 8 || (bg != 0 && fg > 8))) fg = bg == 3 || bg == 6 || bg == 7 || bg == 2 ? 0 : 15;
        if (fg >= 0) n += snprintf(b + n, sizeof b - (size_t)n, ";%d", fg < 8 ? 30 + fg : 82 + fg);
        if (bg >= 0) n += snprintf(b + n, sizeof b - (size_t)n, ";%d", 40 + bg);
    } else {
        if (s.attr & A_DIM) n += snprintf(b + n, sizeof b - (size_t)n, ";2");
        if (s.attr & A_ITALIC) n += snprintf(b + n, sizeof b - (size_t)n, ";3");
        if (s.attr & A_UNDER) n += snprintf(b + n, sizeof b - (size_t)n, ";4");
        if (s.fg >= 0) n += snprintf(b + n, sizeof b - (size_t)n, ";38;5;%d", s.fg);
        if (s.bg >= 0) n += snprintf(b + n, sizeof b - (size_t)n, ";48;5;%d", s.bg);
    }
    out_add(b, (size_t)n);
    out_add("m", 1);
}

static bool has(const char *text, const char *part) { return text && strstr(text, part) != NULL; }

/* What the terminal can do, from the environment; PSEUDOSCOPE_COLORS (256, 16,
 * 0) and PSEUDOSCOPE_GLYPHS (unicode, console, ascii) decide instead. */
static void detect(void) {
    const char *term = getenv("TERM"), *color = getenv("COLORTERM");
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    bool utf8 = has(locale, "UTF-8") || has(locale, "utf8") || has(locale, "utf-8") || has(locale, "UTF8");
    linux_console = term && strncmp(term, "linux", 5) == 0;
    alternate = !linux_console && term && strncmp(term, "vt", 2) != 0 && strcmp(term, "ansi") != 0 && strcmp(term, "dumb") != 0;

    colors = 16;
    if (has(term, "256color") || (color && *color) || has(term, "kitty") || has(term, "alacritty") || has(term, "foot") ||
        has(term, "ghostty") || has(term, "wezterm"))
        colors = 256;
    if (linux_console) colors = 16;
    if (!term || !*term || strncmp(term, "vt", 2) == 0 || strcmp(term, "dumb") == 0 || strcmp(term, "ansi-m") == 0) colors = 0;
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) colors = 0;
    ascii = !utf8;
    poor = ascii || linux_console;

    const char *wanted = getenv("PSEUDOSCOPE_COLORS");
    if (wanted && *wanted) colors = strcmp(wanted, "256") == 0 ? 256 : strcmp(wanted, "0") == 0 ? 0 : 16;
    wanted = getenv("PSEUDOSCOPE_GLYPHS");
    if (wanted && *wanted) {
        ascii = strcmp(wanted, "ascii") == 0;
        poor = ascii || strcmp(wanted, "console") == 0;
    }
}

static bool same_style(Style a, Style b) { return a.fg == b.fg && a.bg == b.bg && a.attr == b.attr; }

void frame_end(void) {
    if (headless) return;
    Style now = S(-2, -2, 255);
    int cx = -1, cy = -1;
    bool any = false;
    char b[32];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Cell *c = &cur[y * width + x], *p = &prev[y * width + x];
            if (!full_redraw && same_style(c->st, p->st) && strcmp(c->ch, p->ch) == 0) continue;
            if (!any) {
                if (colors == 256 && !poor) out_str("\033[?2026h"); /* one update, where supported */
                any = true;
            }
            if (cx != x || cy != y) {
                snprintf(b, sizeof b, "\033[%d;%dH", y + 1, x + 1);
                out_str(b);
            }
            if (!same_style(c->st, now)) {
                emit_style(c->st);
                now = c->st;
            }
            out_str(poor ? poor_glyph(c->ch) : c->ch);
            *p = *c;
            cx = x + 1;
            cy = y;
        }
    }
    if (any) {
        out_str("\033[0m");
        if (colors == 256 && !poor) out_str("\033[?2026l");
        out_flush();
    }
    full_redraw = false;
}

void frame_dump(bool styled) {
    for (int y = 0; y < height; y++) {
        Style now = S(-2, -2, 255);
        int last = width;
        if (!styled)
            while (last > 0 && strcmp(cur[y * width + last - 1].ch, " ") == 0) last--;
        for (int x = 0; x < last; x++) {
            Cell *c = &cur[y * width + x];
            if (styled && !same_style(c->st, now)) {
                emit_style(c->st);
                now = c->st;
            }
            out_str(poor ? poor_glyph(c->ch) : c->ch);
        }
        out_str(styled ? "\033[0m\n" : "\n");
    }
    out_flush();
}

/* ---- text ---- */

static Style markup_style(char code, Style now, Style base) {
    switch (code) {
        case '0': return base;
        case 'b': now.attr |= A_BOLD; break;
        case 'm': now.fg = C_DIM; break;
        case 'n': now.fg = C_FAINT; break;
        case 'w': now.fg = C_TEXT; break;
        case 'r': now.fg = C_READ; break;
        case 'g': now.fg = C_WRITE; break;
        case 'y': now.fg = C_FOCUS; break;
        case 'c': now.fg = C_CALL; break;
        case 't': now.fg = C_TYPE; break;
        case 'p': now.fg = C_POINTER; break;
        case 'e': now.fg = C_ERROR; break;
        case 'R': now = S(231, C_READ_BG, A_BOLD); break;
        case 'G': now = S(231, C_WRITE_BG, A_BOLD); break;
        case 'Y': now = S(16, C_FOCUS_BG, A_BOLD); break;
        case 'C': now = S(231, C_CALL_BG, A_BOLD); break;
        case 'T': now = S(231, C_TYPE_BG, A_BOLD); break;
        case 'E': now = S(231, C_ERROR_BG, A_BOLD); break;
        case 'O': now = S(C_DIM, C_SELECT, 0); break;
        case 'K': now = S(16, 250, A_BOLD); break;
        default: break;
    }
    return now;
}

int text_width(const char *s) {
    int w = 0;
    while (*s) {
        if (*s == '\001') {
            if (!s[1]) break;
            s += 2;
            continue;
        }
        s += char_bytes(s);
        w++;
    }
    return w;
}

void fit_quiet(bool quiet) { fit_silent = quiet; }

void fit_note(const char *what, int x, int y, int room, int needed, const char *text) {
    if (!fit_log || fit_silent) return;
    fprintf(fit_log, "%s\t%d\t%d\t%d\t%d\t%d\t", what, fit_frame, x, y, room, needed);
    for (const char *s = text; *s; s++) {
        if (*s == '\001') {
            if (s[1]) s++;
            continue;
        }
        fputc(*s == '\n' || *s == '\t' ? ' ' : *s, fit_log);
    }
    fputc('\n', fit_log);
    fflush(fit_log);
}

static int draw_markup(int x, int y, int max, Style base, const char *s) {
    int total = text_width(s);
    bool cut = max >= 0 && total > max;
    if (cut) fit_note("cut", x, y, max, total, s);
    else if (y >= 0 && y < height && total > 0 && x + total > width) fit_note("off", x, y, width - x, total, s);
    int limit = cut ? max - 1 : total, col = 0;
    Style now = base;
    while (*s && col < limit) {
        if (*s == '\001') {
            if (!s[1]) break;
            now = markup_style(s[1], now, base);
            s += 2;
            continue;
        }
        int n = char_bytes(s);
        set_cell(x + col++, y, s, n, now);
        s += n;
    }
    if (cut && max > 0) set_cell(x + col++, y, "…", 3, now);
    return col;
}

static int word_width(const char *s) {
    int w = 0;
    while (*s && *s != ' ') {
        if (*s == '\001') {
            if (!s[1]) break;
            s += 2;
            continue;
        }
        s += char_bytes(s);
        w++;
    }
    return w;
}

void put_text(int x, int y, Style st, const char *s) {
    while (*s) {
        int n = char_bytes(s);
        set_cell(x++, y, s, n, st);
        s += n;
    }
}

#define FORMAT(buffer)                                                                             \
    char buffer[4096];                                                                             \
    va_list args;                                                                                  \
    va_start(args, format);                                                                        \
    vsnprintf(buffer, sizeof buffer, format, args);                                                \
    va_end(args)

int print_at(int x, int y, int max, Style base, const char *format, ...) {
    FORMAT(text);
    return draw_markup(x, y, max, base, text);
}

int print_centered(int x, int y, int w, Style base, const char *format, ...) {
    FORMAT(text);
    int used = text_width(text);
    if (used < w) x += (w - used) / 2;
    return draw_markup(x, y, w, base, text);
}

int print_wrapped(int x, int y, int w, int max_lines, Style base, const char *format, ...) {
    FORMAT(text);
    Style now = base;
    int line = 0, col = 0;
    bool lost = false;
    if (w <= 0) return 0;
    for (const char *s = text; *s;) {
        if (*s == '\001') {
            if (!s[1]) break;
            now = markup_style(s[1], now, base);
            s += 2;
            continue;
        }
        if (*s == '\n') {
            line++;
            col = 0;
            s++;
            continue;
        }
        if (*s == ' ' && col > 0) {
            int next = word_width(s + 1);
            if (col + 1 + next > w && next <= w) {
                line++;
                col = 0;
                s++;
                continue;
            }
        }
        if (col >= w) {
            line++;
            col = 0;
            if (*s == ' ') {
                s++;
                continue;
            }
        }
        int n = char_bytes(s);
        if (line < max_lines) set_cell(x + col, y + line, s, n, now);
        else lost = true;
        s += n;
        col++;
    }
    if (lost) fit_note("lost", x, y, w * max_lines, text_width(text), text);
    return line + 1 < max_lines ? line + 1 : max_lines;
}

/* ---- shapes ---- */

void fill(int x, int y, int w, int h, Style st) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) set_cell(x + i, y + j, " ", 1, st);
}

void hline(int x, int y, int w, Style st, const char *glyph) {
    for (int i = 0; i < w; i++) set_cell(x + i, y, glyph, char_bytes(glyph), st);
}

void vline(int x, int y, int h, Style st, const char *glyph) {
    for (int i = 0; i < h; i++) set_cell(x, y + i, glyph, char_bytes(glyph), st);
}

void draw_box(int x, int y, int w, int h, short color, const char *title) {
    if (w < 2 || h < 2) return;
    Style st = S(color, C_DEFAULT, 0);
    put_text(x, y, st, "╭");
    hline(x + 1, y, w - 2, st, "─");
    put_text(x + w - 1, y, st, "╮");
    vline(x, y + 1, h - 2, st, "│");
    vline(x + w - 1, y + 1, h - 2, st, "│");
    put_text(x, y + h - 1, st, "╰");
    hline(x + 1, y + h - 1, w - 2, st, "─");
    put_text(x + w - 1, y + h - 1, st, "╯");
    if (title && *title) print_at(x + 2, y, w - 4, S(C_TEXT, C_DEFAULT, A_BOLD), " %s ", title);
}

void draw_pill(int x, int y, int w, const char *text, bool on, short background) {
    short bg = on ? background : C_PANEL;
    Style inner = on ? S(16, bg, A_BOLD) : S(C_DIM, bg, 0);
    if (w < 3) return;
    put_text(x, y, S(bg, C_DEFAULT, 0), "▐");
    fill(x + 1, y, w - 2, 1, inner);
    print_centered(x + 1, y, w - 2, inner, "%s", text);
    put_text(x + w - 1, y, S(bg, C_DEFAULT, 0), "▌");
}

void draw_scrollbar(int x, int y, int h, int first, int shown, int total) {
    if (total <= shown || h < 2) return;
    int size = h * shown / total;
    if (size < 1) size = 1;
    int at = (h - size) * first / (total - shown);
    for (int i = 0; i < h; i++)
        if (i >= at && i < at + size) put_text(x, y + i, S(C_DIM, C_DEFAULT, 0), "┃");
}
