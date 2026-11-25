#include "plike/cli.h"

#include "plike/codegen.h"
#include "plike/dump.h"
#include "plike/parser.h"
#include "plike/ui.h"
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
        fprintf(stderr, "plike: cannot open '%s': %s\n", path, strerror(errno));
        return false;
    }
    char chunk[8192];
    size_t n;
    strbuf_puts(text, "");
    while ((n = fread(chunk, 1, sizeof chunk, file)) > 0) strbuf_append(text, chunk, n);
    bool ok = !ferror(file);
    if (!ok) fprintf(stderr, "plike: cannot read '%s': %s\n", path, strerror(errno));
    if (!is_stdin) fclose(file);
    return ok;
}

static bool write_file(const char *path, const StrBuf *text) {
    bool is_stdout = !path || strcmp(path, "-") == 0;
    FILE *file = is_stdout ? stdout : fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "plike: cannot write '%s': %s\n", path, strerror(errno));
        return false;
    }
    bool ok = fwrite(text->data, 1, text->length, file) == text->length;
    ok &= (is_stdout ? fflush(file) : fclose(file)) == 0;
    if (!ok) fprintf(stderr, "plike: cannot write '%s'\n", is_stdout ? "standard output" : path);
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
        fprintf(stderr, "plike: cannot run '%s': %s\n", args[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "plike: '%s' was killed by signal %d\n", args[0], WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}

static bool compile_c(const Options *options, const char *source, const char *executable) {
    const char *cc = options->cc ? options->cc : getenv("CC");
    if (!cc || !*cc) cc = "cc";
    const char *const args[] = {cc, "-std=c11", "-O1", "-o", executable, source, "-lm", NULL};
    if (options->verbose) fprintf(stderr, "plike: compiling with %s\n", cc);
    if (execute(args) == 0) return true;
    fputs("plike: the C compiler failed\n", stderr);
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
    fputs("plike: there is nothing to run: declare a procedure 'main' without parameters\n", stderr);
    return EXIT_PROBLEMS;
}

static int build_and_run(const Options *options, const StrBuf *code, Arena *arena) {
    const char *base = getenv("TMPDIR");
    char *dir = arena_printf(arena, "%s/plike-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir)) {
        fprintf(stderr, "plike: cannot create a temporary directory: %s\n", strerror(errno));
        return EXIT_IO;
    }
    char *source = arena_printf(arena, "%s/program.c", dir);
    char *executable = arena_printf(arena, "%s/program", dir);
    int status = EXIT_IO;

    if (write_file(source, code) && compile_c(options, source, executable)) {
        if (options->command == CMD_RUN) {
            const char *const args[] = {executable, NULL};
            if (options->verbose) fputs("plike: running\n", stderr);
            status = execute(args);
            if (status < 0) status = EXIT_IO;
        } else {
            const char *target = options->output ? options->output
                                                 : default_executable(arena, options->input);
            const char *const args[] = {"cp", "--", executable, target, NULL};
            if (same_file(options->input, target))
                fputs("plike: the executable would overwrite the input\n", stderr);
            else if (execute(args) == 0)
                status = EXIT_OK;
        }
    }
    unlink(source);
    unlink(executable);
    rmdir(dir);
    return status;
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
        fputs("plike: too many steps, stopped\n", stderr);
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

static void step(const Options *options, const char *text) {
    if (options->verbose) fprintf(stderr, "plike: %s\n", text);
}

static bool italian(const Options *options, bool otherwise) {
    const char *lang = options->lang ? options->lang : getenv("PLIKE_LANG");
    return lang && *lang ? strcmp(lang, "it") == 0 : otherwise;
}

static int translate(const Options *options, const Source *source, Arena *arena, StrBuf *out) {
    bool tty = isatty(STDERR_FILENO) && !getenv("NO_COLOR");
    Diag diag = {.source = source, .out = stderr, .silent_warnings = options->quiet,
                 .italian = italian(options, false),
                 .color = options->color == COLOR_ALWAYS ||
                          (options->color == COLOR_AUTO && tty)};

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
        fputs("plike: warnings are treated as errors\n", stderr);
        return EXIT_PROBLEMS;
    }
    if (options->command == CMD_CHECK) return EXIT_OK;

    if (options->command == CMD_EXEC) {
        step(options, "executing");
        return has_main(program) ? interpret(program, options) : no_main();
    }

    step(options, "generating C");
    generate(program, &options->config, arena, out);
    if (options->command == CMD_TRANSLATE) {
        if (options->output && same_file(options->input, options->output)) {
            fputs("plike: the output would overwrite the input\n", stderr);
            return EXIT_USAGE;
        }
        return write_file(options->output, out) ? EXIT_OK : EXIT_IO;
    }
    if (!has_main(program)) return no_main();
    return build_and_run(options, out, arena);
}

static int start_ui(const Options *options) {
    UiOptions ui = {.config = options->config, .path = options->input,
                    .italian = italian(options, true), .keys = options->keys,
                    .width = options->width, .height = options->height,
                    .colors = options->color == COLOR_ALWAYS};
    return run_ui(&ui);
}

int run_command(const Options *options) {
    if (options->command == CMD_UI) return start_ui(options);
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
