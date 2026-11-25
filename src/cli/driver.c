#include "plike/blocks.h"
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
        fprintf(stderr, T("pseudoscope: cannot open '%s': %s\n"), path, strerror(errno));
        return false;
    }
    char chunk[8192];
    size_t n;
    strbuf_puts(text, "");
    while ((n = fread(chunk, 1, sizeof chunk, file)) > 0) strbuf_append(text, chunk, n);
    bool ok = !ferror(file);
    if (!ok) fprintf(stderr, T("pseudoscope: cannot read '%s': %s\n"), path, strerror(errno));
    if (!is_stdin) fclose(file);
    return ok;
}

static bool write_file(const char *path, const StrBuf *text) {
    bool is_stdout = !path || strcmp(path, "-") == 0;
    FILE *file = is_stdout ? stdout : fopen(path, "wb");
    if (!file) {
        fprintf(stderr, T("pseudoscope: cannot write '%s': %s\n"), path, strerror(errno));
        return false;
    }
    bool ok = fwrite(text->data, 1, text->length, file) == text->length;
    ok &= (is_stdout ? fflush(file) : fclose(file)) == 0;
    if (!ok) fprintf(stderr, T("pseudoscope: cannot write '%s'\n"), is_stdout ? T("standard output") : path);
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
        fprintf(stderr, T("pseudoscope: cannot run '%s': %s\n"), args[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    if (WIFSIGNALED(status)) {
        fprintf(stderr, T("pseudoscope: '%s' was killed by signal %d\n"), args[0], WTERMSIG(status));
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
    if (options->verbose) fprintf(stderr, T("pseudoscope: compiling with %s\n"), tool);
    if (execute(lang == LANG_C ? c : lang == LANG_CPP ? cpp : rust) == 0) return true;
    fprintf(stderr, T("pseudoscope: the %s compiler failed\n"), lang_name(lang));
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
    fputs(T("pseudoscope: there is nothing to run: declare a procedure 'main' without parameters\n"), stderr);
    return EXIT_PROBLEMS;
}

static bool is_compiled(Lang lang) { return lang == LANG_C || lang == LANG_CPP || lang == LANG_RUST; }

/* `argument`: given to the program, or NULL. */
static int build_and_run(const Options *options, Lang lang, const StrBuf *code, Arena *arena, const char *class_name,
                         const char *argument) {
    const char *base = getenv("TMPDIR");
    char *dir = arena_printf(arena, "%s/pseudoscope-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(dir)) {
        fprintf(stderr, T("pseudoscope: cannot create a temporary directory: %s\n"), strerror(errno));
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
        if (options->verbose) fputs(T("pseudoscope: compiling with tsc\n"), stderr);
        if (execute(tsc) != 0) {
            fputs(T("pseudoscope: the TypeScript compiler failed\n"), stderr);
        } else {
            status = execute(node);
            if (status < 0) status = EXIT_IO;
        }
        unlink(script);
    } else if (!is_compiled(lang)) {
        const char *tool = tool_for(options, lang, arena);
        const char *const plain[] = {tool, source, argument, NULL};
        const char *const stripped[] = {tool, "--no-warnings", "--experimental-strip-types", source, argument, NULL};
        if (options->verbose) fprintf(stderr, T("pseudoscope: running with %s\n"), tool);
        status = execute(lang == LANG_TS && !options->cc ? stripped : plain);
        if (status < 0) status = EXIT_IO;
    } else if (compile_source(options, lang, source, executable, arena)) {
        if (options->command != CMD_BUILD) {
            const char *const args[] = {executable, argument, NULL};
            if (options->verbose) fputs(T("pseudoscope: running\n"), stderr);
            status = execute(args);
            if (status < 0) status = EXIT_IO;
        } else {
            const char *target = options->output ? options->output
                                                 : default_executable(arena, options->input);
            const char *const args[] = {"cp", "--", executable, target, NULL};
            if (same_file(options->input, target))
                fputs(T("pseudoscope: the executable would overwrite the input\n"), stderr);
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
/* VisuAlg: arquivo "dados.txt" reads from a file what would be typed. */
static char *input_file_of(const Source *source, Arena *arena) {
    for (const char *s = source->text; *s;) {
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "arquivo", 7) == 0 || strncmp(s, "Arquivo", 7) == 0 || strncmp(s, "ARQUIVO", 7) == 0) {
            const char *open = strchr(s, '"'), *line = s + strcspn(s, "\n");
            const char *close = open && open < line ? strchr(open + 1, '"') : NULL;
            if (close && close < line) return arena_strndup(arena, open + 1, (size_t)(close - open - 1));
        }
        s += strcspn(s, "\n");
        if (*s) s++;
    }
    return NULL;
}

/* The drawing, as a picture that everything opens (PPM). */
static bool write_canvas(const Vm *vm, const char *path) {
    int width, height;
    vm_canvas_size(vm, &width, &height);
    int *pixels = malloc((size_t)width * (size_t)height * sizeof(int));
    FILE *file = fopen(path, "w");
    if (!pixels || !file) {
        fprintf(stderr, T("pseudoscope: cannot write '%s'\n"), path);
        free(pixels);
        if (file) fclose(file);
        return false;
    }
    vm_render(vm, width, height, pixels);
    fprintf(file, "P3\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        int c = pixels[i] < 0 ? 0xFFFFFF : pixels[i];
        fprintf(file, "%d %d %d%s", c >> 16 & 255, c >> 8 & 255, c & 255, (i + 1) % width == 0 ? "\n" : " ");
    }
    free(pixels);
    return fclose(file) == 0;
}

static int interpret(const Program *program, const Options *options, const Source *source, Arena *arena) {
    Vm *vm = vm_create(program, &options->config);
    size_t shown = 0, length;
    vm_start(vm);
    const char *input_file = options->config.dialect == DIALECT_VISUALG ? input_file_of(source, arena) : NULL;
    if (input_file) {
        StrBuf typed = {0};
        if (read_file(input_file, &typed)) vm_give_input(vm, typed.data);
        vm_end_input(vm);
        strbuf_free(&typed);
    }
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
        fputs(T("pseudoscope: too many steps, stopped\n"), stderr);
        vm_destroy(vm);
        return EXIT_PROBLEMS;
    }
    if (vm_status(vm) == VM_FAILED) {
        const Frame *frame = vm_frame(vm, vm_frame_count(vm) - 1);
        if (frame->stmt) fprintf(stderr, T("runtime error at line %d: %s\n"), frame->stmt->loc.line, tr(vm_message(vm)));
        else fprintf(stderr, T("runtime error: %s\n"), tr(vm_message(vm)));
    }
    int status = vm_exit_status(vm);
    /* what the program wrote in its files reaches the disk now */
    for (int i = 0; i < vm_file_count(vm); i++) {
        const char *data;
        int size;
        bool written;
        const char *path = vm_file(vm, i, &data, &size, &written);
        if (!written) continue;
        FILE *file = fopen(path, "wb");
        if (!file || fwrite(data, 1, (size_t)size, file) != (size_t)size) {
            fprintf(stderr, T("pseudoscope: cannot write '%s'\n"), path);
            if (status == 0) status = EXIT_IO;
        }
        if (file) fclose(file);
    }
    if (options->canvas && !write_canvas(vm, options->canvas) && status == 0) status = EXIT_IO;
    vm_destroy(vm);
    return status;
}

/* Runs every "test ... end test", and reports what they checked. */
static int run_tests(const Program *program, const Options *options) {
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
                printf("    %s %s %d: %s\n", c->ok ? "✓" : "✗", T("line"), c->stmt->loc.line, c->text);
        }
        if (vm_status(vm) == VM_FAILED) printf("    ✗ %s\n", tr(vm_message(vm)));
        else if (vm_status(vm) == VM_NEEDS_INPUT) printf("    ✗ %s\n", T("a test cannot read input"));
        else if (vm_status(vm) == VM_READY) printf("    ✗ %s\n", T("too many steps, stopped"));
    }
    printf(T("%d tests, %d failed\n"), total, failed);
    vm_destroy(vm);
    return failed ? EXIT_PROBLEMS : EXIT_OK;
}

static void step(const Options *options, const char *text) {
    if (options->verbose) fprintf(stderr, "pseudoscope: %s\n", tr(text));
}

static int translate(const Options *options, const Source *source, Arena *arena, StrBuf *out, bool diagram) {
    bool tty = isatty(STDERR_FILENO) && !getenv("NO_COLOR");
    Diag diag = {.source = source, .out = stderr, .silent_warnings = options->quiet,
                 .color = options->color == COLOR_ALWAYS ||
                          (options->color == COLOR_AUTO && tty)};

    if (diagram) { /* it can be checked and listed */
        Uml *uml = uml_parse(source, arena, &diag);
        if (!uml || diag.errors) return EXIT_PROBLEMS;
        if (options->command == CMD_CHECK) return options->werror && diag.warnings ? EXIT_PROBLEMS : EXIT_OK;
        if (options->command != CMD_AST) {
            fputs(T("pseudoscope: a diagram can be opened with ui, check and ast\n"), stderr);
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
        fputs(T("pseudoscope: warnings are treated as errors\n"), stderr);
        return EXIT_PROBLEMS;
    }
    if (options->command == CMD_CHECK) return EXIT_OK;

    Lang lang = options->target, found;
    if (!options->target_given && options->command == CMD_TRANSLATE && options->output && lang_from_path(options->output, &found))
        lang = found;
    if (options->command == CMD_TEST && !options->target_given) return run_tests(program, options);
    if (options->command == CMD_EXEC) {
        step(options, "executing");
        return has_main(program) || has_components(program) ? interpret(program, options, source, arena) : no_main();
    }
    if (!translatable(program, &options->config, lang, &diag)) return EXIT_PROBLEMS;
    if (options->command == CMD_BUILD && !is_compiled(lang)) {
        fprintf(stderr, T("pseudoscope: build makes an executable, and %s programs are run as they are: use translate, or run\n"),
                lang_name(lang));
        return EXIT_USAGE;
    }

    step(options, "translating");
    const char *class_name = class_name_for(options, arena);
    translate_program(program, &options->config, arena, lang, class_name, out);
    if (options->command == CMD_TRANSLATE) {
        if (options->output && same_file(options->input, options->output)) {
            fputs(T("pseudoscope: the output would overwrite the input\n"), stderr);
            return EXIT_USAGE;
        }
        return write_file(options->output, out) ? EXIT_OK : EXIT_IO;
    }
    if (options->command == CMD_TEST) {
        if (!has_tests(program)) {
            fputs(T("pseudoscope: there are no tests in this program\n"), stderr);
            return EXIT_PROBLEMS;
        }
        return build_and_run(options, lang, out, arena, class_name, "test");
    }
    if (!has_main(program) && !has_components(program)) return no_main();
    return build_and_run(options, lang, out, arena, class_name, NULL);
}

static int start_ui(const Options *options) {
    UiOptions ui = {.config = options->config, .path = options->input,
                    .keys = options->keys,
                    .width = options->width, .height = options->height,
                    .colors = options->color == COLOR_ALWAYS, .target = (int)options->target};
    return run_ui(&ui);
}

/* Markdown and LaTeX: the programs and the diagrams they hold. */
static int run_document(const Options *given, const char *file, int number, const StrBuf *text) {
    Arena arena = {0};
    Source whole = {file, text->data, text->length};
    Blocks *blocks = blocks_read(&whole, &arena);
    int status = EXIT_OK;
    if (number > blocks->count) {
        fprintf(stderr, T("pseudoscope: '%s' holds %d programs and diagrams, not %d\n"), file, blocks->count, number);
        arena_free(&arena);
        return EXIT_USAGE;
    }
    if (number == 0 && given->command == CMD_AST) { /* what there is */
        StrBuf out = {0};
        strbuf_puts(&out, "");
        if (blocks->outline) uml_dump(blocks->outline, &out);
        for (int i = 0; i < blocks->count; i++) {
            const DocBlock *b = &blocks->items[i];
            strbuf_printf(&out, "#%d  line %d  %s  %s%s%s\n", i + 1, b->line, b->diagram ? "diagram" : "program", b->language,
                          b->title ? "  " : "", b->title ? b->title : "");
        }
        status = write_file(given->output, &out) ? EXIT_OK : EXIT_IO;
        strbuf_free(&out);
        arena_free(&arena);
        return status;
    }
    int first = number ? number - 1 : 0, last = number ? number - 1 : blocks->count - 1;
    if (number == 0 && given->command != CMD_CHECK) { /* the only program, if there is only one */
        int programs = 0;
        for (int i = 0; i < blocks->count; i++) {
            if (blocks->items[i].diagram) continue;
            programs++;
            first = last = i;
        }
        if (programs != 1) {
            fprintf(stderr, T("pseudoscope: '%s' holds %d programs: say which with %s#N (ast lists them)\n"), file, programs, file);
            arena_free(&arena);
            return EXIT_USAGE;
        }
    }
    for (int i = first; i <= last; i++) {
        const DocBlock *b = &blocks->items[i];
        Options adjusted = *given;
        adjusted.config.dialect = b->dialect;
        adjusted.config.dialect_given = true;
        adjusted.config = config_for(adjusted.config, file);
        Source source = {arena_printf(&arena, "%s#%d", file, i + 1), b->text, strlen(b->text)};
        StrBuf out = {0};
        strbuf_puts(&out, "");
        int result = translate(&adjusted, &source, &arena, &out, b->diagram);
        if (result > status) status = result;
        strbuf_free(&out);
    }
    if (number == 0 && given->command == CMD_CHECK && !given->quiet)
        fprintf(stderr, T("pseudoscope: %s: %d programs and diagrams checked\n"), file, blocks->count);
    arena_free(&arena);
    return status;
}

int run_command(const Options *given) {
    if (given->command == CMD_UI) return start_ui(given);
    Options adjusted = *given;
    const Options *options = &adjusted;
    char file[1024];
    int number = block_number(given->input, file, sizeof file);
    StrBuf text = {0};
    if (!read_file(file, &text)) {
        strbuf_free(&text);
        return EXIT_IO;
    }
    /* what it is: by its name, or by how it begins */
    bool standard = strcmp(file, "-") == 0;
    FileKind kind = standard ? (looks_like_uml(text.data) ? FILE_DIAGRAM : FILE_PROGRAM) : file_kind(file, text.data);
    if (kind == FILE_DOCUMENT) {
        int result = run_document(given, file, number, &text);
        strbuf_free(&text);
        return result;
    }
    adjusted.config = config_for_text(given->config, standard ? "" : file, standard ? NULL : text.data);
    const char *name = standard ? "<stdin>" : options->input;
    Source source = {name, text.data, text.length};
    Arena arena = {0};
    StrBuf out = {0};
    strbuf_puts(&out, "");
    int status = translate(options, &source, &arena, &out, kind == FILE_DIAGRAM);
    strbuf_free(&out);
    arena_free(&arena);
    strbuf_free(&text);
    return status;
}
