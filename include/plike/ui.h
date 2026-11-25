#ifndef PLIKE_PUBLIC_UI_H
#define PLIKE_PUBLIC_UI_H

#include "plike/config.h"

#include <stddef.h>

typedef struct {
    Config config;
    const char *path;      /* a file or a directory */
    bool italian;
    /* When set, no terminal is used: these keys are pressed, then the screen
     * is written to the standard output. */
    const char *keys;
    int width, height;
    bool colors;
} UiOptions;

int run_ui(const UiOptions *options);

#endif
