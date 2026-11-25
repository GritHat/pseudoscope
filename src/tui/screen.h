#ifndef PLIKE_SCREEN_H
#define PLIKE_SCREEN_H

/* Terminal graphics: a buffer of cells, of which only the changed ones are
 * sent. Taken from the interface of reliable-over-unreliable.
 *
 * Text may contain the M_* codes below to change colour. */

#include <stdbool.h>
#include <stddef.h>

/* colours of the 256-colour palette */
#define C_DEFAULT (-1)
#define C_BRIGHT 255
#define C_TEXT 252
#define C_DIM 246
#define C_FAINT 240
#define C_BORDER 240
#define C_PANEL 235
#define C_SELECT 237
#define C_CELL 238

/* meanings, the same everywhere */
#define C_READ 75      /* blue: being read */
#define C_READ_BG 25
#define C_WRITE 114    /* green: just written, results */
#define C_WRITE_BG 28
#define C_FOCUS 221    /* yellow: here, now, true or false */
#define C_FOCUS_BG 178
#define C_CALL 177     /* violet: procedures, functions, calls */
#define C_CALL_BG 97
#define C_TYPE 117     /* sky: types and records */
#define C_TYPE_BG 31
#define C_POINTER 209  /* orange: pointers */
#define C_ERROR 203
#define C_ERROR_BG 160
#define C_STRING 150
#define C_NUMBER 180

#define A_BOLD 1u
#define A_DIM 2u
#define A_ITALIC 4u
#define A_UNDER 8u

typedef struct {
    short fg, bg;
    unsigned char attr;
} Style;

#define S(fg, bg, attr) ((Style){(short)(fg), (short)(bg), (unsigned char)(attr)})
#define S_TEXT S(C_TEXT, C_DEFAULT, 0)
#define S_DIM S(C_DIM, C_DEFAULT, 0)
#define S_FAINT S(C_FAINT, C_DEFAULT, 0)
#define S_BOLD S(C_BRIGHT, C_DEFAULT, A_BOLD)

#define M_RESET "\001" "0"
#define M_BOLD "\001" "b"
#define M_DIM "\001" "m"
#define M_FAINT "\001" "n"
#define M_TEXT "\001" "w"
#define M_READ "\001" "r"
#define M_WRITE "\001" "g"
#define M_FOCUS "\001" "y"
#define M_CALL "\001" "c"
#define M_TYPE "\001" "t"
#define M_POINTER "\001" "p"
#define M_ERROR "\001" "e"
#define M_CHIP_READ "\001" "R"
#define M_CHIP_WRITE "\001" "G"
#define M_CHIP_FOCUS "\001" "Y"
#define M_CHIP_CALL "\001" "C"
#define M_CHIP_TYPE "\001" "T"
#define M_CHIP_ERROR "\001" "E"
#define M_CHIP_OFF "\001" "O"
#define M_KEY "\001" "K"

enum {
    K_NONE = -1, K_ENTER = 13, K_TAB = 9, K_ESC = 27, K_BACKSPACE = 127,
    K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PAGE_UP, K_PAGE_DOWN, K_DELETE,
    K_SHIFT_TAB
};

bool screen_open(void);                       /* false if not on a terminal */
void screen_open_headless(int width, int height);
void screen_close(void);
void screen_suspend(void);                    /* before running another program */
void screen_resume(void);
bool screen_should_quit(void);
int screen_key(int timeout_ms);
int screen_width(void);
int screen_height(void);

void frame_begin(void);
void frame_end(void);
void frame_dump(bool styled);                 /* headless: to the standard output */

void put_text(int x, int y, Style st, const char *utf8);
int print_at(int x, int y, int max, Style base, const char *format, ...)
    __attribute__((format(printf, 5, 6)));
int print_centered(int x, int y, int w, Style base, const char *format, ...)
    __attribute__((format(printf, 5, 6)));
int print_wrapped(int x, int y, int w, int max_lines, Style base, const char *format, ...)
    __attribute__((format(printf, 6, 7)));    /* returns the lines used */
int text_width(const char *markup);
void fill(int x, int y, int w, int h, Style st);
void hline(int x, int y, int w, Style st, const char *glyph);
void vline(int x, int y, int h, Style st, const char *glyph);
void draw_box(int x, int y, int w, int h, short color, const char *title);
void draw_pill(int x, int y, int w, const char *text, bool active, short background);
void draw_scrollbar(int x, int y, int h, int first, int shown, int total);
int char_bytes(const char *utf8);

#endif
