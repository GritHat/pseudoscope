#ifndef PSEUDOSCOPE_UI_H
#define PSEUDOSCOPE_UI_H

#include "pseudoscope/codegen.h"
#include "pseudoscope/i18n.h"
#include "pseudoscope/uml.h"
#include "pseudoscope/blocks.h"
#include "pseudoscope/target.h"
#include "pseudoscope/vm.h"
#include "screen.h"

#include <time.h>


#define MIN_WIDTH 80
#define MIN_HEIGHT 24

/* ---- a source file ---- */

enum { H_PLAIN, H_KEYWORD, H_TYPE, H_NUMBER, H_STRING, H_COMMENT, H_FUNCTION, H_RECORD };

typedef struct {
    Loc loc;
    bool warning;
    char text[200];
} Message;

typedef enum { ITEM_COMPONENT, ITEM_CLASS, ITEM_FUNCTION, ITEM_RECORD, ITEM_GLOBALS, ITEM_TEST } ItemKind;

typedef struct {
    ItemKind kind;
    const char *name;
    const Function *function;
    const Record *record;
    const Class *klass;
    int first, last;         /* source lines */
    const Function **calls;  /* functions: who it calls */
    int call_count;
} Item;

typedef struct {
    char *path;              /* as asked: "notes.md#2" is the second block of a document */
    char *file;              /* the file itself */
    int top_line;            /* where what matters begins */
    StrBuf text;
    Source source;
    Config config;
    Arena arena;
    TokenList tokens;
    Program *program;
    Uml *uml;                /* a diagram instead of a program */
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

/* The states a component can go to: from, label, to. */
typedef struct {
    int from, to;
    const char *label;
} StateArrow;
int class_transitions(const Document *doc, const Class *klass, StateArrow *out, int max);

/* ---- diagrams ---- */

typedef enum { SEQ_MESSAGE, SEQ_RETURN, SEQ_LOST, SEQ_IGNORED, SEQ_STATE, SEQ_STEP, SEQ_CHECK, SEQ_NOTE, SEQ_FRAME } SequenceKind;

typedef struct {
    SequenceKind kind;
    int from, to;
    const char *label;
    long time;       /* -1: none */
    bool dashed, ok, activates;
    int end;         /* row at which the activation ends */
} SequenceItem;

typedef struct {
    int from, to;
    const char *label;
    double progress;
    long arrives;
} SequenceFlying;

typedef struct {
    const char **lives;
    const bool *kinds;  /* components, drawn in another colour; or NULL */
    int life_count, current;
    const SequenceItem *rows;
    int row_count, back;
    const SequenceFlying *flying;
    int flying_count;
} SequenceView;

void draw_sequence(int x, int y, int w, int h, const SequenceView *view);

typedef struct {
    const char **states;
    int state_count, current;
    const StateArrow *arrows;
    int arrow_count, last, selected;
} StateView;

void draw_states(int x, int y, int w, int h, const StateView *view);

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

typedef enum { VIEW_FILES, VIEW_EXPLORE, VIEW_TRIAL, VIEW_RUN, VIEW_TESTS, VIEW_UML } View;
typedef enum { OVERLAY_NONE, OVERLAY_HELP, OVERLAY_OUTPUT, OVERLAY_LOG, OVERLAY_CANVAS } Overlay;

typedef struct {
    char *name;
    bool directory;
    char *target;            /* what it opens, when it is not a file of the folder */
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

typedef enum { SYS_SEQUENCE, SYS_COMPONENTS, SYS_STATES, SYS_TIMING, SYS_COMMUNICATION, SYS_OBJECTS, SYS_CHECKS, SYS_VIEWS } SystemView;

typedef struct {
    const Function *test;
    bool ran, ok;
    int checks, failed;
    char message[160];
    Check items[64];
} TestResult;

typedef struct {
    TestResult *results;
    int count, selected;
} Tests;

typedef struct {
    int at;              /* sequence: rows shown; graphs: the node we are in */
    int choice;          /* branch of the next alternative; arrow to follow */
    int selected, top;
    int history[256], arrows[256], depth;
    bool playing;
    long long next_tick;
    unsigned branches;   /* sequence: the branch taken in each alternative */
} UmlWalk;

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
    const Function *test;
    SystemView system;
    int system_back, system_life;
    bool activity;           /* the function as a flow instead of its text */
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
    Tests tests;
    UmlWalk walk;
    char notice[160];
    Lang target;             /* the language translations are shown in: T changes it */
    char problem[240];       /* why the program cannot be said in it; empty if it can */
    const Document *problem_doc;
    Lang problem_target;
} App;

/* Why what is open cannot be translated to the chosen language, or NULL. */
const char *translation_problem(App *app);

void files_open(App *app, const char *directory);
/* Back to where a document was chosen from. */
void files_back(App *app, const char *path);
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
bool run_open_test(App *app, const Function *test);
void run_close(App *app);
void run_draw(App *app);
void run_key(App *app, int key);
int run_tick(App *app);  /* milliseconds to the next one */

void run_data_draw(App *app, int x, int y, int w, int h);
void run_memory_draw(App *app, int x, int y, int w, int h);
int run_tints(App *app, const Function *function, Tint *out, int max);

void run_system_draw(App *app, int x, int y, int w, int h);
bool run_system_has(const App *app, SystemView view);

void draw_timing(App *app, int x, int y, int w, int h);
void draw_communication(App *app, int x, int y, int w, int h);
void draw_objects(App *app, int x, int y, int w, int h);
void draw_activity(App *app, const Function *function, int x, int y, int w, int h);

void tests_open(App *app);
void tests_draw(App *app);
void tests_key(App *app, int key);
void tests_close(App *app);

void draw_uml_plan(App *app, int x, int y, int w, int h);
void draw_uml_tree(App *app, int x, int y, int w, int h);
void draw_uml_pie(App *app, int x, int y, int w, int h);
long plan_step(const Uml *uml, long day, int direction);
const char *uml_kind_name(const Uml *uml);
void uml_draw(App *app);
void uml_key(App *app, int key);
int uml_tick(App *app);      /* as run_tick */

long long now_ms(void);

#endif
