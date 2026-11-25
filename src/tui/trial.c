#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trying a single subprogram: the arguments are typed in, or generated. */

enum { PRESET_CUSTOM, PRESET_RANDOM, PRESET_UP, PRESET_DOWN, PRESET_EQUAL, PRESETS };

static const char *preset_name(int preset) {
    switch (preset) {
        case PRESET_RANDOM: return T("random");
        case PRESET_UP: return T("increasing");
        case PRESET_DOWN: return T("decreasing");
        case PRESET_EQUAL: return T("all equal");
        default: return T("your own");
    }
}

static bool is_list(const Symbol *param) {
    return param->type->kind == TY_ARRAY || param->type->kind == TY_RECORD;
}

/* Filled in by the subprogram: nothing to type. */
static bool is_output(const Symbol *param) {
    if (param->mode != MODE_OUT) return false;
    return param->type->kind != TY_ARRAY || param->type->dims.items[0].hi;
}

static bool one_of(const char *name, const char *const *names) {
    for (; *names; names++)
        if (equals_ignore_case(name, *names)) return true;
    return false;
}

/* A sensible first value, guessed from the name. */
static void guess(const App *app, const Symbol *param, char *out) {
    static const char *const FIRST[] = {"l", "lo", "low", "left", "i", "p", "inizio", "sx", "start", NULL};
    static const char *const LAST[] = {"r", "hi", "high", "right", "fine", "q", "dx", "end_", NULL};
    static const char *const MIDDLE[] = {"m", "mid", "medio", NULL};
    int base = app->doc->config.index_base;
    switch (param->type->kind) {
        case TY_INT: {
            int v = 6;
            if (one_of(param->name, FIRST)) v = base;
            else if (one_of(param->name, LAST)) v = base + 5;
            else if (one_of(param->name, MIDDLE)) v = base + 2;
            snprintf(out, TRIAL_TEXT, "%d", v);
            break;
        }
        case TY_REAL: snprintf(out, TRIAL_TEXT, "2.5"); break;
        case TY_BOOL: snprintf(out, TRIAL_TEXT, "true"); break;
        case TY_CHAR: snprintf(out, TRIAL_TEXT, "'a'"); break;
        default: out[0] = '\0';
    }
}

static bool parse_values(const char *text, TrialArgument *arg) {
    int capacity = 16;
    arg->count = 0;
    arg->values = malloc((size_t)capacity * sizeof(double));
    if (!arg->values) abort();
    while (*text) {
        if (strchr(" ,;\t", *text)) {
            text++;
            continue;
        }
        double v;
        char *end = NULL;
        if (*text == '\'' && text[1] && text[2] == '\'') {
            v = (signed char)text[1];
            end = (char *)text + 3;
        } else if (strncmp(text, "true", 4) == 0) {
            v = 1;
            end = (char *)text + 4;
        } else if (strncmp(text, "false", 5) == 0) {
            v = 0;
            end = (char *)text + 5;
        } else {
            v = strtod(text, &end);
            if (end == text) return false;
        }
        if (arg->count == capacity) {
            capacity *= 2;
            arg->values = realloc(arg->values, (size_t)capacity * sizeof(double));
            if (!arg->values) abort();
        }
        arg->values[arg->count++] = v;
        text = end;
    }
    return true;
}

static void free_args(TrialArgument *args, int count) {
    for (int i = 0; args && i < count; i++) free(args[i].values);
    free(args);
}

static TrialArgument *parse_all(Trial *t, int *bad) {
    TrialArgument *args = calloc((size_t)t->count + 1, sizeof *args);
    if (!args) abort();
    *bad = -1;
    for (int i = 0; i < t->count; i++)
        if (!parse_values(t->text[i], &args[i]) && *bad < 0) *bad = i;
    return args;
}

/* Fills the arrays that follow a preset, with as many values as they need. */
static void refill(App *app) {
    Trial *t = &app->trial;
    int bad;
    TrialArgument *args = parse_all(t, &bad);
    for (int i = 0; i < t->count; i++) {
        const Symbol *param = t->function->symbol->params[i];
        if (!is_list(param) || is_output(param) || t->preset[i] == PRESET_CUSTOM) continue;
        bool open;
        int count = vm_trial_size(&app->doc->config, t->function, args, i, &open);
        if (count < 0) count = 0;
        if (open) count *= 6;
        if (count > 60) count = 60;
        char *out = t->text[i];
        size_t used = 0;
        out[0] = '\0';
        for (int k = 0; k < count && used + 8 < TRIAL_TEXT; k++) {
            int v = t->preset[i] == PRESET_UP ? (k + 1) * 3 : t->preset[i] == PRESET_DOWN ? (count - k) * 3
                  : t->preset[i] == PRESET_EQUAL ? 7 : 1 + rand() % 50;
            used += (size_t)snprintf(out + used, TRIAL_TEXT - used, "%s%d", k ? " " : "", v);
        }
    }
    free_args(args, t->count);
}

void trial_close(App *app) {
    free(app->trial.text);
    free(app->trial.preset);
    app->trial = (Trial){0};
}

void trial_open(App *app, const Function *function) {
    if (app->trial.function != function) {
        trial_close(app);
        Trial *t = &app->trial;
        t->function = function;
        t->count = function->symbol->param_count;
        t->text = calloc((size_t)t->count + 1, TRIAL_TEXT);
        t->preset = calloc((size_t)t->count + 1, sizeof(int));
        if (!t->text || !t->preset) abort();
        for (int i = 0; i < t->count; i++) {
            const Symbol *param = function->symbol->params[i];
            if (is_list(param)) t->preset[i] = PRESET_RANDOM;
            else if (!is_output(param)) guess(app, param, t->text[i]);
        }
        refill(app);
        while (t->selected < t->count - 1 && is_output(function->symbol->params[t->selected])) t->selected++;
    }
    app->trial.error[0] = '\0';
    app->view = VIEW_TRIAL;
}

void trial_draw(App *app) {
    Trial *t = &app->trial;
    int w = screen_width(), h = screen_height();
    char path[200];
    const char *name = strrchr(app->doc->path, '/');
    snprintf(path, sizeof path, M_TEXT "%s" M_FAINT " ▸ " M_CALL "%s" M_FAINT " ▸ " M_TEXT "%s",
             name ? name + 1 : app->doc->path, t->function->name, T("try"));
    draw_header(path, NULL, 0, 0, 0);
    draw_box(0, 2, w, h - 4, C_CALL, T("With which values?"));
    print_at(3, 4, w - 6, S_DIM, "%s",
             T("Choose the values to pass, then press Enter to see what happens."));
    int row = 6;
    for (int i = 0; i < t->count && row < h - 4; i++) {
        const Symbol *param = t->function->symbol->params[i];
        bool selected = i == t->selected;
        char type[100] = "";
        describe_type(app->doc, param->type, type, sizeof type);
        put_text(3, row, S(C_FOCUS, C_DEFAULT, A_BOLD), selected ? "▸" : " ");
        print_at(5, row, 16, S(param->mode == MODE_IN ? C_READ : C_WRITE, C_DEFAULT, A_BOLD), "%s", param->name);
        print_at(22, row, w - 50, S_FAINT, "%s", type);
        if (is_list(param) && !is_output(param)) print_at(w - 28, row, 25, S_DIM, "%s: " M_FOCUS "%s", T("values"), preset_name(t->preset[i]));
        row++;
        if (is_output(param)) {
            print_at(5, row, w - 8, S_FAINT, "%s", T("filled in by the subprogram"));
        } else {
            fill(5, row, w - 9, 1, S(C_TEXT, selected ? C_SELECT : C_PANEL, 0));
            int width = text_width(t->text[i]), room = w - 12;
            int skip = width > room ? width - room : 0;
            print_at(6, row, room, S(C_BRIGHT, selected ? C_SELECT : C_PANEL, 0), "%s", t->text[i] + skip);
            if (selected) put_text(6 + width - skip, row, S(16, C_FOCUS, 0), " ");
        }
        row += 2;
    }
    if (t->error[0]) print_at(3, h - 4, w - 6, S(C_ERROR, C_DEFAULT, A_BOLD), "✗ %s", t->error);
    const Hint hints[] = {{"↑↓", T("field")}, {T("Enter"), T("run")},
                          {"Tab", T("other values")}, {"Esc", T("back")}};
    draw_footer(hints, 4);
}

void trial_key(App *app, int key) {
    Trial *t = &app->trial;
    char *text = t->text[t->selected];
    const Symbol *param = t->function->symbol->params[t->selected];
    size_t n = strlen(text);
    t->error[0] = '\0';
    switch (key) {
        case K_ESC: app->view = VIEW_EXPLORE; return;
        case K_UP: if (t->selected > 0) t->selected--; return;
        case K_DOWN: if (t->selected < t->count - 1) t->selected++; return;
        case K_TAB: case K_SHIFT_TAB:
            if (!is_list(param) || is_output(param)) { /* the first array, wherever we are */
                int i = 0;
                while (i < t->count && (!is_list(t->function->symbol->params[i]) ||
                                        is_output(t->function->symbol->params[i]))) i++;
                if (i == t->count) return;
                t->selected = i;
            }
            t->preset[t->selected] = t->preset[t->selected] % (PRESETS - 1) + 1;
            refill(app);
            return;
        case K_BACKSPACE:
            if (n && !is_output(param)) {
                text[n - 1] = '\0';
                if (is_list(param)) t->preset[t->selected] = PRESET_CUSTOM;
                else refill(app);
            }
            return;
        case K_ENTER: {
            int bad;
            TrialArgument *args = parse_all(t, &bad);
            if (bad >= 0) {
                snprintf(t->error, sizeof t->error, T("Cannot understand the values of '%s'"),
                         t->function->symbol->params[bad]->name);
            } else {
                for (int i = 0; i < t->count; i++) {
                    const Symbol *p = t->function->symbol->params[i];
                    if (!is_list(p) && !is_output(p) && p->type->kind != TY_POINTER && args[i].count < 1) bad = i;
                }
                if (bad >= 0)
                    snprintf(t->error, sizeof t->error, T("The value of '%s' is missing"),
                             t->function->symbol->params[bad]->name);
                else if (run_open(app, t->function, args, t->count))
                    return; /* the run keeps the arguments */
                else
                    snprintf(t->error, sizeof t->error, "%s",
                             T("With these values the sizes of the arrays are not valid"));
            }
            free_args(args, t->count);
            return;
        }
        default:
            if (key >= 32 && key < 127 && n + 1 < TRIAL_TEXT && !is_output(param)) {
                text[n] = (char)key;
                text[n + 1] = '\0';
                if (is_list(param)) t->preset[t->selected] = PRESET_CUSTOM;
                else refill(app);
            }
    }
}
