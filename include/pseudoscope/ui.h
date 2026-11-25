#ifndef PSEUDOSCOPE_PUBLIC_UI_H
#define PSEUDOSCOPE_PUBLIC_UI_H

#include "pseudoscope/config.h"

#include <stddef.h>

typedef struct {
    Config config;
    const char *path;      /* a file or a directory */
    /* When set, no terminal is used: these keys are pressed, then the screen
     * is written to the standard output. */
    const char *keys;
    int width, height;
    bool colors;
    int target;            /* the language translations are shown in (a Lang) */
} UiOptions;

int run_ui(const UiOptions *options);

#endif
