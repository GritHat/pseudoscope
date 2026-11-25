#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int compare_entries(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    if (x->directory != y->directory) return x->directory ? -1 : 1;
    return strcmp(x->name, y->name);
}

static bool is_source(const char *name) {
    const char *dot = strrchr(name, '.');
    static const char *const KNOWN[] = {".plike", ".p", ".psc", ".alg", ".por", ".pse"};
    for (size_t i = 0; dot && i < sizeof KNOWN / sizeof KNOWN[0]; i++)
        if (equals_ignore_case(dot, KNOWN[i])) return true;
    return is_uml_path(name);
}

static char *join(const char *directory, const char *name) {
    size_t n = strlen(directory) + strlen(name) + 2;
    char *path = malloc(n);
    if (!path) abort();
    bool slash = directory[strlen(directory) - 1] == '/';
    snprintf(path, n, "%s%s%s", directory, slash ? "" : "/", name);
    return path;
}

static void clear(Files *f) {
    for (int i = 0; i < f->count; i++) free(f->entries[i].name);
    free(f->entries);
    f->entries = NULL;
    f->count = f->selected = f->top = 0;
}

static void forget_preview(Files *f) {
    document_free(f->preview);
    free(f->preview_path);
    f->preview = NULL;
    f->preview_path = NULL;
}

static void update_preview(App *app) {
    Files *f = &app->files;
    if (f->count == 0 || f->entries[f->selected].directory) {
        forget_preview(f);
        return;
    }
    char *path = join(f->directory, f->entries[f->selected].name);
    if (f->preview && f->preview_path && strcmp(path, f->preview_path) == 0) {
        free(path);
        return;
    }
    forget_preview(f);
    f->preview = document_load(path, &app->config);
    f->preview_path = path;
}

void files_open(App *app, const char *directory) {
    Files *f = &app->files;
    char *real = realpath(directory, NULL);
    if (!real) {
        snprintf(app->notice, sizeof app->notice, L("Non riesco ad aprire %s", "Cannot open %s"), directory);
        return;
    }
    DIR *dir = opendir(real);
    if (!dir) {
        snprintf(app->notice, sizeof app->notice, L("Non riesco ad aprire %s", "Cannot open %s"), real);
        free(real);
        return;
    }
    clear(f);
    free(f->directory);
    f->directory = real;
    int capacity = 0;
    if (strcmp(real, "/") != 0) {
        f->entries = malloc(64 * sizeof(Entry));
        if (!f->entries) abort();
        capacity = 64;
        f->entries[f->count++] = (Entry){strdup(".."), true};
    }
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        struct stat st;
        char *path = join(real, e->d_name);
        bool ok = stat(path, &st) == 0, directory_entry = ok && S_ISDIR(st.st_mode);
        free(path);
        if (!ok || (!directory_entry && !is_source(e->d_name))) continue;
        if (f->count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            f->entries = realloc(f->entries, (size_t)capacity * sizeof(Entry));
            if (!f->entries) abort();
        }
        f->entries[f->count++] = (Entry){strdup(e->d_name), directory_entry};
    }
    closedir(dir);
    if (f->count) qsort(f->entries, (size_t)f->count, sizeof(Entry), compare_entries);
    for (int i = 0; i < f->count; i++) {
        if (!f->entries[i].directory) { /* start from the first program */
            f->selected = i;
            break;
        }
    }
    app->view = VIEW_FILES;
    update_preview(app);
}

static const char *kind_of(const Document *doc) {
    if (doc->uml) return uml_kind_name(doc->uml);
    switch (doc->config.dialect) {
        case DIALECT_PSEINT: return "PSeInt";
        case DIALECT_VISUALG: return "VisuAlg";
        case DIALECT_PORTUGOL: return "Portugol";
        case DIALECT_CAMBRIDGE: return "Cambridge";
        default: return "P-like";
    }
}

static void draw_preview(const Document *doc, int x, int y, int w, int h) {
    print_at(x + w - 24, y + 1, 24, S(C_TYPE, C_DEFAULT, A_BOLD), "%24s", kind_of(doc));
    int functions = 0, records = 0;
    for (int i = 0; i < doc->item_count; i++) {
        functions += doc->items[i].kind == ITEM_FUNCTION;
        records += doc->items[i].kind == ITEM_RECORD;
    }
    if (doc->uml) print_at(x, y, w, S_DIM, "%s", doc->uml->title ? doc->uml->title : "");
    else print_at(x, y, w, S_TEXT, M_CALL "ƒ" M_RESET " %d %s   " M_TYPE "▦" M_RESET " %d record   " M_DIM "%d %s",
             functions, L("sottoprogrammi", "subprograms"), records, doc->line_count, L("righe", "lines"));
    int errors = 0;
    for (int i = 0; i < doc->message_count; i++) errors += !doc->messages[i].warning;
    if (errors) print_at(x, y + 1, w, S_TEXT, M_ERROR "✗ %d %s", errors, L("errori", "errors"));
    else if (doc->message_count)
        print_at(x, y + 1, w, S_TEXT, M_FOCUS "⚠ %d %s", doc->message_count, L("avvisi", "warnings"));
    else print_at(x, y + 1, w, S_TEXT, M_WRITE "✓ %s", L("nessun errore", "no errors"));
    hline(x, y + 2, w, S_FAINT, "┄");
    CodeView view = {.doc = doc, .first = 1, .last = doc->line_count, .top = 1};
    draw_code(x - 1, y + 3, w, h - 3, &view);
}

void files_draw(App *app) {
    Files *f = &app->files;
    int w = screen_width(), h = screen_height();
    const char *home = getenv("HOME"), *where = f->directory ? f->directory : "";
    size_t n = home && home[0] && home[1] ? strlen(home) : 0;
    char place[600];
    bool inside = n && strncmp(where, home, n) == 0 && (where[n] == '/' || where[n] == '\0');
    snprintf(place, sizeof place, "%s%s", inside ? "~" : "", inside ? where + n : where);
    draw_header(place, NULL, 0, 0, 0);
    int list = w / 3 < 26 ? 26 : w / 3, rows = h - 5;
    draw_box(0, 2, list, h - 4, C_READ, L("File", "Files"));
    f->top = scroll_to(f->top, f->selected, 0, f->count - 1, rows - 1);
    for (int i = 0; i < rows - 1 && f->top + i < f->count; i++) {
        const Entry *e = &f->entries[f->top + i];
        draw_table_row(1, 3 + i, list - 2, f->top + i == f->selected, "%s%s%s",
                       e->directory ? M_FOCUS "▪ " M_RESET : is_uml_path(e->name) ? M_TYPE "▣ " M_RESET : M_READ "◆ " M_RESET, e->name,
                       e->directory ? "/" : "");
    }
    bool any = false;
    for (int i = 0; i < f->count; i++) any |= !f->entries[i].directory;
    if (!any)
        print_wrapped(3, 4 + f->count, list - 5, 3, S_DIM, "%s",
                      L("Nessun file .plike in questa cartella.", "No .plike files in this folder."));
    draw_box(list, 2, w - list, h - 4, C_BORDER, f->preview ? L("Anteprima", "Preview") : NULL);
    if (f->preview) draw_preview(f->preview, list + 2, 3, w - list - 4, h - 6);
    const Hint hints[] = {{"↑↓", L("scegli", "choose")}, {"Invio", L("apri", "open")},
                          {"←", L("cartella superiore", "parent folder")}, {"?", L("aiuto", "help")},
                          {"q", L("esci", "quit")}};
    Hint shown[5];
    memcpy(shown, hints, sizeof shown);
    if (!ui_italian) shown[1].key = "Enter";
    draw_footer(shown, 5);
}

void files_key(App *app, int key) {
    Files *f = &app->files;
    switch (key) {
        case K_UP: case 'k': if (f->selected > 0) f->selected--; break;
        case K_DOWN: case 'j': if (f->selected < f->count - 1) f->selected++; break;
        case K_PAGE_UP: f->selected = f->selected > 10 ? f->selected - 10 : 0; break;
        case K_PAGE_DOWN: f->selected = f->selected + 10 < f->count ? f->selected + 10 : f->count - 1; break;
        case K_HOME: f->selected = 0; break;
        case K_END: f->selected = f->count - 1; break;
        case K_LEFT: case K_BACKSPACE: case 'h': {
            char *path = join(f->directory, "..");
            files_open(app, path);
            free(path);
            return;
        }
        case K_ENTER: case K_RIGHT: case 'l': {
            if (f->count == 0) return;
            char *path = join(f->directory, f->entries[f->selected].name);
            if (f->entries[f->selected].directory) files_open(app, path);
            else explore_open(app, path);
            free(path);
            return;
        }
        case 'q': app->quit = true; return;
        default: return;
    }
    if (f->selected < 0) f->selected = 0;
    update_preview(app);
}

void files_close(App *app) {
    clear(&app->files);
    forget_preview(&app->files);
    free(app->files.directory);
    app->files.directory = NULL;
}
