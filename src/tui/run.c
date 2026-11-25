#include "ui.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int DELAYS[] = {1500, 900, 500, 260, 120, 50, 16};
#define SPEEDS ((int)(sizeof DELAYS / sizeof DELAYS[0]))

/* ---- life ---- */

void run_close(App *app) {
    Run *r = &app->run;
    vm_destroy(r->vm);
    free(r->breakpoints);
    for (int i = 0; r->args && i < r->arg_count; i++) free(r->args[i].values);
    free(r->args);
    strbuf_free(&r->c_code);
    arena_free(&r->c_arena);
    *r = (Run){0};
}

static bool start(Run *r) {
    r->playing = false;
    r->goal = GOAL_NONE;
    if (r->trial) {
        if (!vm_start_trial(r->vm, r->trial, r->args)) return false;
    } else if (r->test) {
        vm_start_test(r->vm, r->test);
    } else {
        vm_start(r->vm);
    }
    r->system_back = 0;
    if (r->level != LEVEL_SYSTEM) vm_run(r->vm, LEVEL_STATEMENT, 1000000);
    return true;
}

static bool open_run(App *app, Run fresh) {
    Vm *vm = vm_create(app->doc->program, &app->doc->config);
    fresh.vm = vm;
    fresh.speed = 2;
    bool same = app->run.vm && app->run.trial == fresh.trial && app->run.test == fresh.test;
    fresh.level = same ? app->run.level
                : app->doc->program->classes && !fresh.trial ? LEVEL_SYSTEM : LEVEL_STATEMENT;
    if (same) fresh.system = app->run.system;
    if (!start(&fresh)) {
        vm_destroy(vm);
        return false;
    }
    run_close(app);
    fresh.breakpoints = calloc((size_t)app->doc->line_count + 2, sizeof(bool));
    if (!fresh.breakpoints) abort();
    app->run = fresh;
    app->view = VIEW_RUN;
    return true;
}

bool run_open_test(App *app, const Function *test) { return open_run(app, (Run){.test = test}); }

bool run_open(App *app, const Function *trial, TrialArgument *args, int count) {
    return open_run(app, (Run){.trial = trial, .args = args, .arg_count = count});
}

static void restart(Run *r) {
    start(r);
}

/* ---- words ---- */

static const char *vm_text(const char *english) { return english ? tr(english) : ""; }

static void expr_text(const Expr *e, char *out, size_t size) {
    StrBuf text = {0};
    strbuf_puts(&text, "");
    if (e) print_expr(NULL, e, NULL, 0, "", "", "", "", &text);
    snprintf(out, size, "%s", text.data);
    strbuf_free(&text);
}

/* Whether an expression is in words of ours, which the writer never wrote: count_(Q) != 0. */
static bool in_our_words(const char *text) {
    for (const char *c = text; *c; c++)
        if (*c == '_' && (c[1] == '(' || c[1] == ' ' || c[1] == '.' || !c[1]) && c > text && isalnum((unsigned char)c[-1])) return true;
    return false;
}

/* The line as it was written, without its margin and its comment. */
static bool written_line(const Document *doc, int line, char *out, size_t size) {
    if (!doc->shown || line < 1 || line > doc->line_count) return false;
    const char *from = doc->shown + doc->shown_lines[line - 1], *end = from + strcspn(from, "\n");
    while (from < end && (*from == ' ' || *from == '\t' || isdigit((unsigned char)*from))) from++;
    const char *comment = strstr(from, "//");
    if (comment && comment < end) end = comment;
    while (end > from && (end[-1] == ' ' || end[-1] == '\t')) end--;
    snprintf(out, size, "%.*s", (int)(end - from), from);
    return end > from;
}

/* What happened, in a sentence. */
static void sentence(const Document *doc, const LogEntry *e, char *out, size_t size, int room) {
    char test[160] = "";
    switch (e->kind) {
        case LOG_ASSIGN:
            if (e->old[0] && strcmp(e->old, "?") != 0 && !(e->old[0] == '{' && strcmp(e->old, e->value) == 0))
                snprintf(out, size, M_WRITE "✎ " M_BOLD "%s" M_RESET " %s " M_CHIP_WRITE " %s " M_RESET M_DIM "  %s %s", e->target,
                         T("becomes"), e->value, T("it was"), e->old);
            else
                snprintf(out, size, M_WRITE "✎ " M_BOLD "%s" M_RESET " %s " M_CHIP_WRITE " %s ", e->target,
                         T("becomes"), e->value);
            break;
        case LOG_LOOP:
            snprintf(out, size, M_POINTER "↻ " M_RESET "%s " M_POINTER M_BOLD "%s" M_RESET " = " M_CHIP_WRITE " %s ",
                     e->truth ? T("next round:") : T("the loop starts with"),
                     e->target, e->value);
            break;
        case LOG_CONDITION: {
            if (e->stmt->kind == ST_FOR) {
                char limit[60];
                expr_text(e->stmt->limit, limit, sizeof limit);
                bool same = strcmp(limit, e->value) == 0;
                snprintf(test, sizeof test, "%.30s = %.20s, %s %.30s%s%.20s", e->stmt->target->text, e->target,
                         T("up to"), limit, same ? "" : " = ", same ? "" : e->value);
            } else {
                char source[80];
                expr_text(e->expr, source, sizeof source);
                bool same = !e->value[0] || strcmp(source, e->value) == 0;
                snprintf(test, sizeof test, "%.60s%s%.40s", source, same ? "" : "  →  ", same ? "" : e->value);
                char written[100]; /* what is run is in words of ours: say it as it was written */
                if (in_our_words(source) && written_line(doc, e->stmt->loc.line, written, sizeof written)) snprintf(test, sizeof test, "%s", written);
            }
            const char *then;
            bool t = e->truth;
            switch (e->stmt->kind) {
                case ST_IF: then = t ? T("this branch runs") : T("this branch is skipped"); break;
                case ST_REPEAT: then = t ? T("the loop ends") : T("once more"); break;
                default: then = t ? T("into the loop") : T("the loop ends");
            }
            snprintf(out, size, M_FOCUS "? " M_RESET "%s  %s%s" M_RESET M_DIM "  %s", test, t ? M_CHIP_WRITE : M_CHIP_ERROR,
                     t ? T(" true ") : T(" false "), then);
            if (room > 0 && text_width(out) > room) /* no room to say what follows from it: where the program goes shows it */
                snprintf(out, size, M_FOCUS "? " M_RESET "%s  %s%s", test, t ? M_CHIP_WRITE : M_CHIP_ERROR, t ? T(" true ") : T(" false "));
            break;
        }
        case LOG_CALL:
            snprintf(out, size, M_CALL "▶ " M_RESET "%s " M_CALL M_BOLD "%s" M_RESET M_DIM "(%s)", T("entering"),
                     e->function->full_name, e->value);
            break;
        case LOG_RETURN:
            if (e->truth)
                snprintf(out, size, M_CALL "◀ " M_BOLD "%s" M_RESET " %s " M_CHIP_WRITE " %s ", e->function->full_name,
                         T("returns"), e->value);
            else
                snprintf(out, size, M_CALL "◀ " M_BOLD "%s" M_RESET " %s", e->function->full_name, T("is done"));
            break;
        case LOG_PRINT:
            snprintf(out, size, M_TYPE "≡ " M_RESET "%s  " M_CHIP_OFF M_TEXT " %s ", T("prints"), e->value);
            break;
        case LOG_READ:
            snprintf(out, size, M_READ "» " M_RESET "%s " M_BOLD "%s" M_RESET " = " M_CHIP_WRITE " %s ", T("reads"),
                     e->target, e->value);
            break;
        case LOG_WARNING:
            expr_text(e->expr, test, sizeof test);
            snprintf(out, size, M_FOCUS "⚠ " M_BOLD "%s" M_RESET M_FOCUS " %s", test, vm_text(e->text));
            break;
        case LOG_ERROR:
            if (e->target[0])
                snprintf(out, size, M_ERROR M_BOLD "✗ %s" M_RESET M_TEXT "   %s[" M_ERROR "%s" M_TEXT "]" M_DIM "  %s %s",
                         vm_text(e->text), e->target, e->value, T("valid:"), e->old);
            else
                snprintf(out, size, M_ERROR M_BOLD "✗ %s", vm_text(e->text));
            break;
        case LOG_SEND:
            snprintf(out, size, M_READ "▷ " M_RESET M_BOLD "%s" M_RESET " %s " M_CHIP_READ " %s " M_RESET " %s " M_BOLD "%s", e->old,
                     T("sends"), e->text, T("to"), e->target);
            break;
        case LOG_DELIVER:
            snprintf(out, size, M_READ "▶ " M_CHIP_READ " %s%s%s%s " M_RESET " %s " M_BOLD "%s", e->text, e->value[0] ? "(" : "", e->value,
                     e->value[0] ? ")" : "", T("reaches"), e->target);
            break;
        case LOG_LOST:
            snprintf(out, size, M_ERROR M_BOLD "✗ %s" M_RESET " %s " M_BOLD "%s" M_RESET " %s " M_BOLD "%s", e->text,
                     T("got lost between"), e->old, T("and"), e->target);
            break;
        case LOG_IGNORED:
            snprintf(out, size, M_DIM "⊘ " M_BOLD "%s" M_RESET M_DIM " %s %s%s%s", e->text, T("does not matter to"), e->target,
                     e->value[0] ? T(", which is in ") : "", e->value);
            break;
        case LOG_CHECK:
            snprintf(out, size, "%s " M_RESET " %s", e->truth ? M_CHIP_WRITE " ✓" : M_CHIP_ERROR " ✗", e->value);
            break;
        case LOG_STEP:
            snprintf(out, size, M_FOCUS M_BOLD "● %s", e->text);
            break;
        case LOG_TODO:
            snprintf(out, size, M_FOCUS "… " M_RESET M_BOLD "%s" M_RESET M_DIM " %s", e->function ? e->function->full_name : "",
                     T("is only designed: the code will go here"));
            break;
        case LOG_STATE:
            if (strcmp(e->old, e->value) == 0)
                snprintf(out, size, M_FOCUS "◆ " M_RESET M_BOLD "%s" M_RESET " %s " M_CHIP_FOCUS " %s ", e->target, T("remains"), e->value);
            else
                snprintf(out, size, M_FOCUS "◆ " M_RESET M_BOLD "%s" M_RESET " %s " M_CHIP_FOCUS " %s " M_RESET M_DIM "  %s %s", e->target,
                         TC("state", "becomes"), e->value, T("it was"), e->old);
            break;
        case LOG_END:
            snprintf(out, size, M_WRITE M_BOLD "✓ %s", T("the program is over"));
            break;
    }
}

/* ---- moving ---- */

static int depth_now(const Run *r) { return vm_frame_count(r->vm); }
static bool can_run(const Run *r) { return vm_status(r->vm) == VM_READY; }

static bool at_breakpoint(const Run *r) {
    const Frame *f = vm_frame(r->vm, vm_frame_count(r->vm) - 1);
    return f->stmt && r->breakpoints[f->stmt->loc.line];
}

static bool has_breakpoints(const App *app) {
    for (int i = 0; i <= app->doc->line_count; i++)
        if (app->run.breakpoints[i]) return true;
    return false;
}

/* Works towards the goal for a moment; true when there. */
static bool pursue(App *app) {
    Run *r = &app->run;
    long long until = now_ms() + (app->headless ? 5000 : 30);
    bool stops = has_breakpoints(app);
    while (can_run(r)) {
        switch (r->goal) {
            case GOAL_STOP:
                if (vm_run(r->vm, r->level, 200000) != LEVEL_NONE) return true;
                break;
            case GOAL_OVER: case GOAL_OUT: {
                Level got = vm_run(r->vm, LEVEL_STATEMENT, 200000);
                int wanted = r->goal == GOAL_OVER ? r->goal_depth : r->goal_depth - 1;
                if (got == LEVEL_STATEMENT && depth_now(r) <= wanted) return true;
                if (got != LEVEL_NONE && stops && at_breakpoint(r)) return true;
                break;
            }
            case GOAL_END:
                if (!stops) vm_run(r->vm, LEVEL_NONE, 200000);
                else if (vm_run(r->vm, LEVEL_STATEMENT, 200000) == LEVEL_STATEMENT && at_breakpoint(r)) return true;
                break;
            default:
                return true;
        }
        if (now_ms() > until) return false;
    }
    return true;
}

static void aim(App *app, Goal goal) {
    Run *r = &app->run;
    r->goal = goal;
    r->goal_depth = depth_now(r);
    if (pursue(app)) r->goal = GOAL_NONE;
}

int run_tick(App *app) {
    Run *r = &app->run;
    if (app->view == VIEW_UML) return uml_tick(app);
    if (!r->vm || app->view != VIEW_RUN) return 1000;
    if (r->goal != GOAL_NONE) {
        if (pursue(app)) r->goal = GOAL_NONE;
        return r->goal == GOAL_NONE ? 1000 : 0;
    }
    if (!r->playing) return 1000;
    long long now = now_ms();
    if (now >= r->next_tick) {
        r->next_tick = now + DELAYS[r->speed];
        aim(app, GOAL_STOP);
        if (!can_run(r) || (at_breakpoint(r) && vm_last_stop(r->vm) == LEVEL_STATEMENT)) r->playing = false;
    }
    long long wait = r->next_tick - now_ms();
    return r->playing ? (int)(wait < 0 ? 0 : wait) : 1000;
}

/* ---- drawing ---- */

static void draw_calls(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int total = vm_node_count(vm);
    long long now = vm_steps(vm);
    if (total == 0) return;
    /* when there are too many, what has finished is folded */
    bool fold = total > h;
    int *visible = malloc((size_t)total * sizeof(int));
    bool *hidden = calloc((size_t)total, sizeof(bool)), *folded = calloc((size_t)total, sizeof(bool));
    if (!visible || !hidden || !folded) abort();
    int n = 0, current = 0;
    const Frame *top = vm_frame(vm, vm_frame_count(vm) - 1);
    for (int i = 0; i < total; i++) {
        const CallNode *node = vm_node(vm, i);
        if (node->parent >= 0 && fold) {
            hidden[i] = hidden[node->parent] || vm_node(vm, node->parent)->end <= now;
            if (hidden[i] && !hidden[node->parent]) folded[node->parent] = true;
        }
        if (hidden[i]) continue;
        if (top->function && i == top->node) current = n;
        visible[n++] = i;
    }
    int first = scroll_to(0, current, 0, n - 1, h);
    for (int row = 0; row < h && first + row < n; row++) {
        const CallNode *node = vm_node(vm, visible[first + row]);
        bool active = node->end > now, here = first + row == current && top->function;
        int indent = node->depth * 2;
        if (indent > w / 2) indent = w / 2;
        char line[300];
        snprintf(line, sizeof line, "%s%s%s" M_RESET M_DIM "(%s)%s%s%s", node->depth ? M_FAINT "└ " : "",
                 here ? M_CHIP_CALL : active ? M_CALL M_BOLD : M_DIM, node->function->full_name, node->args,
                 !active && node->result[0] ? "  ⇒ " M_WRITE M_BOLD : "", !active ? node->result : "",
                 folded[visible[first + row]] ? M_FAINT "  …" : "");
        print_at(x, y + row, 2, S(C_FOCUS, C_DEFAULT, A_BOLD), "%s", here ? "▶" : !active ? M_WRITE "✓" : " ");
        print_at(x + 2 + indent, y + row, w - indent - 2, S_TEXT, "%s", line);
    }
    draw_scrollbar(x + w, y, h, first, h, n);
    free(visible);
    free(hidden);
    free(folded);
}

static void draw_stack(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int frames = vm_frame_count(vm) - 1, row = y;
    if (frames == 0) {
        print_wrapped(x, y, w, 3, S_FAINT, "%s", T("No subprogram is running."));
        return;
    }
    for (int i = frames; i >= 1 && row + 3 <= y + h; i--) {
        const Frame *f = vm_frame(vm, i);
        bool top = i == frames;
        if (row + 4 > y + h && i > 1) {
            print_centered(x, row, w, S_DIM, T("… %d more below"), i);
            return;
        }
        draw_box(x, row, w, 3, top ? C_CALL : C_BORDER, NULL);
        const char *args = f->node >= 0 ? vm_node(vm, f->node)->args : "";
        print_at(x + 2, row + 1, w - 4, S_TEXT, "%s%s" M_RESET M_DIM "(%s)", top ? M_CALL M_BOLD "▶ " : M_DIM "‖ ",
                 f->function->full_name, args);
        row += 3;
        if (i > 1 && row < y + h) {
            print_centered(x, row, w, S_FAINT, "%s", T("▲ waits for it to end"));
            row++;
        }
    }
}

static void draw_current_code(App *app, int x, int y, int w, int h) {
    const Run *r = &app->run;
    const Vm *vm = r->vm;
    const Frame *f = vm_frame(vm, vm_frame_count(vm) - 1);
    const Function *function = f->function ? f->function : r->trial;
    const Item *item = function ? document_function(app->doc, function) : NULL;
    if (!function && has_components(app->doc->program)) {
        print_wrapped(x + 2, y + 1, w - 4, 4, S_DIM, "%s",
                      T("Nobody is working now: the next message is awaited."));
        return;
    }
    if (!item) item = app->doc->main_item >= 0 ? &app->doc->items[app->doc->main_item] : NULL;
    if (!item) return;
    Tint tints[8];
    CodeView view = {.doc = app->doc, .first = item->first, .last = item->last, .breakpoints = r->breakpoints, .tints = tints};
    view.tint_count = run_tints(app, item->function, tints, 8);
    if (f->function && f->stmt) view.current = f->stmt->loc.line;
    view.current_bg = vm_status(vm) == VM_FAILED ? 52 : 58;
    view.top = scroll_to(item->first, view.current ? view.current : item->first, item->first, item->last, h);
    if (view.current && item->last - item->first >= h) {
        view.top = view.current - h / 2;
        if (view.top > item->last - h + 1) view.top = item->last - h + 1;
        if (view.top < item->first) view.top = item->first;
    }
    draw_code(x, y, w, h, &view);
}

static void reduce(StrBuf *out, const Vm *vm, const Expr *e, const Eval *evals, int count) {
    print_expr(vm, e, evals, count, M_CHIP_FOCUS " ", " " M_RESET, M_READ M_BOLD, M_RESET, out);
}

/* The statement with the first `count` parts already computed. */
static void reduced_statement(const Vm *vm, const Frame *f, const Eval *evals, int count, StrBuf *out) {
    const Stmt *s = f->stmt;
    out->length = 0;
    strbuf_puts(out, "");
    switch (s->kind) {
        case ST_ASSIGN:
            reduce(out, vm, s->target, evals, count);
            strbuf_puts(out, M_DIM "  :=  " M_RESET);
            reduce(out, vm, s->value, evals, count);
            break;
        case ST_FOR:
            if (f->phase == PHASE_CONDITION) {
                reduce(out, vm, s->target, evals, count);
                strbuf_printf(out, M_DIM "  %s  " M_RESET, T("up to"));
                reduce(out, vm, s->limit, evals, count);
            } else if (f->phase == PHASE_STEP) {
                strbuf_printf(out, "%s" M_DIM "  :=  " M_RESET, s->target->text);
                reduce(out, vm, s->target, evals, count);
                strbuf_puts(out, " + ");
                if (s->step) reduce(out, vm, s->step, evals, count);
                else strbuf_puts(out, "1");
            } else {
                strbuf_printf(out, "%s" M_DIM "  :=  " M_RESET, s->target->text);
                reduce(out, vm, s->value, evals, count);
            }
            break;
        case ST_PRINT: case ST_READ:
            strbuf_puts(out, s->kind == ST_PRINT ? "print " : "read ");
            for (int i = 0; i < s->args.count; i++) {
                if (i) strbuf_puts(out, M_DIM " ,  " M_RESET);
                reduce(out, vm, s->args.items[i], evals, count);
            }
            break;
        default:
            if (s->kind == ST_RETURN) strbuf_puts(out, "return ");
            if (s->value) reduce(out, vm, s->value, evals, count);
    }
}

static void draw_statement(App *app, int w, int h) {
    const Vm *vm = app->run.vm;
    const Frame *f = vm_frame(vm, vm_frame_count(vm) - 1);
    int half = w * 55 / 100, bottom = h - 6;
    draw_box(0, 2, w, 3, C_FOCUS, T("Statement"));
    if (f->stmt) {
        int line = f->stmt->loc.line;
        CodeView view = {.doc = app->doc, .first = line, .last = line, .top = line};
        draw_code(2, 3, w - 4, 1, &view);
    }
    draw_box(0, 5, half, bottom - 5, C_BORDER, T("How it is computed"));
    if (f->stmt && f->function) {
        int count, rows = bottom - 8;
        const Eval *evals = vm_evals(vm, &count);
        StrBuf text = {0};
        int first = (count + 1) * 2 - 1 > rows ? count - (rows - 1) / 2 : 0;
        for (int k = first; k <= count && 7 + (k - first) * 2 < bottom - 1; k++) {
            reduced_statement(vm, f, evals, k, &text);
            int y = 7 + (k - first) * 2;
            print_at(4, y, half - 6, k == count ? S_BOLD : S_DIM, "%s", text.data);
            if (k > first && y - 1 > 6) put_text(4, y - 1, S_FAINT, "│");
        }
        if (count == 0 && vm_status(vm) == VM_READY)
            print_at(4, 9, half - 6, S_FAINT, "%s", T("→ to compute one piece at a time"));
        strbuf_free(&text);
    }
    draw_box(half, 5, w - half, bottom - 5, C_BORDER, T("Values involved"));
    run_data_draw(app, half + 2, 7, w - half - 4, bottom - 9);
}

static void draw_machine(App *app, int w, int h) {
    Run *r = &app->run;
    const Vm *vm = r->vm;
    const Frame *f = vm_frame(vm, vm_frame_count(vm) - 1);
    int bottom = h - 6, left = w - 66 < 40 ? w - w * 58 / 100 : w - 66;
    char title[60];
    snprintf(title, sizeof title, "%s %s", T("Translated to"), lang_name(app->target));
    draw_box(0, 2, left, bottom - 2, C_BORDER, title);
    const char *problem = translation_problem(app);
    if (problem) {
        print_wrapped(2, 4, left - 4, bottom - 6, S(C_FOCUS, C_DEFAULT, 0), "%s", problem);
        print_at(2, bottom - 2, left - 4, S_DIM, M_KEY " T " M_RESET " %s", T("another language"));
    } else if (f->function) {
        if (r->c_function != f->function) {
            arena_free(&r->c_arena);
            r->map = (LineMap){0};
            r->c_code.length = 0;
            strbuf_puts(&r->c_code, "");
            translate_function(app->doc->program, &app->doc->config, &r->c_arena, app->target, f->function, &r->c_code, &r->map);
            r->c_function = f->function;
        }
        int hot = -1, last = -1, total = 0, rows = bottom - 4;
        for (int i = 0; i < r->map.count; i++) {
            if (r->map.items[i].stmt != f->stmt) continue;
            hot = last = f->stmt->kind == ST_REPEAT ? r->map.items[i].last : r->map.items[i].first;
        }
        for (size_t i = 0; i < r->c_code.length; i++) total += r->c_code.data[i] == '\n';
        int top = hot >= 0 ? hot - rows / 2 : 0;
        if (top > total - rows) top = total - rows;
        if (top < 0) top = 0;
        draw_c_code(1, 3, left - 2, rows, r->c_code.data, top, hot, last);
    }
    draw_box(left, 2, w - left, bottom - 2, C_BORDER, T("Memory"));
    run_memory_draw(app, left + 2, 3, w - left - 4, bottom - 4);
}

/* From outside only what goes from one to another counts. */
static bool tells(const Run *r, const Program *program, const LogEntry *e) {
    if (r->level != LEVEL_SYSTEM || !has_components(program)) return true;
    switch (e->kind) {
        case LOG_SEND: case LOG_DELIVER: case LOG_LOST: case LOG_IGNORED: case LOG_CHECK: case LOG_STEP: case LOG_TODO:
        case LOG_STATE: case LOG_END: case LOG_ERROR: case LOG_PRINT: case LOG_READ: return true;
        default: return false;
    }
}

static void draw_events(App *app, int y, int w) {
    const Vm *vm = app->run.vm;
    VmStatus status = vm_status(vm);
    short color = status == VM_FAILED ? C_ERROR : status == VM_FINISHED ? C_WRITE : status == VM_NEEDS_INPUT ? C_FOCUS : C_BORDER;
    if (status == VM_NEEDS_INPUT) {
        draw_box(0, y, w, 5, color, T("The program waits for a value"));
        const Frame *f = vm_frame(vm, vm_frame_count(vm) - 1);
        char target[100] = "";
        if (f->stmt && f->stmt->args.count) expr_text(f->stmt->args.items[0], target, sizeof target);
        int x = 3 + print_at(3, y + 2, w / 2, S_TEXT, "%s " M_READ M_BOLD "%s" M_RESET "  ", T("Type"), target);
        fill(x, y + 2, 30, 1, S(C_BRIGHT, C_SELECT, 0));
        print_at(x + 1, y + 2, 28, S(C_BRIGHT, C_SELECT, A_BOLD), "%s", app->run.input);
        put_text(x + 1 + text_width(app->run.input), y + 2, S(16, C_FOCUS, 0), " ");
        return;
    }
    draw_box(0, y, w, 5, color, T("What happened"));
    long last = vm_log_count(vm) - 1, at = last, oldest = last > 2000 ? last - 2000 : 0;
    for (int i = 0; i < 3; i++) {
        const LogEntry *e = NULL;
        while (at >= oldest && (e = vm_log(vm, at)) && !tells(&app->run, app->doc->program, e)) at--, e = NULL;
        if (!e) break;
        at--;
        char text[400];
        sentence(app->doc, e, text, sizeof text, w - 4);
        if (i == 0) {
            print_at(2, y + 3, w - 4, S_TEXT, "%s", text);
        } else { /* older ones without colours */
            char plain[400];
            size_t n = 0;
            for (const char *s = text; *s && n + 1 < sizeof plain; s++) {
                if (*s == '\001' && s[1]) s++;
                else plain[n++] = *s;
            }
            plain[n] = '\0';
            print_at(2, y + 3 - i, w - 4, S_FAINT, "%s", plain);
        }
    }
    if (last < 0)
        print_at(2, y + 2, w - 4, S_DIM, "%s", T("Ready: → takes a step, Space plays."));
}

static void draw_status(App *app, int w) {
    const Run *r = &app->run;
    const Vm *vm = r->vm;
    const char *chip, *state;
    switch (vm_status(vm)) {
        case VM_FINISHED: chip = M_CHIP_WRITE " ✓", state = TC("run", "done"); break;
        case VM_FAILED: chip = M_CHIP_ERROR " ✗", state = T("error"); break;
        case VM_NEEDS_INPUT: chip = M_CHIP_FOCUS " »", state = T("waiting"); break;
        default:
            if (r->goal != GOAL_NONE) chip = M_CHIP_CALL " …", state = T("working");
            else if (r->playing) chip = M_CHIP_READ " ▶", state = T("playing");
            else chip = M_CHIP_OFF " ‖", state = T("paused");
    }
    int x = 1 + print_at(1, 1, w, S_DIM, "%s %s ", chip, state) + 2;
    if (r->playing) {
        x += print_at(x, 1, 20, S_DIM, "%s ", T("speed"));
        for (int i = 0; i < SPEEDS; i++) put_text(x++, 1, S(i <= r->speed ? C_READ : C_FAINT, C_DEFAULT, 0), "▮");
        x += 2;
    }
    /* where we are: main ▸ sort ▸ swap */
    char trail[300] = "";
    int frames = vm_frame_count(vm);
    for (int i = frames > 5 ? frames - 4 : 1; i < frames; i++) {
        size_t n = strlen(trail);
        snprintf(trail + n, sizeof trail - n, "%s%s%.48s", n ? M_FAINT " ▸ " : frames > 5 ? M_FAINT "… ▸ " : "",
                 i == frames - 1 ? M_CALL M_BOLD : M_RESET M_DIM, vm_frame(vm, i)->function->full_name);
    }
    print_at(x, 1, w - x - 12, S_DIM, "%s", trail);
    size_t length;
    vm_output(vm, &length);
    if (length) print_at(w - 13, 1, 13, S_DIM, M_TYPE "≡ " M_RESET "%s", T("output: o"));
    if (vm_draw_count(vm)) print_at(w - 28, 1, 14, S_DIM, M_TYPE "▣ " M_RESET "%s", T("drawing: d"));
}

/* The end of the output, or `back` lines before. */
static void draw_output_text(App *app, int x, int y, int w, int rows, int back) {
    size_t length;
    const char *text = vm_output(app->run.vm, &length);
    int total = 0;
    for (size_t i = 0; i < length; i++) total += text[i] == '\n';
    int top = total > rows ? total - rows - back : 0;
    if (top < 0) top = 0;
    int line = 0;
    for (size_t i = 0; i < length;) {
        size_t n = 0;
        while (i + n < length && text[i + n] != '\n') n++;
        if (line >= top && line - top < rows) print_at(x, y + line - top, w, S_TEXT, "%.*s", (int)(n > 900 ? 900 : n), text + i);
        line++;
        i += n + 1;
    }
    if (length == 0) print_at(x, y, w, S_FAINT, "%s", T("Nothing."));
}

/* The nearest of the 256 colours of a terminal. */
static short color_of(int rgb) {
    int r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
    if (r == g && g == b) return r < 8 ? 16 : r > 248 ? 231 : (short)(232 + (r - 8) / 10);
    return (short)(16 + 36 * ((r + 25) / 51) + 6 * ((g + 25) / 51) + (b + 25) / 51);
}

/* What the program has drawn: each character is two dots, one above the other. */
static void draw_canvas(App *app, int w, int h) {
    const Vm *vm = app->run.vm;
    int cw, ch, cols = w - 4, rows = h - 7;
    vm_canvas_size(vm, &cw, &ch);
    /* as large as it fits, keeping its shape (a character is twice as tall as it is wide) */
    if ((long)cols * ch > (long)rows * 2 * cw) cols = (int)((long)rows * 2 * cw / ch);
    else rows = (int)((long)cols * ch / cw / 2);
    if (cols < 2 || rows < 1) return;
    char title[80];
    char strokes[32];
    snprintf(strokes, sizeof strokes, TN("%d stroke", "%d strokes", vm_draw_count(vm)), vm_draw_count(vm));
    snprintf(title, sizeof title, "%s  %d x %d  %s", T("What the program drew"), cw, ch, strokes);
    draw_box(0, 2, w, h - 4, C_TYPE, title);
    int *pixels = malloc((size_t)cols * (size_t)rows * 2 * sizeof(int));
    if (!pixels) abort();
    vm_render(vm, cols, rows * 2, pixels);
    int x0 = (w - cols) / 2, y0 = 4;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int top = pixels[y * 2 * cols + x], bottom = pixels[(y * 2 + 1) * cols + x];
            if (top < 0 && bottom < 0) put_text(x0 + x, y0 + y, S(C_FAINT, 255, 0), " ");
            else put_text(x0 + x, y0 + y, S(top < 0 ? 255 : color_of(top), bottom < 0 ? 255 : color_of(bottom), 0), "▀");
        }
    }
    int first = 0, count = vm_draw_count(vm);
    for (int i = 0; i < count; i++)
        if (vm_draw(vm, i)->kind == 'c') first = i + 1;
    for (int i = first; i < count; i++) { /* texts are written as they are */
        const DrawOp *op = vm_draw(vm, i);
        if (op->kind != 't') continue;
        int x = (int)((long)op->x * cols / cw), y = (int)((long)op->y * rows / ch);
        if (x >= 0 && x < cols && y >= 0 && y < rows) print_at(x0 + x, y0 + y, cols - x, S(color_of(op->color), 255, A_BOLD), "%s", vm_draw_text(vm, op));
    }
    free(pixels);
}

static void draw_output(App *app, int w, int h) {
    draw_box(0, 2, w, h - 4, C_TYPE, T("What the program printed"));
    draw_output_text(app, 3, 3, w - 6, h - 6, app->run.overlay_top);
}

static void draw_log(App *app, int w, int h) {
    draw_box(0, 2, w, h - 4, C_BORDER, T("Everything that happened"));
    int rows = h - 6;
    long last = vm_log_count(app->run.vm) - 1 - app->run.overlay_top;
    for (int i = 0; i < rows; i++) {
        const LogEntry *e = vm_log(app->run.vm, last - i);
        if (!e) break;
        char text[400];
        int indent = e->depth > 2 ? (e->depth - 2) * 2 : 0;
        if (indent > 20) indent = 20;
        sentence(app->doc, e, text, sizeof text, w - 11 - indent);
        if (e->stmt) print_at(2, h - 4 - i, 5, S_FAINT, "%4d", e->stmt->loc.line);
        print_at(8 + indent, h - 4 - i, w - 11 - indent, S_TEXT, "%s", text);
    }
}

void run_draw(App *app) {
    Run *r = &app->run;
    int w = screen_width(), h = screen_height();
    char path[300];
    const char *name = strrchr(app->doc->path, '/');
    snprintf(path, sizeof path, M_TEXT "%s", name ? name + 1 : app->doc->path);
    const char *levels[] = {T("System"), T("Program"), T("Function"), T("Statement"),
                            T("Memory")};
    draw_header(path, levels, 5, (int)r->level - 1, C_FOCUS);
    draw_status(app, w);
    if (app->overlay == OVERLAY_CANVAS) { /* it goes on running under it */
        draw_canvas(app, w, h);
        const Hint hints[] = {{"→", T("step")}, {"←", T("back")}, {T("Space"), TC("short", "play")},
                              {"g", T("to the end")}, {"d", T("close")}};
        draw_footer(hints, 5);
        return;
    }
    if (app->overlay == OVERLAY_OUTPUT || app->overlay == OVERLAY_LOG) {
        if (app->overlay == OVERLAY_OUTPUT) draw_output(app, w, h);
        else draw_log(app, w, h);
        const Hint hints[] = {{"↑↓", T("scroll")}, {"Esc", T("close")}};
        draw_footer(hints, 2);
        return;
    }
    int bottom = h - 6;
    switch (r->level) {
        case LEVEL_SYSTEM: run_system_draw(app, 0, 2, w, bottom - 2); break;
        case LEVEL_CALL: {
            int left = w * 60 / 100;
            draw_box(0, 2, left, bottom - 2, C_CALL, T("Calls"));
            draw_calls(app, 2, 3, left - 4, bottom - 4);
            draw_box(left, 2, w - left, bottom - 2, C_BORDER, T("Stack: who waits for whom"));
            draw_stack(app, left + 2, 3, w - left - 4, bottom - 4);
            break;
        }
        case LEVEL_EXPRESSION: draw_statement(app, w, h); break;
        case LEVEL_MEMORY: draw_machine(app, w, h); break;
        default: {
            int left = w * 46 / 100;
            const Frame *f = vm_frame(r->vm, vm_frame_count(r->vm) - 1);
            draw_box(0, 2, left, bottom - 2, C_BORDER, f->function ? f->function->full_name : T("Code"));
            if (r->activity && f->function) draw_activity(app, f->function, 2, 3, left - 4, bottom - 4);
            else draw_current_code(app, 1, 3, left - 3, bottom - 4);
            bool over = r->trial && vm_frame_count(r->vm) == 1;
            if (vm_status(r->vm) == VM_FINISHED && !over) {
                draw_box(left, 2, w - left, bottom - 2, C_TYPE, T("What it printed"));
                draw_output_text(app, left + 2, 4, w - left - 4, bottom - 6, 0);
                break;
            }
            draw_box(left, 2, w - left, bottom - 2, C_BORDER, over ? T("In the end") : T("Data"));
            run_data_draw(app, left + 2, 4, w - left - 4, bottom - 6);
        }
    }
    draw_events(app, bottom, w);
    if (vm_status(r->vm) == VM_NEEDS_INPUT) {
        const Hint hints[] = {{T("Enter"), T("confirm")}, {"Ctrl-D", T("end of input")},
                              {"Esc", T("back")}};
        draw_footer(hints, 3);
        return;
    }
    const Hint hints[] = {{"→", T("step")}, {"←", T("back")},
                          {T("Space"), r->playing ? T("pause") : TC("short", "play")},
                          {T("Enter"), T("details")}, {"Esc", T("overview")},
                          {r->level == LEVEL_SYSTEM ? "Tab" : "n", r->level == LEVEL_SYSTEM ? T("other view") : T("over")},
                          {"g", T("to the end")}, {"?", T("help")}};
    draw_footer(hints, 8);
}

/* ---- keys ---- */

static void leave(App *app) {
    app->view = app->run.trial && app->run.arg_count ? VIEW_TRIAL : app->run.test && app->tests.count ? VIEW_TESTS : VIEW_EXPLORE;
    app->run.playing = false;
    app->run.goal = GOAL_NONE;
    if (app->view == VIEW_TRIAL && app->trial.function != app->run.trial) app->view = VIEW_EXPLORE;
}

static void input_key(App *app, int key) {
    Run *r = &app->run;
    size_t n = strlen(r->input);
    if (key == K_ENTER) {
        char line[140];
        snprintf(line, sizeof line, "%s\n", r->input);
        vm_give_input(r->vm, line);
        r->input[0] = '\0';
        aim(app, GOAL_STOP);
    } else if (key == 4) {
        vm_end_input(r->vm);
        aim(app, GOAL_STOP);
    } else if (key == K_BACKSPACE) {
        if (n) r->input[n - 1] = '\0';
    } else if (key == K_LEFT) {
        vm_back(r->vm, r->level);
    } else if (key == K_ESC) {
        leave(app);
    } else if (key >= 32 && key < 127 && n + 1 < 28) {
        r->input[n] = (char)key;
        r->input[n + 1] = '\0';
    }
}

void run_key(App *app, int key) {
    Run *r = &app->run;
    if (app->overlay == OVERLAY_OUTPUT || app->overlay == OVERLAY_LOG) {
        if (key == K_UP) r->overlay_top++;
        else if (key == K_DOWN && r->overlay_top > 0) r->overlay_top--;
        else if (key == K_PAGE_UP) r->overlay_top += 10;
        else if (key == K_PAGE_DOWN) r->overlay_top = r->overlay_top > 10 ? r->overlay_top - 10 : 0;
        else if (key == K_ESC || key == 'o' || key == 'c' || key == 'q' || key == K_ENTER) app->overlay = OVERLAY_NONE;
        return;
    }
    if (app->overlay == OVERLAY_CANVAS && (key == K_ESC || key == 'd' || key == 'q')) {
        app->overlay = OVERLAY_NONE;
        return;
    }
    if (vm_status(r->vm) == VM_NEEDS_INPUT) {
        input_key(app, key);
        return;
    }
    if (r->goal != GOAL_NONE && key != K_NONE) { /* any key interrupts */
        r->goal = GOAL_NONE;
        r->playing = false;
        return;
    }
    switch (key) {
        case K_RIGHT: case 'l': r->playing = false; aim(app, GOAL_STOP); break;
        case K_LEFT: case 'h':
            r->playing = false;
            vm_back(r->vm, r->level);
            break;
        case ' ':
            if (!can_run(r)) break;
            r->playing = !r->playing;
            r->next_tick = 0;
            break;
        case K_UP: case 'k': if (r->speed < SPEEDS - 1) r->speed++; break;
        case K_DOWN: case 'j': if (r->speed > 0) r->speed--; break;
        case 'n': r->playing = false; aim(app, GOAL_OVER); break;
        case 'u': r->playing = false; aim(app, GOAL_OUT); break;
        case 'g': r->playing = false; aim(app, GOAL_END); break;
        case '0': case K_HOME: case 'R': restart(r); break;
        case K_ENTER: case '+': if (r->level < LEVEL_MEMORY) r->level++; break;
        case K_ESC: case '-':
            if (r->level > LEVEL_SYSTEM) r->level--;
            else leave(app);
            break;
        case '1': case '2': case '3': case '4': case '5': r->level = (Level)(key - '0'); break;
        case K_TAB:
            if (r->level == LEVEL_SYSTEM) {
                do r->system = (SystemView)((r->system + 1) % SYS_VIEWS);
                while (!run_system_has(app, r->system));
            } else {
                r->selected_var++;
            }
            break;
        case 'm': r->system_life++; break;
        case 'f': r->activity = !r->activity; break;
        case K_PAGE_UP: r->system_back += 5; break;
        case K_PAGE_DOWN: r->system_back = r->system_back > 5 ? r->system_back - 5 : 0; break;
        case K_SHIFT_TAB: if (r->selected_var > 0) r->selected_var--; break;
        case 'v': r->bars = !r->bars; break;
        case 'b': {
            const Frame *f = vm_frame(r->vm, vm_frame_count(r->vm) - 1);
            if (f->stmt) r->breakpoints[f->stmt->loc.line] = !r->breakpoints[f->stmt->loc.line];
            break;
        }
        case 'o': app->overlay = OVERLAY_OUTPUT; r->overlay_top = 0; break;
        case 'd':
            if (vm_draw_count(r->vm)) app->overlay = OVERLAY_CANVAS;
            break;
        case 'c': app->overlay = OVERLAY_LOG; r->overlay_top = 0; break;
        case 'x': leave(app); break;
        case 'q': app->quit = true; break;
        default: break;
    }
}
