#include "ui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The system as a whole: who talks to whom, what is on its way, the states
 * everybody is in, and what the test has verified so far. */

bool run_system_has(const App *app, SystemView view) {
    const Program *program = app->doc->program;
    switch (view) {
        case SYS_COMPONENTS: return has_components(program);
        case SYS_OBJECTS: return program->classes && !has_components(program);
        case SYS_COMMUNICATION: return program->classes != NULL;
        case SYS_STATES: case SYS_TIMING:
            for (const Class *c = program->classes; c; c = c->next)
                if (c->is_component && c->states) return true;
            return false;
        case SYS_CHECKS: return app->run.test != NULL;
        default: return true;
    }
}

static const char *view_name(SystemView view) {
    switch (view) {
        case SYS_COMPONENTS: return T("Components");
        case SYS_STATES: return T("States");
        case SYS_TIMING: return T("Timing");
        case SYS_COMMUNICATION: return T("Communication");
        case SYS_OBJECTS: return T("Objects");
        case SYS_CHECKS: return T("Checks");
        default: return T("Sequence");
    }
}

static int draw_tabs(App *app, int x, int y, int w) {
    for (int v = 0; v < SYS_VIEWS; v++) {
        if (!run_system_has(app, (SystemView)v)) continue;
        bool on = app->run.system == (SystemView)v;
        const char *name = view_name((SystemView)v);
        x += print_at(x, y, -1, on ? S(C_BRIGHT, C_SELECT, A_BOLD) : S_DIM, on ? " %s " : " %s ", name) + 1;
    }
    const Vm *vm = app->run.vm;
    if (has_components(app->doc->program))
        print_at(w - 12 - (w - x > 24 ? 10 : 0), y, 20, S_DIM, w - x > 24 ? "%s " M_FOCUS M_BOLD "t = %ld" : "%.0s" M_FOCUS M_BOLD "t = %ld",
                 T("time"), vm_time(vm));
    return x;
}

/* ---- sequence ---- */

#define MAX_ROWS 600
#define MAX_LIVES 64

static void draw_live_sequence(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    static SequenceItem rows[MAX_ROWS];
    static char labels[MAX_ROWS][100];
    const char *lives[MAX_LIVES];
    bool kinds[MAX_LIVES];
    int count = vm_life_count(vm), total = vm_message_count(vm);
    if (count > MAX_LIVES) count = MAX_LIVES;
    /* the program itself only appears if it takes part */
    int skip = 1;
    int begin = total > MAX_ROWS ? total - MAX_ROWS : 0;
    for (int i = begin; i < total; i++) {
        const SequenceRow *m = vm_message_at(vm, i);
        bool real = m->kind != MSG_STEP && m->kind != MSG_CHECK;
        if (real && (m->from == 0 || m->to == 0)) skip = 0;
    }
    for (int i = 0; i < vm_queue_count(vm); i++)
        if (vm_queued(vm, i)->from_life == 0 && strcmp(vm_queued(vm, i)->name, "start") != 0) skip = 0;
    if (count <= 1) skip = 0;
    for (int i = skip; i < count; i++) {
        lives[i - skip] = vm_life(vm, i)->name;
        kinds[i - skip] = vm_life(vm, i)->klass && vm_life(vm, i)->klass->is_component;
    }
    bool timed = has_components(app->doc->program);
    int n = 0;
    for (int i = begin; i < total; i++, n++) {
        const SequenceRow *m = vm_message_at(vm, i);
        SequenceItem *r = &rows[n];
        *r = (SequenceItem){.from = m->from - skip, .to = m->to - skip, .time = timed ? m->time : -1, .ok = m->ok,
                            .end = m->end >= INT_MAX - 1 ? INT_MAX : m->end - begin};
        snprintf(labels[n], sizeof labels[n], m->args[0] ? "%.40s(%s)" : "%.40s%s", m->name, m->args);
        switch (m->kind) {
            case MSG_CALL: r->activates = true; break;
            case MSG_DELIVER: r->activates = true; break;
            case MSG_RETURN:
                r->kind = SEQ_RETURN;
                r->dashed = true;
                snprintf(labels[n], sizeof labels[n], "%s", m->args);
                break;
            case MSG_LOST: r->kind = SEQ_LOST; break;
            case MSG_IGNORED: r->kind = SEQ_IGNORED; break;
            case MSG_STATE:
                r->kind = SEQ_STATE;
                snprintf(labels[n], sizeof labels[n], "%s", m->name);
                break;
            case MSG_STEP:
                r->kind = SEQ_STEP;
                snprintf(labels[n], sizeof labels[n], "%s", m->name);
                break;
            case MSG_CHECK:
                r->kind = SEQ_CHECK;
                snprintf(labels[n], sizeof labels[n], "%s", m->args);
                break;
        }
        r->label = labels[n];
    }
    SequenceFlying flying[12];
    char names[12][60];
    int flying_count = 0, room = h / 4, queued = vm_queue_count(vm);
    if (room > 12) room = 12;
    /* messages first, then timers */
    for (int k = 0; k < 2 * queued && flying_count < room; k++) {
        const Event *e = vm_queued(vm, k % queued);
        if (strcmp(e->name, "start") == 0 || (e->from_life == e->to_life) != (k >= queued)) continue;
        double span = (double)(e->time - e->sent);
        snprintf(names[flying_count], sizeof names[0], "%.50s", e->name);
        flying[flying_count] = (SequenceFlying){e->from_life - skip, e->to_life - skip, names[flying_count],
                                                span > 0 ? (double)(vm_time(vm) - e->sent) / span : 0, e->time};
        if (flying[flying_count].progress < 0.15) flying[flying_count].progress = 0.15;
        flying_count++;
    }
    SequenceView view = {.lives = lives, .kinds = kinds, .life_count = count - skip, .current = vm_current_life(vm) - skip,
                         .rows = rows, .row_count = n, .back = app->run.system_back, .flying = flying,
                         .flying_count = flying_count};
    if (view.life_count <= 0) {
        print_at(x + 2, y + 2, w - 4, S_DIM, "%s", T("Nobody has talked to anybody yet."));
        return;
    }
    draw_sequence(x, y, w, h, &view);
}

/* ---- components ---- */

static void component_card(App *app, int index, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    const Class *klass;
    Pointer at = vm_instance(vm, index, &klass);
    Life life = {.at = at, .klass = klass};
    const char *state = vm_state_name(vm, &life);
    char name[40];
    vm_place_name(vm, at, name, sizeof name);
    int current = vm_current_life(vm);
    bool acting = current > 0 && vm_life(vm, current)->at.object == at.object && vm_life(vm, current)->at.offset == at.offset;
    draw_box(x, y, w, h, acting ? C_CALL : C_TYPE, NULL);
    print_at(x + 2, y, w - 4, S(acting ? C_CALL : C_TEXT, C_DEFAULT, A_BOLD), " %s ", name);
    print_at(x + 2, y + 1, w - 4, S_FAINT, "%s", klass->name);
    int row = y + 1;
    if (state) print_at(x + w - text_width(state) - 4, row, text_width(state) + 2, S(16, C_FOCUS_BG, A_BOLD), " %s ", state);
    row++;
    for (const VarDecl *f = klass->record->fields; f && row < y + h - 1; f = f->next) {
        const Type *type = f->symbol->type;
        if (type->states) continue;
        char text[120] = "";
        Pointer p = at;
        p.offset += f->symbol->slot;
        if (type->kind == TY_ARRAY) {
            int n = vm_slots(&app->doc->config, type);
            for (int k = 0; k < n && k < 12; k++) {
                char one[30];
                Pointer q = p;
                q.offset += k;
                const Value *v = vm_cell(vm, q);
                if (v) vm_format(vm, v, vm_slot_type(&app->doc->config, type, k, NULL), one, sizeof one);
                size_t used = strlen(text);
                snprintf(text + used, sizeof text - used, "%s%s", k ? " " : "", v ? one : "?");
            }
        } else if (type->kind == TY_RECORD) {
            snprintf(text, sizeof text, "{...}");
        } else {
            const Value *v = vm_cell(vm, p);
            if (v) vm_format(vm, v, type, text, sizeof text);
        }
        int touches;
        const Touch *touched = vm_touched(vm, &touches);
        bool written = false;
        for (int k = 0; k < touches; k++)
            written |= touched[k].write && touched[k].at.object == p.object && touched[k].at.offset == p.offset;
        int used = print_at(x + 2, row, w / 2, S_DIM, "%s ", f->name);
        print_at(x + 2 + used, row, w - used - 4, written ? S(231, C_WRITE_BG, A_BOLD) : S_TEXT, written ? " %s " : "%s", text);
        row++;
    }
}

/* One lane for each pair that has something on its way. */
static void draw_lanes(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int shown = 0, waiting = 0;
    bool done[256] = {false};
    int n = vm_queue_count(vm);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        const Event *e = vm_queued(vm, i);
        if (done[i]) continue;
        if (e->from_life == e->to_life || e->from_life == 0) {
            waiting++;
            continue;
        }
        if (shown * 2 + 1 >= h) break;
        int row = y + shown * 2, left = x + 16, right = x + w - 18;
        print_at(x, row, 15, S(C_TEXT, C_DEFAULT, A_BOLD), "%15.15s", vm_life(vm, e->from_life)->name);
        put_text(left - 1, row, S_DIM, "├");
        hline(left, row, right - left, S_FAINT, "┄");
        put_text(right, row, S(C_READ, C_DEFAULT, A_BOLD), "▶");
        print_at(right + 2, row, 15, S(C_TEXT, C_DEFAULT, A_BOLD), "%s", vm_life(vm, e->to_life)->name);
        for (int k = i; k < n; k++) {
            const Event *f = vm_queued(vm, k);
            if (f->from_life != e->from_life || f->to_life != e->to_life) continue;
            done[k] = true;
            double span = (double)(f->time - f->sent), p = span > 0 ? (double)(vm_time(vm) - f->sent) / span : 0;
            if (f->lost) p /= 2;
            char label[60];
            int width = snprintf(label, sizeof label, " %.30s ", f->name);
            int at = left + 1 + (int)((right - left - width - 1) * p);
            print_at(at, row, width, S(231, C_READ_BG, A_BOLD), "%s", label);
        }
        shown++;
    }
    if (shown == 0) print_at(x + 2, y, w - 4, S_FAINT, "%s", T("No message on its way."));
    if (waiting && shown * 2 + 1 < h) {
        int at = x + 2 + print_at(x + 2, y + (shown ? shown * 2 : 2), w, S_DIM, "%s ", T("waiting:"));
        for (int i = 0; i < n; i++) {
            const Event *e = vm_queued(vm, i);
            if ((e->from_life != e->to_life && e->from_life != 0) || at > x + w - 24) continue;
            at += print_at(at, y + (shown ? shown * 2 : 2), 30, S(C_POINTER, C_DEFAULT, 0), "◷ %s.%s →%ld  ", vm_life(vm, e->to_life)->name,
                           e->name, e->time);
        }
    }
}

static void draw_components(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int count = vm_instance_count(vm), per = w / 30;
    if (per < 1) per = 1;
    if (count < per) per = count;
    if (per < 1) return;
    int rows = (count + per - 1) / per, height = 7;
    if (rows * height > h * 2 / 3) height = h * 2 / 3 / rows;
    if (height < 4) height = 4;
    int width = w / per;
    for (int i = 0; i < count; i++) {
        int cy = y + i / per * height;
        if (cy + height > y + h) break;
        component_card(app, i, x + i % per * width, cy, width - 1, height);
    }
    int lanes = y + rows * height + 1;
    if (lanes + 2 < y + h) {
        print_at(x, lanes, w, S_BOLD, "%s", T("On their way"));
        draw_lanes(app, x, lanes + 2, w, y + h - lanes - 2);
    }
}

/* ---- states ---- */

static void one_chart(App *app, int index, int x, int y, int w, int h, bool titled) {
    const Vm *vm = app->run.vm;
    const Class *klass;
    Life life = {.at = vm_instance(vm, index, &klass)};
    life.klass = klass;
    const char *names[64], *now = vm_state_name(vm, &life);
    StateArrow arrows[96];
    StateView view = {.states = names, .arrows = arrows, .current = -1, .last = -1, .selected = -1};
    for (const StateName *s = klass->states; s && view.state_count < 64; s = s->next) {
        if (now && strcmp(now, s->name) == 0) view.current = view.state_count;
        names[view.state_count++] = s->name;
    }
    view.arrow_count = class_transitions(app->doc, klass, arrows, 96);
    /* how it got here: the last change, and the message before it */
    const char *before = NULL, *cause = NULL;
    for (int i = vm_message_count(vm) - 1; i >= 0; i--) {
        const SequenceRow *m = vm_message_at(vm, i);
        const Life *l = m->to > 0 ? vm_life(vm, m->to) : NULL;
        bool mine = l && l->at.object == life.at.object && l->at.offset == life.at.offset;
        if (!mine) continue;
        if (m->kind == MSG_STATE && !before) {
            before = m->args;
        } else if (m->kind == MSG_DELIVER && before) {
            cause = m->name;
            break;
        }
    }
    for (int i = 0; before && cause && i < view.arrow_count; i++)
        if (strcmp(names[arrows[i].from], before) == 0 && arrows[i].to == view.current && strcmp(arrows[i].label, cause) == 0) view.last = i;
    if (titled) {
        char name[40];
        vm_place_name(vm, life.at, name, sizeof name);
        print_at(x, y, w, S_BOLD, "%s" M_RESET M_FAINT "  %s", name, klass->name);
        y += 2;
        h -= 2;
    }
    draw_states(x, y, w, h, &view);
}

static void draw_component_states(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int count = vm_instance_count(vm), with = 0, chosen[64];
    for (int i = 0; i < count && with < 64; i++) {
        const Class *klass;
        vm_instance(vm, i, &klass);
        if (klass->states) chosen[with++] = i;
    }
    if (!with) return;
    int pick = app->run.system_life % with, cx = x;
    for (int i = 0; i < with; i++) { /* everybody's state, in a line */
        const Class *klass;
        Life life = {.at = vm_instance(vm, chosen[i], &klass)};
        life.klass = klass;
        char name[40];
        vm_place_name(vm, life.at, name, sizeof name);
        int need = text_width(name) + text_width(vm_state_name(vm, &life)) + 6;
        if (cx + need > x + w) break;
        cx += print_at(cx, y, -1, i == pick ? S(C_BRIGHT, C_SELECT, A_BOLD) : S_DIM, " %s " M_CHIP_FOCUS " %s ", name, vm_state_name(vm, &life)) + 2;
    }
    /* as many charts side by side as there is room for */
    int tiles = w / 46;
    if (tiles < 1) tiles = 1;
    if (tiles > with) tiles = with;
    int width = w / tiles;
    for (int k = 0; k < tiles; k++) {
        int index = chosen[(pick + k) % with];
        if (k > 0) vline(x + k * width - 1, y + 2, h - 2, S_FAINT, "│");
        one_chart(app, index, x + k * width + 1, y + 2, width - 3, h - 2, tiles > 1);
    }
}

/* ---- checks ---- */

static void draw_checks(App *app, int x, int y, int w, int h) {
    const Vm *vm = app->run.vm;
    int row = y, total = vm_check_count(vm), passed = 0;
    for (int i = 0; i < total; i++) passed += vm_check(vm, i)->ok;
    const char *title = app->run.test ? app->run.test->name : "";
    print_at(x, row, w, S_BOLD, "%s", title);
    VmStatus status = vm_status(vm);
    bool over = status == VM_FINISHED || status == VM_FAILED, ok = passed == total && status != VM_FAILED;
    print_at(x, row + 1, w, S_TEXT, "%s %s " M_RESET M_DIM "  %d / %d", !over ? M_CHIP_READ : ok ? M_CHIP_WRITE " ✓" : M_CHIP_ERROR " ✗",
             !over ? TC("run", "running") : ok ? TC("run", "passed") : TC("run", "failed"), passed, total);
    row += 3;
    /* steps and checks in the order they came */
    int lines = 0, rows = vm_message_count(vm);
    for (int i = 0; i < rows; i++) {
        MessageKind k = vm_message_at(vm, i)->kind;
        lines += k == MSG_STEP || k == MSG_CHECK;
    }
    int room = y + h - row, skip = lines > room ? lines - room : 0, n = 0;
    for (int i = 0; i < rows && row < y + h; i++) {
        const SequenceRow *m = vm_message_at(vm, i);
        if (m->kind != MSG_STEP && m->kind != MSG_CHECK) continue;
        if (n++ < skip) continue;
        if (m->kind == MSG_STEP) print_at(x, row++, w, S(C_FOCUS, C_DEFAULT, A_BOLD), "● %s", m->name);
        else print_at(x + 3, row++, w - 3, S_TEXT, "%s" M_RESET " %s" M_FAINT "   %s %d", m->ok ? M_WRITE M_BOLD "✓" : M_ERROR M_BOLD "✗", m->args,
                      T("line"), m->stmt ? m->stmt->loc.line : 0);
    }
}

void run_system_draw(App *app, int x, int y, int w, int h) {
    if (!run_system_has(app, app->run.system)) app->run.system = SYS_SEQUENCE;
    draw_tabs(app, x + 1, y, x + w);
    draw_box(x, y + 1, w, h - 1, C_CALL, NULL);
    int ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 3;
    switch (app->run.system) {
        case SYS_COMPONENTS: draw_components(app, ix, iy, iw, ih); break;
        case SYS_STATES: draw_component_states(app, ix, iy, iw, ih); break;
        case SYS_TIMING: draw_timing(app, ix, iy, iw, ih); break;
        case SYS_COMMUNICATION: draw_communication(app, ix, iy, iw, ih); break;
        case SYS_OBJECTS: draw_objects(app, ix, iy, iw, ih); break;
        case SYS_CHECKS: draw_checks(app, ix, iy, iw, ih); break;
        default: draw_live_sequence(app, ix, iy, iw, ih);
    }
}

/* ---- all the tests ---- */

void tests_close(App *app) {
    free(app->tests.results);
    app->tests = (Tests){0};
}

static void run_one(App *app, TestResult *t) {
    Vm *vm = vm_create(app->doc->program, &app->doc->config);
    vm_start_test(vm, t->test);
    vm_run(vm, LEVEL_NONE, 5000000);
    t->ran = true;
    t->checks = vm_check_count(vm);
    t->failed = 0;
    for (int i = 0; i < t->checks; i++) {
        t->failed += !vm_check(vm, i)->ok;
        if (i < 64) t->items[i] = *vm_check(vm, i);
    }
    t->ok = vm_status(vm) == VM_FINISHED && !t->failed;
    if (vm_status(vm) == VM_FAILED) snprintf(t->message, sizeof t->message, "%s", tr(vm_message(vm)));
    else if (vm_status(vm) != VM_FINISHED) snprintf(t->message, sizeof t->message, "%s", T("it does not end"));
    vm_destroy(vm);
}

void tests_open(App *app) {
    tests_close(app);
    Tests *t = &app->tests;
    int n = 0;
    for (const Function *f = app->doc->program->functions; f; f = f->next) n += f->is_test;
    t->results = calloc((size_t)n + 1, sizeof(TestResult));
    if (!t->results) abort();
    for (const Function *f = app->doc->program->functions; f; f = f->next) {
        if (!f->is_test) continue;
        t->results[t->count].test = f;
        run_one(app, &t->results[t->count++]);
    }
    app->view = VIEW_TESTS;
}

void tests_draw(App *app) {
    Tests *t = &app->tests;
    int w = screen_width(), h = screen_height(), list = w * 45 / 100, passed = 0;
    char path[200];
    const char *name = strrchr(app->doc->path, '/');
    snprintf(path, sizeof path, M_TEXT "%s" M_FAINT " ▸ " M_TEXT "%s", name ? name + 1 : app->doc->path, T("tests"));
    draw_header(path, NULL, 0, 0, 0);
    for (int i = 0; i < t->count; i++) passed += t->results[i].ok;
    draw_box(0, 2, list, h - 4, passed == t->count ? C_WRITE : C_ERROR, T("Tests"));
    print_at(2, 3, list - 4, S_DIM, M_WRITE M_BOLD "%d" M_RESET M_DIM " / %d %s", passed, t->count, T("passed"));
    int top = scroll_to(0, t->selected, 0, t->count - 1, h - 8);
    for (int i = top; i < t->count && i - top < h - 8; i++) {
        const TestResult *r = &t->results[i];
        draw_table_row(1, 5 + i - top, list - 2, i == t->selected, "%s" M_RESET " %s", r->ok ? M_WRITE "✓" : M_ERROR "✗", r->test->name);
    }
    draw_box(list, 2, w - list, h - 4, C_BORDER, T("What it checks"));
    if (t->count) {
        const TestResult *r = &t->results[t->selected];
        int row = 4;
        for (int i = 0; i < r->checks && i < 64 && row < h - 4; i++)
            print_at(list + 3, row++, w - list - 6, S_TEXT, "%s" M_RESET " %s", r->items[i].ok ? M_WRITE "✓" : M_ERROR "✗", r->items[i].text);
        if (r->message[0]) print_at(list + 3, row++, w - list - 6, S(C_ERROR, C_DEFAULT, A_BOLD), "✗ %s", r->message);
    }
    const Hint hints[] = {{"↑↓", T("choose")}, {T("Enter"), T("watch it run")},
                          {"Esc", T("back")}, {"?", T("help")}};
    draw_footer(hints, 4);
}

void tests_key(App *app, int key) {
    Tests *t = &app->tests;
    if ((key == K_UP || key == 'k') && t->selected > 0) t->selected--;
    else if ((key == K_DOWN || key == 'j') && t->selected < t->count - 1) t->selected++;
    else if (key == K_ENTER && t->count) run_open_test(app, t->results[t->selected].test);
    else if (key == K_ESC || key == K_BACKSPACE) app->view = VIEW_EXPLORE;
    else if (key == 'q') app->quit = true;
}
