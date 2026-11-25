#include "plike/cli.h"

#include "plike/codegen.h"
#include "plike/dump.h"
#include "plike/parser.h"
#include "plike/ui.h"
#include "plike/uml.h"
#include "plike/vm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool read_file(const char *path, StrBuf *text) {
    bool is_stdin = strcmp(path, "-") == 0;
    FILE *file = is_stdin ? stdin : fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "pseudoscope: cannot open '%s': %s\n", path, strerror(errno));
        return false;
    }
    char chunk[8192];
    size_t n;
    strbuf_puts(text, "");
    while ((n = fread(chunk, 1, sizeof chunk, file)) > 0) strbuf_append(text, chunk, n);
    bool ok = !ferror(file);
    if (!ok) fprintf(stderr, "pseudoscope: cannot read '%s': %s\n", path, strerror(errno));
    if (!is_stdin) fclose(file);
    return ok;
}

static bool write_file(const char *path, const StrBuf *text) {
    bool is_stdout = !path || strcmp(path, "-") == 0;
    FILE *file = is_stdout ? stdout : fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "pseudoscope: cannot write '%s': %s\n", path, strerror(errno));
        return false;
    }
    bool ok = fwrite(text->data, 1, text->length, file) == text->length;
    ok &= (is_stdout ? fflush(file) : fclose(file)) == 0;
    if (!ok) fprintf(stderr, "pseudoscope: cannot write '%s'\n", is_stdout ? "standard output" : path);
    return ok;
}

static bool same_file(const char *a, const char *b) {
    struct stat first, second;
    return stat(a, &first) == 0 && stat(b, &second) == 0 &&
           first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}

/* Runs a program and returns its exit status, or -1 if it could not start. */
static int execute(const char *const *args) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(args[0], (char *const *)args);
        fprintf(stderr, "pseudoscope: cannot run '%s': %s\n", args[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "pseudoscope: '%s' was killed by signal %d\n", args[0], WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}

/* The command that compiles, or runs, programs in a language. */
static const char *tool_for(const Options *options, Lang lang, Arena *arena) {
    static const char *const VARIABLES[LANG_COUNT] = {"CC", "CXX", "JAVA", "RUSTC", "NODE", "NODE", "PYTHON"};
    static const char *const DEFAULTS[LANG_COUNT] = {"cc", "c++", "java", "rustc", "node", "node", "python3"};
    const char *tool = options->cc ? options->cc : getenv(VARIABLES[lang]);
    if (tool && *tool) return tool;
    if (lang == LANG_RUST && system("command -v rustc > /dev/null 2>&1") != 0) { /* rustup does not always reach the PATH */
        const char *home = getenv("HOME");
        char *cargo = arena_printf(arena, "%s/.cargo/bin/rustc", home ? home : "");
        if (access(cargo, X_OK) == 0) return cargo;
    }
    return DEFAULTS[lang];
}

static bool compile_source(const Options *options, Lang lang, const char *source, const char *executable, Arena *arena) {
    const char *tool = tool_for(options, lang, arena);
    const char *const c[] = {tool, "-std=c11", "-O1", "-o", executable, source, "-lm", NULL};
    const char *const cpp[] = {tool, "-std=c++17", "-O1", "-o", executable, source, NULL};
    const char *const rust[] = {tool, "--edition=2021", "-A", "warnings", "-o", executable, source, NULL};
    if (options->verbose) fprintf(stderr, "pseudoscope: compiling with %s\n", tool);
    if (execute(lang == LANG_C ? c : lang == LANG_CPP ? cpp : rust) == 0) return true;
    fprintf(stderr, "pseudoscope: the %s compiler failed\n", lang_name(lang));
    return false;
}

/* Where `build` puts the executable when not told. */
static char *default_executable(Arena *arena, const char *input) {
    if (strcmp(input, "-") == 0) return arena_strdup(arena, "a.out");
    const char *slash = strrchr(input, '/');
    const char *dot = strrchr(input, '.');
    if (dot && dot != input && (!slash || dot > slash + 1))
        return arena_strndup(arena, input, (size_t)(dot - input));
    return arena_printf(arena, "%s.out", input);
}

static int no_main(void) {
    fputs("pseudoscope: there is nothing to run: declare a procedure 'main' without parameters\n", stderr);
    return EXIT_PROBLEMS;
}

static bool is_compiled(Lang lang) { return lang == LANG_C || lang == LANG_CPP || lang == LANG_RUST; }

/* `argument`: given to the program, or NULL. */
static int build_and_run(const Options *options, Lang lang, const StrBuf *code, Arena *arena, const char *class_name,
                         const char *argument) {
    const char *base = getenv("TMPDIR");
    char *dir = arena_printf(arena, "%s/pseudoscope-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir)) {
        fprintf(stderr, "pseudoscope: cannot create a temporary directory: %s\n", strerror(errno));
        return EXIT_IO;
    }
    char *source = lang == LANG_JAVA ? arena_printf(arena, "%s/%s.java", dir, class_name)
                                     : arena_printf(arena, "%s/program%s", dir, lang_extension(lang));
    char *executable = arena_printf(arena, "%s/program", dir);
    int status = EXIT_IO;

    if (!write_file(source, code)) {
        /* reported */
    } else if (lang == LANG_TS && !options->cc && system("command -v tsc > /dev/null 2>&1") == 0) {
        /* checked and turned into JavaScript by its compiler */
        char *script = arena_printf(arena, "%s/program.js", dir);
        const char *const tsc[] = {"tsc", "--target", "es2022", "--module", "commonjs", "--strict", "--outDir", dir, source, NULL};
        const char *const node[] = {tool_for(options, LANG_JS, arena), script, argument, NULL};
        if (options->verbose) fputs("pseudoscope: compiling with tsc\n", stderr);
        if (execute(tsc) != 0) {
            fputs("pseudoscope: the TypeScript compiler failed\n", stderr);
        } else {
            status = execute(node);
            if (status < 0) status = EXIT_IO;
        }
        unlink(script);
    } else if (!is_compiled(lang)) {
        const char *tool = tool_for(options, lang, arena);
        const char *const plain[] = {tool, source, argument, NULL};
        const char *const stripped[] = {tool, "--no-warnings", "--experimental-strip-types", source, argument, NULL};
        if (options->verbose) fprintf(stderr, "pseudoscope: running with %s\n", tool);
        status = execute(lang == LANG_TS && !options->cc ? stripped : plain);
        if (status < 0) status = EXIT_IO;
    } else if (compile_source(options, lang, source, executable, arena)) {
        if (options->command != CMD_BUILD) {
            const char *const args[] = {executable, argument, NULL};
            if (options->verbose) fputs("pseudoscope: running\n", stderr);
            status = execute(args);
            if (status < 0) status = EXIT_IO;
        } else {
            const char *target = options->output ? options->output
                                                 : default_executable(arena, options->input);
            const char *const args[] = {"cp", "--", executable, target, NULL};
            if (same_file(options->input, target))
                fputs("pseudoscope: the executable would overwrite the input\n", stderr);
            else if (execute(args) == 0)
                status = EXIT_OK;
        }
    }
    unlink(source);
    unlink(executable);
    rmdir(dir);
    return status;
}

/* The name of the class that Java wants around a program: that of the file. */
static const char *class_name_for(const Options *options, Arena *arena) {
    const char *path = options->command == CMD_TRANSLATE ? options->output : NULL;
    if (!path) return "Main";
    const char *slash = strrchr(path, '/'), *start = slash ? slash + 1 : path, *dot = strrchr(start, '.');
    size_t length = dot ? (size_t)(dot - start) : strlen(start);
    bool ok = length > 0 && ((start[0] >= 'A' && start[0] <= 'Z') || (start[0] >= 'a' && start[0] <= 'z') || start[0] == '_');
    for (size_t i = 0; ok && i < length; i++)
        ok = (start[i] >= 'A' && start[i] <= 'Z') || (start[i] >= 'a' && start[i] <= 'z') || (start[i] >= '0' && start[i] <= '9') ||
             start[i] == '_';
    return ok ? arena_strndup(arena, start, length) : "Main";
}

/* Runs on the virtual machine, reading input only when it is asked for. */
static int interpret(const Program *program, const Options *options) {
    Vm *vm = vm_create(program, &options->config);
    size_t shown = 0, length;
    vm_start(vm);
    long left = options->max_steps;
    for (;;) {
        long chunk = options->max_steps && left < 100000 ? left : 100000;
        vm_run(vm, LEVEL_NONE, chunk);
        left -= chunk;
        const char *output = vm_output(vm, &length);
        fwrite(output + shown, 1, length - shown, stdout);
        shown = length;
        if (vm_status(vm) == VM_NEEDS_INPUT) {
            char line[4096];
            fflush(stdout);
            if (fgets(line, sizeof line, stdin)) vm_give_input(vm, line);
            else vm_end_input(vm);
        } else if (vm_status(vm) != VM_READY || (options->max_steps && left <= 0)) {
            break;
        }
    }
    fflush(stdout);
    if (vm_status(vm) == VM_READY) {
        fputs("pseudoscope: too many steps, stopped\n", stderr);
        vm_destroy(vm);
        return EXIT_PROBLEMS;
    }
    if (vm_status(vm) == VM_FAILED) {
        const Frame *frame = vm_frame(vm, vm_frame_count(vm) - 1);
        fprintf(stderr, "runtime error");
        if (frame->stmt) fprintf(stderr, " at line %d", frame->stmt->loc.line);
        fprintf(stderr, ": %s\n", vm_message(vm));
    }
    int status = vm_exit_status(vm);
    vm_destroy(vm);
    return status;
}

static const char *says(bool it, const char *italian_text, const char *english) { return it ? italian_text : english; }

/* Runs every "test ... end test", and reports what they checked. */
static int run_tests(const Program *program, const Options *options, bool it) {
    Vm *vm = vm_create(program, &options->config);
    int failed = 0, total = 0;
    long limit = options->max_steps ? options->max_steps : 20000000;
    for (const Function *f = program->functions; f; f = f->next) {
        if (!f->is_test) continue;
        total++;
        vm_start_test(vm, f);
        vm_run(vm, LEVEL_NONE, limit);
        bool ok = vm_status(vm) == VM_FINISHED;
        for (int i = 0; i < vm_check_count(vm); i++) ok &= vm_check(vm, i)->ok;
        printf("%s %s\n", ok ? "✓" : "✗", f->name);
        failed += !ok;
        for (int i = 0; i < vm_check_count(vm); i++) {
            const Check *c = vm_check(vm, i);
            if (!c->ok || options->verbose)
                printf("    %s %s %d: %s\n", c->ok ? "✓" : "✗", says(it, "riga", "line"), c->stmt->loc.line, c->text);
        }
        if (vm_status(vm) == VM_FAILED) printf("    ✗ %s\n", vm_message(vm));
        else if (vm_status(vm) == VM_NEEDS_INPUT) printf("    ✗ %s\n", says(it, "un test non può leggere dati", "a test cannot read input"));
        else if (vm_status(vm) == VM_READY) printf("    ✗ %s\n", says(it, "troppi passi, fermato", "too many steps, stopped"));
    }
    printf(says(it, "%d test, %d falliti\n", "%d tests, %d failed\n"), total, failed);
    vm_destroy(vm);
    return failed ? EXIT_PROBLEMS : EXIT_OK;
}

static void step(const Options *options, const char *text) {
    if (options->verbose) fprintf(stderr, "pseudoscope: %s\n", text);
}

static bool italian(const Options *options, bool otherwise) {
    const char *lang = options->lang ? options->lang : getenv("PSEUDOSCOPE_LANG");
    return lang && *lang ? strcmp(lang, "it") == 0 : otherwise;
}

static int translate(const Options *options, const Source *source, Arena *arena, StrBuf *out) {
    bool tty = isatty(STDERR_FILENO) && !getenv("NO_COLOR");
    Diag diag = {.source = source, .out = stderr, .silent_warnings = options->quiet,
                 .italian = italian(options, false),
                 .color = options->color == COLOR_ALWAYS ||
                          (options->color == COLOR_AUTO && tty)};

    if (is_uml_path(source->name)) { /* a diagram: it can be checked and listed */
        Uml *uml = uml_parse(source, arena, &diag);
        if (!uml || diag.errors) return EXIT_PROBLEMS;
        if (options->command == CMD_CHECK) return options->werror && diag.warnings ? EXIT_PROBLEMS : EXIT_OK;
        if (options->command != CMD_AST) {
            fputs("pseudoscope: a diagram can be opened with ui, check and ast\n", stderr);
            return EXIT_PROBLEMS;
        }
        uml_dump(uml, out);
        return write_file(options->output, out) ? EXIT_OK : EXIT_IO;
    }
    step(options, "reading tokens");
    TokenList tokens = lex(source, &options->config, arena, &diag);
    if (options->command == CMD_TOKENS) {
        dump_tokens(tokens, out);
        if (!write_file(options->output, out)) return EXIT_IO;
        return diag.errors ? EXIT_PROBLEMS : EXIT_OK;
    }

    step(options, "parsing");
    Program *program = parse(tokens, &options->config, arena, &diag);
    if (diag.errors) return EXIT_PROBLEMS;
    if (options->command == CMD_AST) {
        if (options->dot) dump_tree_dot(program, out);
        else dump_tree(program, out);
        return write_file(options->output, out) ? EXIT_OK : EXIT_IO;
    }

    step(options, "checking names and types");
    if (!analyze(program, &options->config, arena, &diag)) return EXIT_PROBLEMS;
    if (options->werror && diag.warnings) {
        fputs("pseudoscope: warnings are treated as errors\n", stderr);
        return EXIT_PROBLEMS;
    }
    if (options->command == CMD_CHECK) return EXIT_OK;

    Lang lang = options->target, found;
    if (!options->target_given && options->command == CMD_TRANSLATE && options->output && lang_from_path(options->output, &found))
        lang = found;
    if (options->command == CMD_TEST && !options->target_given) return run_tests(program, options, diag.italian);
    if (options->command == CMD_EXEC) {
        step(options, "executing");
        return has_main(program) || has_components(program) ? interpret(program, options) : no_main();
    }
    if (!translatable(program, &options->config, lang, &diag)) return EXIT_PROBLEMS;
    if (options->command == CMD_BUILD && !is_compiled(lang)) {
        fprintf(stderr, "pseudoscope: build makes an executable, and %s programs are run as they are: use translate, or run\n",
                lang_name(lang));
        return EXIT_USAGE;
    }

    step(options, "translating");
    const char *class_name = class_name_for(options, arena);
    translate_program(program, &options->config, arena, lang, class_name, out);
    if (options->command == CMD_TRANSLATE) {
        if (options->output && same_file(options->input, options->output)) {
            fputs("pseudoscope: the output would overwrite the input\n", stderr);
            return EXIT_USAGE;
        }
        return write_file(options->output, out) ? EXIT_OK : EXIT_IO;
    }
    if (options->command == CMD_TEST) {
        if (!has_tests(program)) {
            fputs("pseudoscope: there are no tests in this program\n", stderr);
            return EXIT_PROBLEMS;
        }
        return build_and_run(options, lang, out, arena, class_name, "test");
    }
    if (!has_main(program) && !has_components(program)) return no_main();
    return build_and_run(options, lang, out, arena, class_name, NULL);
}

static int start_ui(const Options *options) {
    UiOptions ui = {.config = options->config, .path = options->input,
                    .italian = italian(options, true), .keys = options->keys,
                    .width = options->width, .height = options->height,
                    .colors = options->color == COLOR_ALWAYS};
    return run_ui(&ui);
}

int run_command(const Options *given) {
    if (given->command == CMD_UI) return start_ui(given);
    Options adjusted = *given;
    const Options *options = &adjusted;
    adjusted.config = config_for(given->config, given->input);
    StrBuf text = {0};
    if (!read_file(options->input, &text)) {
        strbuf_free(&text);
        return EXIT_IO;
    }
    const char *name = strcmp(options->input, "-") == 0 ? "<stdin>" : options->input;
    Source source = {name, text.data, text.length};
    Arena arena = {0};
    StrBuf out = {0};
    strbuf_puts(&out, "");
    int status = translate(options, &source, &arena, &out);
    strbuf_free(&out);
    arena_free(&arena);
    strbuf_free(&text);
    return status;
}
