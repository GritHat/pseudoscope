#ifndef PLIKE_UI_H
#define PLIKE_UI_H

#include "plike/codegen.h"
#include "plike/vm.h"
#include "screen.h"

#include <time.h>

extern bool ui_italian;
#define L(it, en) (ui_italian ? (it) : (en))

#define MIN_WIDTH 80
#define MIN_HEIGHT 24

/* ---- a source file ---- */

enum { H_PLAIN, H_KEYWORD, H_TYPE, H_NUMBER, H_STRING, H_COMMENT, H_FUNCTION, H_RECORD };

typedef struct {
    Loc loc;
    bool warning;
    char text[200];
} Message;

typedef enum { ITEM_FUNCTION, ITEM_RECORD, ITEM_GLOBALS } ItemKind;

typedef struct {
    ItemKind kind;
    const char *name;
    const Function *function;
    const Record *record;
    int first, last;         /* source lines */
    const Function **calls;  /* functions: who it calls */
    int call_count;
} Item;

typedef struct {
    char *path;
    StrBuf text;
    Source source;
    Config config;
    Arena arena;
    TokenList tokens;
    Program *program;
    bool ok;
    Message messages[32];
    int message_count;
    int *lines;              /* offset of each line; lines[0] is the first */
    int line_count;
    unsigned char *kinds;    /* highlighting of each byte */
    Item *items;
    int item_count;
    time_t modified;
    int main_item;           /* index of main, or -1 */
} Document;

Document *document_load(const char *path, const Config *config);
void document_free(Document *doc);
bool document_changed(const Document *doc);
const Item *document_function(const Document *doc, const Function *function);
Style highlight_style(int kind);

typedef void (*ExprVisitor)(void *context, const Expr *expr);
void visit_statements(const Stmt *stmt, ExprVisitor visit, void *context);
void visit_expr(const Expr *expr, ExprVisitor visit, void *context);

/* "in A: array [1..n] of real", as the source would say it. */
void describe_type(const Document *doc, const Type *type, char *out, size_t size);

/* ---- widgets ---- */

/* Colours given to the variables used as indices, so that the same variable
 * looks the same in the code and over the arrays. */
typedef struct {
    const Symbol *symbol;
    short color;
} Tint;

typedef struct {
    const Document *doc;
    int first, last;        /* lines that may be shown */
    int top;                /* first one shown */
    int current;            /* marked line, 0 for none */
    short current_bg;
    const bool *breakpoints;/* per line, or NULL */
    const Tint *tints;
    int tint_count;
} CodeView;

void draw_code(int x, int y, int w, int h, const CodeView *view);
int scroll_to(int top, int line, int first, int last, int h);

void draw_c_code(int x, int y, int w, int h, const char *text, int top, int hot_first, int hot_last);

typedef struct {
    const char *key, *label;
} Hint;
void draw_footer(const Hint *hints, int count);
void draw_header(const char *where, const char *const *levels, int level_count, int level,
                 short color);
void draw_table_row(int x, int y, int w, bool selected, const char *format, ...)
    __attribute__((format(printf, 5, 6)));

/* ---- the application ---- */

typedef enum { VIEW_FILES, VIEW_EXPLORE, VIEW_TRIAL, VIEW_RUN } View;
typedef enum { OVERLAY_NONE, OVERLAY_HELP, OVERLAY_OUTPUT, OVERLAY_LOG } Overlay;

typedef struct {
    char *name;
    bool directory;
} Entry;

typedef struct {
    char *directory;
    Entry *entries;
    int count, selected, top;
    Document *preview;
    char *preview_path;
} Files;

typedef struct {
    int selected, top;
    int level;      /* 0 map, 1 code, 2 C */
    int line, code_top;
} Explore;

#define TRIAL_TEXT 400

typedef struct {
    const Function *function;
    char (*text)[TRIAL_TEXT];
    int *preset;
    int count, selected;
    char error[120];
} Trial;

typedef enum { GOAL_NONE, GOAL_STOP, GOAL_OVER, GOAL_OUT, GOAL_END } Goal;

typedef struct {
    Vm *vm;
    Level level;
    bool playing;
    int speed;
    long long next_tick;
    Goal goal;
    int goal_depth;
    bool *breakpoints;
    const Function *trial;
    TrialArgument *args;
    int arg_count;
    int selected_var;
    bool bars;
    int overlay_top;
    char input[128];
    LineMap map;
    StrBuf c_code;
    const Function *c_function;
    Arena c_arena;
} Run;

typedef struct {
    Config config;
    View view;
    Overlay overlay;
    bool quit;
    bool headless;
    Document *doc;
    Files files;
    Explore explore;
    Trial trial;
    Run run;
    char notice[160];
} App;

void files_open(App *app, const char *directory);
void files_draw(App *app);
void files_key(App *app, int key);
void files_close(App *app);

void explore_open(App *app, const char *path);
void explore_reload(App *app);
void explore_draw(App *app);
void explore_key(App *app, int key);

void trial_open(App *app, const Function *function);
void trial_draw(App *app);
void trial_key(App *app, int key);
void trial_close(App *app);

bool run_open(App *app, const Function *trial, TrialArgument *args, int count);
void run_close(App *app);
void run_draw(App *app);
void run_key(App *app, int key);
int run_tick(App *app);  /* milliseconds to the next one */

void run_data_draw(App *app, int x, int y, int w, int h);
void run_memory_draw(App *app, int x, int y, int w, int h);
int run_tints(App *app, const Function *function, Tint *out, int max);

long long now_ms(void);

#endif
