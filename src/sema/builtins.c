#include "pseudoscope/builtins.h"

#include <string.h>

static const Builtin BUILTINS[] = {
    /* numbers: those of the C library, by their names */
    {"sqrt", 1, 'r', "n", false}, {"sin", 1, 'r', "n", false}, {"cos", 1, 'r', "n", false}, {"tan", 1, 'r', "n", false},
    {"exp", 1, 'r', "n", false}, {"log", 1, 'r', "n", false}, {"floor", 1, 'r', "n", false}, {"ceil", 1, 'r', "n", false},
    {"fabs", 1, 'r', "n", false}, {"pow", 2, 'r', "nn", false}, {"abs", 1, 'n', "n", false},
    {"asin", 1, 'r', "n", false}, {"acos", 1, 'r', "n", false}, {"atan", 1, 'r', "n", false}, {"log10", 1, 'r', "n", false},
    {"min", 2, 'n', "nn", false}, {"max", 2, 'n', "nn", false},
    {"random", 2, 'i', "nn", false}, {"random_below", 1, 'i', "n", false}, {"trunc", 1, 'i', "n", false},
    {"round", 1, 'i', "n", false},
    /* texts */
    {"length", 1, 'i', "t", false}, {"upper", 1, 't', "t", false}, {"lower", 1, 't', "t", false},
    {"substring", 3, 't', "tnn", false}, {"concat", 2, 't', "tt", false},
    {"text_of", 1, 't', "a", true}, {"number_of", 1, 'r', "t", true}, {"whole_of", 1, 'i', "t", true},
    {"char_code", 1, 'i', "t", true}, {"char_of", 1, 't', "n", true}, {"position", 2, 'i', "tt", true},
    {"left", 2, 't', "tn", true}, {"right", 2, 't', "tn", true}, {"mid", 3, 't', "tnn", true}, {"slice", 3, 't', "tnn", true},
    {"replace", 3, 't', "ttt", true}, {"char_at", 2, 't', "tn", true},
    /* more numbers, as the other pseudocodes have them */
    {"pi", 0, 'r', "", true}, {"radians", 1, 'r', "n", true}, {"degrees", 1, 'r', "n", true}, {"root", 2, 'r', "nn", true},
    {"log_base", 2, 'r', "nn", true}, {"square", 1, 'n', "n", true}, {"round_to", 2, 'r', "nn", true},
    {"quotient", 2, 'i', "nn", true}, {"remainder", 2, 'i', "nn", true}, {"real_of", 1, 'r', "n", true},
    {"position_from", 3, 'i', "ttn", true}, {"whole_text", 2, 't', "nn", true}, {"whole_from", 2, 'i', "tn", true},
    {"is_whole", 2, 'b', "tn", true}, {"is_number", 1, 'b', "t", true},
    /* time */
    {"wait", 1, 'v', "n", true}, {"clock", 0, 'i', "", true},
    /* files: a file is its number, or its name */
    {"file_open", 2, 'i', "tn", true}, {"file_read", 1, 't', "a", true}, {"file_write", 2, 'v', "aa", true},
    {"file_put", 2, 'v', "aa", true}, {"file_eof", 1, 'b', "a", true}, {"file_close", 1, 'v', "a", true},
    {"file_exists", 1, 'b', "t", true},
    /* drawings */
    {"canvas", 2, 'v', "nn", true}, {"color", 1, 'v', "n", true}, {"rgb", 3, 'i', "nnn", true}, {"clear", 0, 'v', "", true},
    {"rect", 5, 'v', "nnnnn", true}, {"ellipse", 5, 'v', "nnnnn", true}, {"line", 4, 'v', "nnnn", true},
    {"point", 2, 'v', "nn", true}, {"draw_text", 3, 'v', "nnt", true}, {"show", 0, 'v', "", true},
    {"canvas_width", 0, 'i', "", true}, {"canvas_height", 0, 'i', "", true}, {"canvas_on", 1, 'v', "a", true},
    {"canvas_off", 0, 'v', "", true}, {"canvas_title", 1, 'v', "t", true}, {"rounded_rect", 6, 'v', "nnnnaa", true},
};

const Builtin *builtin_named(const char *name) {
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++)
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    return NULL;
}
