#include "pseudoscope/diag.h"

#include <stdarg.h>
#include <string.h>

static void print_source_line(Diag *diag, Loc loc) {
    const char *line = diag->source->shown ? diag->source->shown : diag->source->text;
    for (int i = 1; i < loc.line; i++) {
        line = strchr(line, '\n');
        if (!line) return;
        line++;
    }
    size_t length = strcspn(line, "\r\n");
    if (diag->source->shown) { /* the columns are those of our rewriting: the line is enough */
        fprintf(diag->out, "%5d | %.*s\n", loc.line, (int)length, line);
        return;
    }
    fprintf(diag->out, "%5d | %.*s\n      | ", loc.line, (int)length, line);
    for (int i = 0; i < loc.column - 1 && (size_t)i < length; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t') fputc('\t', diag->out);
        else if ((c & 0xC0) != 0x80) fputc(' ', diag->out);
    }
    fputs(diag->color ? "\033[1;32m^\033[0m\n" : "^\n", diag->out);
}

static void report(Diag *diag, Loc loc, const char *label, const char *color,
                   const char *format, va_list args) {
    format = tr(format);
    if (diag->sink) {
        char message[512];
        vsnprintf(message, sizeof message, format, args);
        diag->sink(diag->sink_context, loc, color[1] == '5', message);
        return;
    }
    fprintf(diag->out, "%s:", diag->source->name);
    if (loc.line > 0) fprintf(diag->out, "%d:%d:", loc.line, loc.column);
    if (diag->color) fprintf(diag->out, " \033[1;%sm%s:\033[0m ", color, label);
    else fprintf(diag->out, " %s: ", label);
    vfprintf(diag->out, format, args);
    fputc('\n', diag->out);
    if (loc.line > 0) print_source_line(diag, loc);
}

void diag_error(Diag *diag, Loc loc, const char *format, ...) {
    /* A second error at the same spot is almost always a consequence. */
    bool repeated = diag->errors > 0 && loc.line > 0 &&
                    diag->last_error.line == loc.line &&
                    diag->last_error.column == loc.column;
    if (repeated || diag->too_many) return;

    diag->errors++;
    diag->last_error = loc;
    va_list args;
    va_start(args, format);
    report(diag, loc, T("error"), "31", format, args);
    va_end(args);
    if (diag->errors >= DIAG_MAX_ERRORS) {
        diag->too_many = true;
        if (!diag->sink) fputs(T("too many errors, stopping here\n"), diag->out);
    }
}

void diag_warning(Diag *diag, Loc loc, const char *format, ...) {
    if (diag->silent_warnings || diag->too_many) return;
    diag->warnings++;
    va_list args;
    va_start(args, format);
    report(diag, loc, T("warning"), "35", format, args);
    va_end(args);
}
