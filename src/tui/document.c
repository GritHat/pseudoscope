#include "ui.h"

#include "plike/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void add_message(void *context, Loc loc, bool warning, const char *text) {
    Document *doc = context;
    if (doc->message_count == (int)(sizeof doc->messages / sizeof doc->messages[0])) return;
    Message *m = &doc->messages[doc->message_count++];
    m->loc = loc;
    m->warning = warning;
    snprintf(m->text, sizeof m->text, "%s", text);
}

static void split_lines(Document *doc) {
    int count = 1;
    for (size_t i = 0; i < doc->text.length; i++) count += doc->text.data[i] == '\n';
    doc->lines = malloc((size_t)(count + 2) * sizeof(int));
    if (!doc->lines) abort();
    int n = 0;
    doc->lines[n++] = 0;
    for (size_t i = 0; i < doc->text.length; i++)
        if (doc->text.data[i] == '\n') doc->lines[n++] = (int)i + 1;
    doc->lines[n] = (int)doc->text.length + 1;
    if (n > 1 && doc->lines[n - 1] == (int)doc->text.length) n--; /* final newline */
    doc->line_count = n;
}

static int token_kind_style(const Token *t) {
    switch (t->kind) {
        case TOK_INTEGER: case TOK_REAL: case TOK_LOGICAL: case TOK_CHARACTER: case TOK_ARRAY:
        case TOK_OF: case TOK_DIM: case TOK_RECORD:
            return H_TYPE;
        case TOK_INT: case TOK_FLOAT: case TOK_TRUE: case TOK_FALSE: return H_NUMBER;
        case TOK_STRING: case TOK_CHAR: return H_STRING;
        case TOK_AND: case TOK_OR: case TOK_NOT: case TOK_MOD: case TOK_EQ: case TOK_NE: {
            char c = t->text[0];
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ? H_KEYWORD : H_PLAIN;
        }
        default:
            return t->kind >= TOK_FUNCTION && t->kind <= TOK_READ ? H_KEYWORD : H_PLAIN;
    }
}

static void color_tokens(Document *doc) {
    size_t length = doc->text.length;
    doc->kinds = calloc(length + 1, 1);
    if (!doc->kinds) abort();
    memset(doc->kinds, H_COMMENT, length); /* what is not a token */
    for (int i = 0; i < doc->tokens.count; i++) {
        const Token *t = &doc->tokens.items[i];
        if (t->kind == TOK_EOF) continue;
        size_t at = (size_t)(doc->lines[t->loc.line - 1] + t->loc.column - 1);
        size_t n = strlen(t->text) + (t->kind == TOK_STRING || t->kind == TOK_CHAR ? 2 : 0);
        int kind = token_kind_style(t);
        if (t->kind == TOK_IDENT && doc->program) {
            for (const Function *f = doc->program->functions; f; f = f->next)
                if (strcmp(f->name, t->text) == 0) kind = H_FUNCTION;
            for (const TypeDecl *d = doc->program->types; d; d = d->next)
                if (strcmp(d->name, t->text) == 0) kind = H_RECORD;
        }
        for (size_t k = at; k < at + n && k < length; k++) doc->kinds[k] = (unsigned char)kind;
    }
}

/* ---- structure ---- */

void visit_expr(const Expr *e, ExprVisitor visit, void *context) {
    if (!e) return;
    visit(context, e);
    visit_expr(e->base, visit, context);
    visit_expr(e->right, visit, context);
    for (int i = 0; i < e->args.count; i++) visit_expr(e->args.items[i], visit, context);
}

void visit_statements(const Stmt *s, ExprVisitor visit, void *context) {
    for (; s; s = s->next) {
        for (const Stmt *b = s; b; b = b->otherwise) {
            visit_expr(b->target, visit, context);
            visit_expr(b->value, visit, context);
            visit_expr(b->limit, visit, context);
            visit_expr(b->step, visit, context);
            for (int i = 0; i < b->args.count; i++) visit_expr(b->args.items[i], visit, context);
            visit_statements(b->body, visit, context);
        }
    }
}

static void note_call(void *context, const Expr *e) {
    Item *item = context;
    if (!e->is_call || !e->symbol || !e->symbol->function) return;
    for (int i = 0; i < item->call_count; i++)
        if (item->calls[i] == e->symbol->function) return;
    if (item->call_count < 64) item->calls[item->call_count++] = e->symbol->function;
}

static bool line_has_token(const Document *doc, int line) {
    for (int i = doc->lines[line - 1]; i < doc->lines[line] - 1 && i < (int)doc->text.length; i++) {
        char c = doc->text.data[i];
        if (c != ' ' && c != '\t' && c != '\r' && doc->kinds[i] != H_COMMENT) return true;
    }
    return false;
}

/* From its first line up to the next declaration, without the blank lines and
 * comments in between. */
static void find_ranges(Document *doc) {
    for (int i = 0; i < doc->item_count; i++) {
        Item *item = &doc->items[i];
        int next = doc->line_count + 1;
        for (int k = 0; k < doc->item_count; k++)
            if (doc->items[k].first > item->first && doc->items[k].first < next) next = doc->items[k].first;
        if (item->kind != ITEM_FUNCTION) /* later parts of other kinds also end it */
            for (const VarDecl *g = doc->program->globals; g; g = g->next)
                if (g->loc.line > item->first && g->loc.line < next && item->kind == ITEM_RECORD) next = g->loc.line;
        item->last = next - 1;
        while (item->last > item->first && !line_has_token(doc, item->last)) item->last--;
    }
}

static void list_items(Document *doc) {
    int count = 1;
    for (const Function *f = doc->program->functions; f; f = f->next) count++;
    for (const TypeDecl *t = doc->program->types; t; t = t->next) count++;
    for (const VarDecl *g = doc->program->globals; g; g = g->next) count++;
    doc->items = calloc((size_t)count, sizeof(Item));
    if (!doc->items) abort();
    for (const Function *f = doc->program->functions; f; f = f->next) {
        Item *item = &doc->items[doc->item_count];
        item->kind = ITEM_FUNCTION;
        item->name = f->name;
        item->function = f;
        item->first = f->loc.line;
        item->calls = arena_alloc(&doc->arena, 64 * sizeof(Function *));
        if (doc->ok) visit_statements(f->body, note_call, item);
        if (strcmp(f->name, "main") == 0 && !f->params) doc->main_item = doc->item_count;
        doc->item_count++;
    }
    for (const TypeDecl *t = doc->program->types; t; t = t->next) {
        Item *item = &doc->items[doc->item_count++];
        item->kind = ITEM_RECORD;
        item->name = t->name;
        item->record = t->record;
        item->first = t->loc.line;
    }
    for (const VarDecl *g = doc->program->globals; g; g = g->next) {
        if (!g->spec || !g->spec->record || g->spec->record->name != g->name) continue;
        Item *item = &doc->items[doc->item_count++]; /* "var p: record ... end" */
        item->kind = ITEM_RECORD;
        item->name = g->name;
        item->record = g->spec->record;
        item->first = g->loc.line;
    }
    if (doc->program->globals) {
        Item *item = &doc->items[doc->item_count++];
        item->kind = ITEM_GLOBALS;
        item->name = "";
        item->first = doc->program->globals->loc.line;
    }
    find_ranges(doc);
}

Document *document_load(const char *path, const Config *config) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    Document *doc = calloc(1, sizeof *doc);
    if (!doc) abort();
    char chunk[8192];
    size_t n;
    strbuf_puts(&doc->text, "");
    while ((n = fread(chunk, 1, sizeof chunk, file)) > 0 && doc->text.length < (8u << 20))
        strbuf_append(&doc->text, chunk, n);
    bool failed = ferror(file) != 0;
    fclose(file);
    if (failed) { /* a directory, for instance */
        strbuf_free(&doc->text);
        free(doc);
        return NULL;
    }
    struct stat st;
    if (stat(path, &st) == 0) doc->modified = st.st_mtime;
    doc->path = strdup(path);
    doc->config = *config;
    doc->main_item = -1;
    doc->source = (Source){doc->path, doc->text.data, doc->text.length};
    split_lines(doc);

    Diag diag = {.source = &doc->source, .out = stderr, .sink = add_message, .italian = ui_italian, .sink_context = doc};
    doc->tokens = lex(&doc->source, &doc->config, &doc->arena, &diag);
    doc->program = parse(doc->tokens, &doc->config, &doc->arena, &diag);
    doc->ok = diag.errors == 0 && analyze(doc->program, &doc->config, &doc->arena, &diag);
    color_tokens(doc);
    list_items(doc);
    return doc;
}

void document_free(Document *doc) {
    if (!doc) return;
    free(doc->path);
    free(doc->lines);
    free(doc->kinds);
    free(doc->items);
    strbuf_free(&doc->text);
    arena_free(&doc->arena);
    free(doc);
}

bool document_changed(const Document *doc) {
    struct stat st;
    return stat(doc->path, &st) == 0 && st.st_mtime != doc->modified;
}

const Item *document_function(const Document *doc, const Function *function) {
    for (int i = 0; i < doc->item_count; i++)
        if (doc->items[i].function && doc->items[i].function == function) return &doc->items[i];
    return NULL;
}

Style highlight_style(int kind) {
    switch (kind) {
        case H_KEYWORD: return S(111, C_DEFAULT, A_BOLD);
        case H_TYPE: return S(C_TYPE, C_DEFAULT, 0);
        case H_NUMBER: return S(C_NUMBER, C_DEFAULT, 0);
        case H_STRING: return S(C_STRING, C_DEFAULT, 0);
        case H_COMMENT: return S(C_FAINT, C_DEFAULT, A_ITALIC);
        case H_FUNCTION: return S(C_CALL, C_DEFAULT, A_BOLD);
        case H_RECORD: return S(C_TYPE, C_DEFAULT, A_BOLD);
        default: return S_TEXT;
    }
}

static void add(char *out, size_t size, const char *text) {
    size_t used = strlen(out);
    if (used + 1 < size) snprintf(out + used, size - used, "%s", text);
}

void describe_type(const Document *doc, const Type *type, char *out, size_t size) {
    switch (type->kind) {
        case TY_ARRAY: {
            StrBuf text = {0};
            strbuf_puts(&text, "");
            add(out, size, "array [");
            for (int i = 0; i < type->dims.count; i++) {
                const Dim *dim = &type->dims.items[i];
                text.length = 0;
                if (i) strbuf_puts(&text, ", ");
                if (dim->lo) {
                    print_expr(NULL, dim->lo, NULL, 0, "", "", "", "", &text);
                    strbuf_puts(&text, "..");
                }
                if (dim->hi) print_expr(NULL, dim->hi, NULL, 0, "", "", "", "", &text);
                add(out, size, text.length ? text.data : "");
                strbuf_puts(&text, "");
            }
            strbuf_free(&text);
            add(out, size, "] of ");
            describe_type(doc, type->base, out, size);
            break;
        }
        case TY_POINTER:
            describe_type(doc, type->base, out, size);
            add(out, size, "*");
            break;
        case TY_RECORD: add(out, size, type->record->name ? type->record->name : "record"); break;
        case TY_INT: add(out, size, "integer"); break;
        case TY_REAL: add(out, size, "real"); break;
        case TY_BOOL: add(out, size, "logical"); break;
        case TY_CHAR: add(out, size, "character"); break;
        default: add(out, size, "?");
    }
}
