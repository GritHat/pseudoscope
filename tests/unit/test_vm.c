#include "pseudoscope/parser.h"
#include "pseudoscope/vm.h"
#include "unit.h"

#include <stdlib.h>

typedef struct {
    Arena arena;
    StrBuf text;
    Source source;
    Config config;
    Diag diag;
    Program *program;
    Vm *vm;
} Loaded;

static bool load(Loaded *l, const char *text, size_t length) {
    memset(l, 0, sizeof *l);
    strbuf_append(&l->text, text, length);
    l->config = CONFIG_DEFAULT;
    l->source = (Source){"t", l->text.data, l->text.length};
    l->diag = (Diag){.source = &l->source, .out = fopen("/dev/null", "w"), .silent_warnings = true};
    TokenList tokens = lex(&l->source, &l->config, &l->arena, &l->diag);
    l->program = parse(tokens, &l->config, &l->arena, &l->diag);
    if (l->diag.errors || !analyze(l->program, &l->config, &l->arena, &l->diag)) return false;
    l->vm = vm_create(l->program, &l->config);
    return true;
}

static void unload(Loaded *l) {
    vm_destroy(l->vm);
    arena_free(&l->arena);
    strbuf_free(&l->text);
    fclose(l->diag.out);
}

/* Forward to the end, remembering every stop; back to the start; forward again. */
static void travel(const char *name, const char *text, size_t length, Level level, int test) {
    Loaded l;
    if (!load(&l, text, length)) {
        unload(&l);
        return; /* meant for other options */
    }
    enum { MAX = 4000 };
    static uint64_t seen[MAX];
    int count = 0;
    const Function *chosen = NULL;
    for (const Function *f = l.program->functions; f && test >= 0; f = f->next)
        if (f->is_test && test-- == 0) chosen = f;
    if (chosen) vm_start_test(l.vm, chosen);
    else vm_start(l.vm);
    vm_give_input(l.vm, "2 1.5\n2.5 x\n 7\n4\n5\n");
    vm_end_input(l.vm);
    seen[count++] = vm_fingerprint(l.vm);
    while (count < MAX && vm_run(l.vm, level, 1000000) != LEVEL_NONE) seen[count++] = vm_fingerprint(l.vm);

    int wrong = 0;
    for (int i = count - 2; i >= 0; i--) {
        if (!vm_back(l.vm, level)) wrong++;
        else if (vm_fingerprint(l.vm) != seen[i]) wrong++;
    }
    if (vm_back(l.vm, level)) wrong++; /* already at the start */
    for (int i = 1; i < count; i++) {
        vm_run(l.vm, level, 1000000);
        if (vm_fingerprint(l.vm) != seen[i]) wrong++;
    }
    unit_checks++;
    if (wrong) {
        unit_failures++;
        fprintf(stderr, "%s: level %d: %d of %d states differ\n", name, (int)level, wrong, count);
    }
    unload(&l);
}

static void test_travel(void) {
    for (int i = 1; i < unit_argc; i++) {
        FILE *file = fopen(unit_argv[i], "rb");
        CHECK(file != NULL);
        if (!file) continue;
        StrBuf text = {0};
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, file)) > 0) strbuf_append(&text, chunk, n);
        fclose(file);
        for (int level = LEVEL_SYSTEM; level <= LEVEL_MEMORY; level++)
            travel(unit_argv[i], text.data, text.length, (Level)level, -1);
        Loaded l;
        int tests = 0;
        if (load(&l, text.data, text.length))
            for (const Function *f = l.program->functions; f; f = f->next) tests += f->is_test;
        unload(&l);
        for (int test = 0; test < tests; test++)
            for (int level = LEVEL_SYSTEM; level <= LEVEL_STATEMENT; level++)
                travel(unit_argv[i], text.data, text.length, (Level)level, test);
        strbuf_free(&text);
    }
}

static const Function *find(const Program *program, const char *name) {
    for (const Function *f = program->functions; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

static void test_trial(void) {
    static const char TEXT[] =
        "procedure sort(in: A, n; out: swaps)\n"
        "var A: array[1..n] of integer\nvar n, i, j, t, swaps: integer\nbegin\n"
        "swaps := 0\n"
        "for i := 1 to n - 1 do for j := 1 to n - i do if A(j) > A(j+1) then\n"
        "t := A(j); A(j) := A(j+1); A(j+1) := t; swaps := swaps + 1 endif endfor endfor\n"
        "end sort\n";
    Loaded l;
    CHECK(load(&l, TEXT, sizeof TEXT - 1));
    const Function *sort = find(l.program, "sort");
    double values[] = {5, 3, 4, 1}, n = 4;
    TrialArgument args[] = {{values, 4}, {&n, 1}, {NULL, 0}};
    bool open;
    CHECK(vm_trial_size(&l.config, sort, args, 0, &open) == 4 && !open);
    CHECK(vm_start_trial(l.vm, sort, args));
    while (vm_run(l.vm, LEVEL_STATEMENT, 100000) != LEVEL_NONE) {}
    CHECK(vm_status(l.vm) == VM_FINISHED);
    const Var *array = vm_trial_var(l.vm, 0), *swaps = vm_trial_var(l.vm, 2);
    Pointer p = array->at;
    const int want[] = {1, 3, 4, 5};
    for (int i = 0; i < 4; i++, p.offset++) CHECK(vm_cell(l.vm, p)->as.i == want[i]);
    CHECK(vm_cell(l.vm, swaps->at)->as.i == 5);
    char name[32];
    p.offset = 2;
    vm_place_name(l.vm, p, name, sizeof name);
    CHECK_STR(name, "A[3]");
    n = -1;
    CHECK(!vm_start_trial(l.vm, sort, args));
    unload(&l);
}

static void test_failures(void) {
    static const struct {
        const char *body, *message;
    } CASES[] = {
        {"x := 1 / (x - x)", "division by zero"},
        {"p := 0\n*p := 1", "null pointer"},
        {"x := a[4]", "array index out of bounds"},
        {"p := &a[3]\nx := *(p + 1)", "access outside of the variable"},
        {"begin var y: integer\n y := 1\n p := &y end\nx := *p", "the variable pointed to no longer exists"},
        {"x := 1 << 40", "shift by a negative number, or by more bits than there are"},
        {"forever()", "too many nested calls: is the recursion ever going to end?"},
        {"unknown(x)", "this function is not defined in the file: it cannot be executed"},
    };
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        char text[600];
        snprintf(text, sizeof text,
                 "procedure forever()\nbegin\nforever()\nend forever\nprocedure main()\nvar x: integer\n"
                 "var p: integer*\nvar a: array[3] of integer\nbegin\nx := 1\n%s\nend main\n", CASES[i].body);
        Loaded l;
        CHECK(load(&l, text, strlen(text)));
        vm_start(l.vm);
        while (vm_run(l.vm, LEVEL_NONE, 1000000), vm_status(l.vm) == VM_READY) {}
        CHECK(vm_status(l.vm) == VM_FAILED);
        CHECK_STR(vm_message(l.vm), CASES[i].message);
        unload(&l);
    }
}

static void test_reduction(void) {
    static const char TEXT[] = "procedure main()\nvar a: array[3] of integer\nvar i: integer\nbegin\n"
                               "i := 2\na[i] := 5\ni := (a[i] + 1) * i\nend main\n";
    Loaded l;
    CHECK(load(&l, TEXT, sizeof TEXT - 1));
    vm_start(l.vm);
    for (int i = 0; i < 4; i++) vm_run(l.vm, LEVEL_STATEMENT, 1000); /* before the last one */
    const Expr *expr = vm_frame(l.vm, 1)->stmt->value;
    static const char *const WANT[] = {
        "(a[<2>] + 1) * i", "(<5> + 1) * i", "<6> * i", "6 * <2>", "<12>",
    };
    for (int i = 0; i < 5; i++) {
        vm_run(l.vm, LEVEL_EXPRESSION, 1000);
        int count;
        const Eval *evals = vm_evals(l.vm, &count);
        StrBuf out = {0};
        print_expr(l.vm, expr, evals, count, "<", ">", "", "", &out);
        CHECK_STR(out.data, WANT[i]);
        strbuf_free(&out);
    }
    unload(&l);
}

void test_vm(void) {
    test_trial();
    test_failures();
    test_reduction();
    test_travel();
}
