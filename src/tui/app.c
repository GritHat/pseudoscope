#include "ui.h"

#include "plike/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

bool ui_italian = true;

long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- help ---- */

typedef struct {
    const char *key, *it, *en;
} Help;

static int help_column(const Help *rows, int count, int x, int y, int w) {
    for (int i = 0; i < count; i++) {
        if (!rows[i].key) {
            print_at(x, y + i, w, S_BOLD, "%s", L(rows[i].it, rows[i].en));
            continue;
        }
        print_at(x, y + i, 10, S(C_FOCUS, C_DEFAULT, A_BOLD), "%s", rows[i].key);
        print_at(x + 11, y + i, w - 11, S_TEXT, "%s", L(rows[i].it, rows[i].en));
    }
    return y + count;
}

static void draw_help(const App *app) {
    int w = screen_width(), h = screen_height(), half = w / 2;
    draw_header(L("Aiuto", "Help"), NULL, 0, 0, 0);
    draw_box(0, 2, half, h - 4, C_BORDER, L("Tasti", "Keys"));
    static const Help RUN[] = {
        {"→ ←", "un passo avanti, indietro", "one step forward, one back"},
        {"Spazio", "va da solo / si ferma", "plays / pauses"},
        {"↑ ↓", "più veloce, più lento", "faster, slower"},
        {"n", "salta la chiamata", "steps over the call"},
        {"u", "esce dal sottoprogramma", "out of the subprogram"},
        {"g", "fino alla fine (o a ●)", "to the end (or to ●)"},
        {"0", "ricomincia", "starts again"},
        {"b", "punto di arresto ●", "breakpoint ● on the line"},
        {"Invio", "più dettaglio", "more detail"},
        {"Esc", "meno dettaglio", "less detail"},
        {"1 … 5", "direttamente a un livello", "straight to a level"},
        {"Tab", "altra vista o variabile", "other view or variable"},
        {"m", "altro componente (Stati)", "other component (States)"},
        {"f", "codice o diagramma di attività", "code or activity diagram"},
        {"v", "array come celle o barre", "arrays as cells or bars"},
        {"o", "cosa è stato stampato", "what was printed"},
        {"c", "tutta la storia", "the whole story"},
        {"x", "torna a esplorare", "back to exploring"},
        {"q", "esci", "quit"},
    };
    static const Help EXPLORE[] = {
        {"↑ ↓", "scorri", "move"},
        {"Invio", "più dettaglio", "more detail"},
        {"Esc", "meno dettaglio", "less detail"},
        {"r", "esegui, o prova", "run, or try"},
        {"t", "tutte le prove (test)", "all the tests"},
        {"E", "apri nell'editor", "open in the editor"},
        {"q", "esci", "quit"},
        {NULL, "", ""},
        {NULL, "Livelli: più scendi, più vedi", "Levels: the deeper, the more"},
        {"Mappa", "cosa c'è nel programma", "what the program contains"},
        {"Codice", "com'è scritto", "how it is written"},
        {"C", "come diventa C, e in memoria", "how it becomes C, and memory"},
    };
    if (app->view == VIEW_RUN) help_column(RUN, sizeof RUN / sizeof RUN[0], 2, 3, half - 4);
    else help_column(EXPLORE, sizeof EXPLORE / sizeof EXPLORE[0], 2, 3, half - 4);

    draw_box(half, 2, w - half, h - 4, C_BORDER, L("Colori", "Colours"));
    static const struct {
        const char *chip, *it, *en;
    } COLORS[] = {
        {M_CHIP_READ " 5 ", "letto adesso", "being read"},
        {M_CHIP_WRITE " 5 ", "appena scritto, risultato", "just written, result"},
        {M_CHIP_FOCUS " 5 ", "il punto in cui siamo", "where we are"},
        {M_CALL M_BOLD "sort", "procedure e funzioni", "procedures and functions"},
        {M_TYPE M_BOLD "Node", "tipi e record", "types and records"},
        {M_POINTER M_BOLD "─▶ ", "puntatori", "pointers"},
        {M_CHIP_ERROR " ✗ ", "errore, falso", "error, false"},
        {M_FAINT " · ", "non ha ancora un valore", "no value yet"},
    };
    for (size_t i = 0; i < sizeof COLORS / sizeof COLORS[0]; i++) {
        print_at(half + 3, 4 + (int)i, 8, S_TEXT, "%s", COLORS[i].chip);
        print_at(half + 12, 4 + (int)i, w - half - 14, S_TEXT, "%s", L(COLORS[i].it, COLORS[i].en));
    }
    if (app->view == VIEW_RUN && h > 22) {
        static const Help LEVELS[] = {
            {NULL, "Livello: cosa vedi, e il passo", "Level: what you see, and the step"},
            {"1", "Sistema: messaggi", "System: a message"},
            {"2", "Programma: chiamate", "Program: a call"},
            {"3", "Funzione: istruzioni", "Function: a statement"},
            {"4", "Istruzione: calcoli", "Statement: a piece of it"},
            {"5", "Memoria: letture, scritture", "Memory: a read or a write"},
        };
        help_column(LEVELS, 6, half + 3, 14, w - half - 5);
    }
    const Hint hints[] = {{L("un tasto", "any key"), L("chiudi", "close")}};
    draw_footer(hints, 1);
}

/* ---- main loop ---- */

static void draw(App *app) {
    frame_begin();
    int w = screen_width(), h = screen_height();
    if (w < MIN_WIDTH || h < MIN_HEIGHT) {
        print_centered(0, h / 2 - 1, w, S(C_FOCUS, C_DEFAULT, A_BOLD), "%s", L("Finestra troppo piccola", "Window too small"));
        print_centered(0, h / 2 + 1, w, S_DIM, L("servono almeno %dx%d caratteri (ora %dx%d)", "at least %dx%d characters are needed (now %dx%d)"),
                       MIN_WIDTH, MIN_HEIGHT, w, h);
    } else if (app->overlay == OVERLAY_HELP) {
        draw_help(app);
    } else {
        switch (app->view) {
            case VIEW_FILES: files_draw(app); break;
            case VIEW_EXPLORE: explore_draw(app); break;
            case VIEW_TRIAL: trial_draw(app); break;
            case VIEW_RUN: run_draw(app); break;
            case VIEW_TESTS: tests_draw(app); break;
            case VIEW_UML: uml_draw(app); break;
        }
        if (app->notice[0])
            print_centered(0, 1, w, S(C_FOCUS, C_DEFAULT, A_BOLD), " %s ", app->notice);
    }
    frame_end();
}

static void edit(App *app) {
    const char *editor = getenv("VISUAL");
    if (!editor || !*editor) editor = getenv("EDITOR");
    if (!editor || !*editor) editor = "vi";
    if (app->headless || !app->doc) return;
    screen_suspend();
    pid_t pid = fork();
    if (pid == 0) { /* EDITOR may hold arguments: let the shell split it */
        char command[512];
        snprintf(command, sizeof command, "%s \"$1\"", editor);
        execl("/bin/sh", "sh", "-c", command, "sh", app->doc->path, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
    screen_resume();
    explore_reload(app);
}

static void handle(App *app, int key) {
    if (key == K_NONE) return;
    app->notice[0] = '\0';
    if (key == 3) {
        app->quit = true;
    } else if (app->overlay == OVERLAY_HELP) {
        app->overlay = OVERLAY_NONE;
    } else if (key == '?' && app->view != VIEW_TRIAL && !(app->view == VIEW_RUN && vm_status(app->run.vm) == VM_NEEDS_INPUT)) {
        app->overlay = OVERLAY_HELP;
    } else if (key == 'E' && (app->view == VIEW_EXPLORE || app->view == VIEW_UML)) {
        edit(app);
    } else {
        switch (app->view) {
            case VIEW_FILES: files_key(app, key); break;
            case VIEW_EXPLORE: explore_key(app, key); break;
            case VIEW_TRIAL: trial_key(app, key); break;
            case VIEW_RUN: run_key(app, key); break;
            case VIEW_TESTS: tests_key(app, key); break;
            case VIEW_UML: uml_key(app, key); break;
        }
    }
}

/* "<down><enter>r": keys for a run without a terminal. */
static int next_key(const char **text) {
    static const struct {
        const char *name;
        int key;
    } NAMES[] = {{"up", K_UP}, {"down", K_DOWN}, {"left", K_LEFT}, {"right", K_RIGHT}, {"enter", K_ENTER},
                 {"esc", K_ESC}, {"tab", K_TAB}, {"space", ' '}, {"back", K_BACKSPACE}, {"home", K_HOME},
                 {"end", K_END}, {"pgup", K_PAGE_UP}, {"pgdn", K_PAGE_DOWN}, {"eof", 4}};
    if (**text == '<') {
        const char *close = strchr(*text, '>');
        for (size_t i = 0; close && i < sizeof NAMES / sizeof NAMES[0]; i++) {
            size_t n = strlen(NAMES[i].name);
            if ((size_t)(close - *text - 1) == n && strncmp(*text + 1, NAMES[i].name, n) == 0) {
                *text = close + 1;
                return NAMES[i].key;
            }
        }
    }
    return (unsigned char)*(*text)++;
}

static void open_path(App *app, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) files_open(app, path);
    else explore_open(app, path);
}

int run_ui(const UiOptions *options) {
    App app = {.config = options->config, .headless = options->keys != NULL};
    ui_italian = options->italian;
    struct stat st;
    if (stat(options->path, &st) != 0) {
        fprintf(stderr, "pseudoscope: cannot open '%s'\n", options->path);
        return 3;
    }
    srand(app.headless ? 1u : (unsigned)time(NULL));
    if (app.headless) {
        screen_open_headless(options->width, options->height);
    } else if (!screen_open()) {
        fputs("pseudoscope: the interface needs a terminal\n", stderr);
        return 2;
    }
    open_path(&app, options->path);
    if (!app.doc && app.view != VIEW_FILES) app.quit = true;
    if (!app.doc && !app.files.directory) app.quit = true;

    if (app.headless) {
        const char *keys = options->keys;
        draw(&app);
        while (*keys && !app.quit) {
            handle(&app, next_key(&keys));
            run_tick(&app);
            draw(&app);
        }
        frame_dump(options->colors);
    } else {
        while (!app.quit && !screen_should_quit()) {
            int wait = run_tick(&app);
            draw(&app);
            handle(&app, screen_key(wait));
            if (app.doc && (app.view == VIEW_EXPLORE || app.view == VIEW_UML) && document_changed(app.doc)) explore_reload(&app);
        }
    }
    run_close(&app);
    trial_close(&app);
    tests_close(&app);
    files_close(&app);
    document_free(app.doc);
    screen_close();
    return 0;
}
