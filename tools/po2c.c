/* po2c: turns the catalogs in lang/ (gettext .po files) into the C table that
 * is compiled into the program.
 *
 *     po2c lang/it.po lang/es.po > catalogs.c
 *
 * The code of a language is the name of its file; its own name is the
 * "X-Language-Name" line of the header. A translation must take the same
 * arguments as the text it translates, in the same order: anything else stops
 * the build here, where it can be read, instead of crashing a program later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t length, room;
} Text;

typedef struct {
    Text context, id, plural, one, many;
    int line;
} Entry;

static void add(Text *t, const char *s, size_t n) {
    if (t->length + n + 1 > t->room) {
        t->room = (t->length + n + 1) * 2;
        t->text = realloc(t->text, t->room);
        if (!t->text) abort();
    }
    memcpy(t->text + t->length, s, n);
    t->length += n;
    t->text[t->length] = '\0';
}

static const char *text_of(const Text *t) { return t->text ? t->text : ""; }

/* The conversions of a format, one letter each: "%d of %.0s" gives "ds". */
static void conversions(const char *s, char *out, size_t size) {
    size_t n = 0;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            continue;
        }
        if (*s != '%') continue;
        s++;
        if (*s == '%') continue;
        while (*s && strchr("-+ #0123456789.*", *s)) s++;
        while (*s && strchr("hlLzjt", *s)) {
            if (n + 1 < size) out[n++] = *s;
            s++;
        }
        if (!*s) break;
        if (n + 1 < size) out[n++] = *s;
    }
    out[n] = '\0';
}

static bool same_arguments(const char *file, const Entry *e, const char *from, const char *to) {
    char a[64], b[64];
    conversions(from, a, sizeof a);
    conversions(to, b, sizeof b);
    if (!*to || strcmp(a, b) == 0) return true;
    fprintf(stderr, "%s:%d: the translation takes other arguments (%%%s) than \"%s\" (%%%s)\n", file, e->line, b, from, a);
    return false;
}

static void literal(const char *s) {
    /* already written as C wants it; "??" would start a trigraph */
    putchar('"');
    for (; *s; s++) {
        if (s[0] == '?' && s[1] == '?') fputs("?\"\"", stdout);
        else putchar(*s);
    }
    putchar('"');
}

static bool emit(const char *file, const Entry *e, int *count) {
    if (!e->id.length || !e->one.length) return true; /* the header, or not translated yet */
    bool ok = same_arguments(file, e, text_of(&e->id), text_of(&e->one));
    if (e->plural.length) ok &= same_arguments(file, e, text_of(&e->plural), text_of(&e->many));
    fputs("    {", stdout);
    if (e->context.length) {
        literal(text_of(&e->context));
        fputs(" \"\\004\" ", stdout);
    }
    literal(text_of(&e->id));
    fputs(", ", stdout);
    if (e->plural.length) literal(text_of(&e->plural));
    else fputs("0", stdout);
    fputs(", {", stdout);
    literal(text_of(&e->one));
    fputs(", ", stdout);
    if (e->many.length) literal(text_of(&e->many));
    else fputs("0", stdout);
    fputs("}},\n", stdout);
    (*count)++;
    return ok;
}

static bool catalog(const char *path, char *code, size_t code_size, char *name, size_t name_size, int *count) {
    FILE *in = fopen(path, "r");
    if (!in) {
        fprintf(stderr, "po2c: cannot open '%s'\n", path);
        return false;
    }
    const char *base = strrchr(path, '/');
    snprintf(code, code_size, "%s", base ? base + 1 : path);
    if (strrchr(code, '.')) *strrchr(code, '.') = '\0';
    snprintf(name, name_size, "%s", code);
    printf("static const CatalogEntry ENTRIES_%s[] = {\n", code);

    char line[4096];
    Entry e = {0};
    Text *into = NULL;
    bool ok = true;
    int number = 0;
    for (;;) {
        char *s = fgets(line, sizeof line, in);
        number++;
        if (s) s += strspn(s, " \t");
        if (!s || *s == '\n' || *s == '\0' || *s == '\r') { /* the end of an entry */
            if (!e.id.length && e.one.length) { /* the header: who this language is */
                const char *found = strstr(e.one.text, "X-Language-Name: ");
                if (found) {
                    found += strlen("X-Language-Name: ");
                    snprintf(name, name_size, "%.*s", (int)strcspn(found, "\\"), found);
                }
            }
            if (e.id.length || e.one.length) ok &= emit(path, &e, count);
            e.context.length = e.id.length = e.plural.length = e.one.length = e.many.length = 0;
            into = NULL;
            if (!s) break;
            continue;
        }
        if (*s == '#') continue;
        static const struct {
            const char *word;
            int field;
        } WORDS[] = {{"msgctxt", 0}, {"msgid_plural", 2}, {"msgid", 1}, {"msgstr[0]", 3}, {"msgstr[1]", 4}, {"msgstr", 3}};
        for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
            size_t n = strlen(WORDS[i].word);
            if (strncmp(s, WORDS[i].word, n) != 0 || (s[n] != ' ' && s[n] != '\t')) continue;
            Text *fields[] = {&e.context, &e.id, &e.plural, &e.one, &e.many};
            into = fields[WORDS[i].field];
            if (WORDS[i].field == 1) e.line = number;
            s += n;
            s += strspn(s, " \t");
            break;
        }
        char *end = strrchr(s, '"');
        if (*s != '"' || !into || end == s) {
            fprintf(stderr, "%s:%d: not understood: %s", path, number, line);
            ok = false;
            continue;
        }
        add(into, s + 1, (size_t)(end - s - 1));
    }
    fclose(in);
    puts("    {0, 0, {0, 0}},\n};\n");
    free(e.context.text), free(e.id.text), free(e.plural.text), free(e.one.text), free(e.many.text);
    return ok;
}

int main(int argc, char **argv) {
    char codes[32][16], names[32][64];
    int counts[32] = {0};
    bool ok = true;
    if (argc - 1 > 32) {
        fputs("po2c: too many languages\n", stderr);
        return 1;
    }
    puts("/* Made by tools/po2c from lang/: do not edit. */\n#include \"plike/i18n.h\"\n");
    for (int i = 1; i < argc; i++) ok &= catalog(argv[i], codes[i - 1], sizeof codes[0], names[i - 1], sizeof names[0], &counts[i - 1]);
    puts("const Catalog CATALOGS[] = {");
    for (int i = 0; i < argc - 1; i++)
        printf("    {\"%s\", \"%s\", ENTRIES_%s, %d},\n", codes[i], names[i], codes[i], counts[i]);
    printf("    {0, 0, 0, 0},\n};\nconst int CATALOG_COUNT = %d;\n", argc - 1);
    return ok ? 0 : 1;
}
