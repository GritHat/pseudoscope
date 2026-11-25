/* Components, messages and tests in the other languages. (To be written.) */

static void system_statement(Gen *g, const Stmt *s) {
    (void)s;
    put(g, PY ? "pass\n" : "// ...\n");
}

static void component_parts(Gen *g, const Class *c, int part) {
    (void)g;
    (void)c;
    (void)part;
}

static void simulation(Gen *g, int part) {
    (void)g;
    (void)part;
}
