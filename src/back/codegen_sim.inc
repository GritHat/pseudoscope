/* Components, messages and tests in C: what each program adds to the
 * simulation, which is the same for all (SIMULATION, below). */

static const char *const SIMULATION[] = {
    "/* A value that travels with a message. */\n"
    "typedef union {\n"
    "    int i;\n"
    "    float f;\n"
    "    bool b;\n"
    "    char c;\n"
    "    const char *s;\n"
    "    void *p;\n"
    "} plike_arg;\n\n"
    "typedef struct {\n"
    "    void *self;\n"
    "    char name[48];\n"
    "    int kind;\n"
    "    bool starts;\n"
    "    bool (*receive)(void *self, const char *name, const plike_arg *args);\n"
    "} plike_component;\n\n"
    "typedef struct {\n"
    "    int time, order, sender, to;\n"
    "    const char *name;\n"
    "    plike_arg args[8];\n"
    "    bool lost;\n"
    "} plike_event;\n\n"
    "/* Messages take time and can get lost; the same seed gives the same run.\n"
    " * Who takes part is known by a number: -1 is whoever is outside. */\n"
    "static struct {\n"
    "    int now, sent, expected, current, sender;\n"
    "    bool stopped;\n"
    "    plike_event queue[4096], delivered[8192];\n"
    "    int queued, delivered_count;\n"
    "    int links[64][8], link_count;\n"
    "    plike_component components[256];\n"
    "    int component_count;\n"
    "    struct {\n"
    "        bool ok;\n"
    "        int line;\n"
    "        char text[240];\n"
    "    } checks[1024];\n"
    "    int check_count;\n"
    "} plike_sim;\n\n"
    "int plike_id(const void *self) {\n"
    "    for (int i = 0; i < plike_sim.component_count; i++)\n"
    "        if (plike_sim.components[i].self == self) return i;\n"
    "    return -1;\n"
    "}\n\n"
    "void *plike_who(int id) { return id < 0 ? NULL : plike_sim.components[id].self; }\n\n"
    "void plike_add(void *self, const char *name, int index, int kind, bool starts,\n"
    "                      bool (*receive)(void *, const char *, const plike_arg *)) {\n"
    "    plike_component *c = &plike_sim.components[plike_sim.component_count++];\n"
    "    c->self = self;\n"
    "    c->kind = kind;\n"
    "    c->starts = starts;\n"
    "    c->receive = receive;\n"
    "    if (index < 0) snprintf(c->name, sizeof c->name, \"%s\", name);\n"
    "    else snprintf(c->name, sizeof c->name, \"%s[%d]\", name, index);\n"
    "}\n\n"
    "void plike_link(int a, int a_count, int b, int b_count, bool both, int delay, int delay_max, int loss) {\n"
    "    int *l = plike_sim.links[plike_sim.link_count++];\n"
    "    l[0] = a, l[1] = a + a_count, l[2] = b, l[3] = b + b_count, l[4] = both, l[5] = delay, l[6] = delay_max, l[7] = loss;\n"
    "}\n\n"
    "void plike_post(int time, int sender, int to, const char *name, int count, const plike_arg *args, bool lost) {\n"
    "    if (plike_sim.queued == 4096) return;\n"
    "    plike_event *e = &plike_sim.queue[plike_sim.queued++];\n"
    "    e->time = time;\n"
    "    e->order = plike_sim.sent++;\n"
    "    e->sender = sender;\n"
    "    e->to = to;\n"
    "    e->name = name;\n"
    "    e->lost = lost;\n"
    "    for (int i = 0; i < count && i < 8; i++) e->args[i] = args[i];\n"
    "}\n\n"
    "void plike_start(void) {\n"
    "    for (int i = 0; i < plike_sim.component_count; i++)\n"
    "        if (plike_sim.components[i].starts) plike_post(0, -1, i, \"start\", 0, NULL, false);\n"
    "}\n\n",
    "void plike_send(int to, const char *name, int after, int count, const plike_arg *args) {\n"
    "    int from = plike_sim.current, delay = from == to ? 0 : 1, loss = 0;\n"
    "    for (int i = 0; i < plike_sim.link_count && from != to; i++) {\n"
    "        const int *l = plike_sim.links[i];\n"
    "        bool there = l[0] <= from && from < l[1] && l[2] <= to && to < l[3];\n"
    "        bool back = l[4] && l[2] <= from && from < l[3] && l[0] <= to && to < l[1];\n"
    "        if (!there && !back) continue;\n"
    "        delay = l[6] > l[5] ? plike_random(l[5], l[6]) : l[5];\n"
    "        loss = l[7];\n"
    "    }\n"
    "    if (delay < 0) delay = 0;\n"
    "    if (after < 0) after = 0;\n"
    "    bool lost = loss > 0 && plike_random(0, 99) < loss;\n"
    "    plike_post(plike_sim.now + after + (lost ? (delay + 1) / 2 : delay), from, to, name, count, args, lost);\n"
    "}\n\n"
    "void plike_send_all(int kind, const char *name, int after, int count, const plike_arg *args) {\n"
    "    for (int i = 0; i < plike_sim.component_count; i++)\n"
    "        if (plike_sim.components[i].kind == kind && i != plike_sim.current) plike_send(i, name, after, count, args);\n"
    "}\n\n"
    "void plike_cancel(const char *name) {\n"
    "    int kept = 0, me = plike_sim.current;\n"
    "    for (int i = 0; i < plike_sim.queued; i++) {\n"
    "        const plike_event *e = &plike_sim.queue[i];\n"
    "        if (!(e->sender == me && e->to == me && strcmp(e->name, name) == 0)) plike_sim.queue[kept++] = *e;\n"
    "    }\n"
    "    plike_sim.queued = kept;\n"
    "}\n\n"
    "void plike_stop(void) {\n"
    "    plike_sim.stopped = true;\n"
    "    plike_sim.queued = 0;\n"
    "}\n\n"
    "/* limit < 0: until nothing is left to do */\n"
    "void plike_run(int limit) {\n"
    "    int deadline = limit < 0 ? -1 : plike_sim.now + limit;\n"
    "    while (!plike_sim.stopped && plike_sim.queued > 0) {\n"
    "        int best = 0;\n"
    "        for (int i = 1; i < plike_sim.queued; i++) {\n"
    "            const plike_event *e = &plike_sim.queue[i], *b = &plike_sim.queue[best];\n"
    "            if (e->time < b->time || (e->time == b->time && e->order < b->order)) best = i;\n"
    "        }\n"
    "        if (deadline >= 0 && plike_sim.queue[best].time > deadline) break;\n"
    "        plike_event e = plike_sim.queue[best];\n"
    "        for (int i = best; i + 1 < plike_sim.queued; i++) plike_sim.queue[i] = plike_sim.queue[i + 1];\n"
    "        plike_sim.queued--;\n"
    "        if (e.time > plike_sim.now) plike_sim.now = e.time;\n"
    "        if (e.lost) continue;\n"
    "        plike_sim.current = e.to;\n"
    "        plike_sim.sender = e.sender;\n"
    "        const plike_component *c = &plike_sim.components[e.to];\n"
    "        if (c->receive(c->self, e.name, e.args) && plike_sim.delivered_count < 8192)\n"
    "            plike_sim.delivered[plike_sim.delivered_count++] = e;\n"
    "        plike_sim.current = plike_sim.sender = -1;\n"
    "    }\n"
    "    if (deadline > plike_sim.now && !plike_sim.stopped) plike_sim.now = deadline;\n"
    "}\n\n"
    "void plike_check(bool ok, int line, const char *text, const char *left) {\n"
    "    if (plike_sim.check_count == 1024) return;\n"
    "    plike_sim.checks[plike_sim.check_count].ok = ok;\n"
    "    plike_sim.checks[plike_sim.check_count].line = line;\n"
    "    char *out = plike_sim.checks[plike_sim.check_count++].text;\n"
    "    if (ok || !left[0]) snprintf(out, 240, \"%s\", text);\n"
    "    else snprintf(out, 240, \"%s (= %s)\", text, left);\n"
    "}\n\n"
    "void plike_expect(int a, int b, const char *name, int line) {\n"
    "    char text[200];\n"
    "    snprintf(text, sizeof text, \"%s -> %s: %s\", a < 0 ? \"test\" : plike_sim.components[a].name,\n"
    "             plike_sim.components[b].name, name);\n"
    "    for (int i = plike_sim.expected; i < plike_sim.delivered_count; i++) {\n"
    "        const plike_event *e = &plike_sim.delivered[i];\n"
    "        if (e->sender != a || e->to != b || strcmp(e->name, name) != 0) continue;\n"
    "        plike_sim.expected = i + 1;\n"
    "        plike_check(true, line, text, \"\");\n"
    "        return;\n"
    "    }\n"
    "    plike_check(false, line, text, \"\");\n"
    "}\n\n"
    "/* A number, as the checks show it. */\n"
    "const char *plike_shown(double x, char *text) {\n"
    "    snprintf(text, 48, \"%.6g\", x);\n"
    "    if (!strpbrk(text, \".en\")) strcat(text, \".0\");\n"
    "    return text;\n"
    "}\n\n",
    NULL,
};

static bool sim_sends_in(const Expr *e, int depth);

/* Does it send or cancel messages, by itself or through what it calls? */
static bool sim_sends(const Stmt *s, int depth) {
    for (; s; s = s->next) {
        if (s->kind == ST_SEND || s->kind == ST_CANCEL) return true;
        if (sim_sends_in(s->value, depth) || sim_sends_in(s->target, depth)) return true;
        for (int i = 0; i < s->args.count; i++)
            if (sim_sends_in(s->args.items[i], depth)) return true;
        if (sim_sends(s->body, depth)) return true;
        for (const Stmt *o = s->otherwise; o; o = o->otherwise)
            if (sim_sends_in(o->value, depth) || sim_sends(o->body, depth)) return true;
    }
    return false;
}

static bool sim_sends_in(const Expr *e, int depth) {
    if (!e || depth > 4) return false;
    if (e->is_call && e->symbol && e->symbol->function && sim_sends(e->symbol->function->body, depth + 1)) return true;
    if (sim_sends_in(e->base, depth) || sim_sends_in(e->right, depth)) return true;
    for (int i = 0; i < e->args.count; i++)
        if (sim_sends_in(e->args.items[i], depth)) return true;
    return false;
}

/* The number of a component. */
static void gen_component_id(Gen *g, const Expr *e) {
    if (!e || e->type->kind == TY_VOID) {
        put(g, "-1");
    } else if (e->kind == EX_NAME && e->symbol && e->symbol->kind == SYM_SENDER) {
        put(g, "plike_sim.sender");
    } else {
        put(g, "plike_id(");
        if (e->type->kind == TY_POINTER) gen_expr(g, e, P_LOWEST);
        else gen_address(g, e, P_LOWEST);
        put(g, ")");
    }
}

static char arg_member(const Type *type) {
    switch (type->kind) {
        case TY_REAL: return 'f';
        case TY_BOOL: return 'b';
        case TY_CHAR: return 'c';
        case TY_STRING: return 's';
        case TY_POINTER: return 'p';
        default: return 'i';
    }
}

static void gen_c_string(Gen *g, const char *text) {
    put(g, "\"");
    for (const char *c = text; *c; c++) {
        if (*c == '"' && (c == text || c[-1] != '\\')) put(g, "\\");
        strbuf_append(g->out, c, 1);
    }
    put(g, "\"");
}

static int class_index(const Gen *g, const Class *klass) {
    int i = 0;
    for (const Class *c = g->program->classes; c && c != klass; c = c->next) i++;
    return i;
}

static void gen_check(Gen *g, const Stmt *s) {
    const Expr *e = s->value;
    StrBuf text = {0};
    strbuf_puts(&text, "");
    if (s->text) strbuf_puts(&text, s->text);
    else print_expr(NULL, e, NULL, 0, "", "", "", "", &text);
    bool compared = e->kind == EX_BINARY && e->op != TOK_AND && e->op != TOK_OR;
    TypeKind kind = compared ? e->base->type->kind : TY_VOID;
    if (compared && kind != TY_RECORD && kind != TY_ARRAY && kind != TY_POINTER) {
        /* the left side is kept: if the check fails, its value is shown */
        Symbol *sym = ARENA_NEW(g->arena, Symbol);
        sym->kind = SYM_VAR;
        sym->name = sym->c_name = "plike_left";
        sym->type = e->base->type;
        Expr *held = ARENA_NEW(g->arena, Expr), *again = ARENA_NEW(g->arena, Expr);
        held->kind = EX_NAME;
        held->loc = e->base->loc;
        held->text = "plike_left";
        held->symbol = sym;
        held->type = e->base->type;
        *again = *e;
        again->base = held;
        put(g, "{\n");
        g->indent++;
        indent(g);
        gen_declarator(g, base_type(g, e->base->type), "", "plike_left = ");
        gen_expr(g, e->base, P_LOWEST);
        put(g, ";\n");
        indent(g);
        put(g, "char plike_text[64];\n");
        indent(g);
        if (e->base->type->states) strbuf_printf(g->out, "snprintf(plike_text, sizeof plike_text, \"%%s\", %s_states[plike_left]);\n", e->base->type->states->record->c_name);
        else if (kind == TY_REAL) put(g, "plike_shown(plike_left, plike_text);\n");
        else if (kind == TY_BOOL) put(g, "snprintf(plike_text, sizeof plike_text, \"%s\", plike_left ? \"true\" : \"false\");\n");
        else if (kind == TY_STRING) put(g, "snprintf(plike_text, sizeof plike_text, \"\\\"%.60s\\\"\", plike_left);\n");
        else if (kind == TY_CHAR) put(g, "snprintf(plike_text, sizeof plike_text, \"'%c'\", plike_left);\n");
        else put(g, "snprintf(plike_text, sizeof plike_text, \"%d\", plike_left);\n");
        indent(g);
        put(g, "plike_check(");
        gen_expr(g, again, P_LOWEST);
        strbuf_printf(g->out, ", %d, ", s->loc.line);
        gen_c_string(g, text.data);
        put(g, ", plike_text);\n");
        g->indent--;
        indent(g);
        put(g, "}\n");
    } else {
        put(g, "plike_check(");
        gen_expr(g, e, P_LOWEST);
        strbuf_printf(g->out, ", %d, ", s->loc.line);
        gen_c_string(g, text.data);
        put(g, ", \"\");\n");
    }
    strbuf_free(&text);
}

static void gen_system_statement(Gen *g, const Stmt *s) {
    switch (s->kind) {
        case ST_SEND: {
            const Symbol *handler = s->value->symbol;
            int n = s->value->args.count;
            if (s->to_all) {
                strbuf_printf(g->out, "plike_send_all(%d, ", class_index(g, s->to_all));
            } else {
                put(g, "plike_send(");
                gen_component_id(g, s->target);
                put(g, ", ");
            }
            strbuf_printf(g->out, "\"%s\", ", s->name);
            if (s->limit) gen_expr(g, s->limit, P_LOWEST);
            else put(g, "0");
            strbuf_printf(g->out, ", %d, ", n);
            if (n == 0) put(g, "NULL");
            else put(g, "(plike_arg[]){");
            for (int i = 0; i < n; i++) {
                strbuf_printf(g->out, "%s{.%c = ", i ? ", " : "", arg_member(handler->params[i + 1]->type));
                gen_expr(g, s->value->args.items[i], P_LOWEST);
                put(g, "}");
            }
            put(g, n ? "});\n" : ");\n");
            break;
        }
        case ST_STOP: put(g, "plike_stop();\n"); break;
        case ST_CANCEL: strbuf_printf(g->out, "plike_cancel(\"%s\");\n", s->name); break;
        case ST_RUN:
            put(g, "plike_run(");
            if (s->value) gen_expr(g, s->value, P_LOWEST);
            else put(g, "-1");
            put(g, ");\n");
            break;
        case ST_EXPECT:
            put(g, "plike_expect(");
            gen_component_id(g, s->target);
            put(g, ", ");
            gen_component_id(g, s->limit);
            strbuf_printf(g->out, ", \"%s\", %d);\n", s->name, s->loc.line);
            break;
        case ST_CHECK: gen_check(g, s); break;
        case ST_STEP: strbuf_printf(g->out, "/* %s */\n", s->text); break;
        default: put(g, "/* ... */\n"); break;
    }
}

/* How each component takes its messages, and the names of its states. */
static void gen_components(Gen *g) {
    for (const Class *c = g->program->classes; c; c = c->next) {
        if (!c->is_component) continue;
        const char *name = c->record->c_name;
        if (c->states) {
            strbuf_printf(g->out, "\nstatic const char *const %s_states[] = {", name);
            for (const StateName *s = c->states; s; s = s->next) strbuf_printf(g->out, "%s\"%s\"", s == c->states ? "" : ", ", s->name);
            put(g, "};\n");
        }
        strbuf_printf(g->out, "\nstatic bool %s_receive(void *self_, const char *name, const plike_arg *args) {\n"
                              "    struct %s *self = self_;\n    (void)self;\n    (void)args;\n", name, name);
        for (const Function *f = g->program->functions; f; f = f->next) {
            if (f->owner != c || !f->is_handler || !f->symbol) continue;
            strbuf_printf(g->out, "    if (strcmp(name, \"%s\") == 0", f->name);
            if (f->guards) {
                put(g, " && (");
                for (const StateName *guard = f->guards; guard; guard = guard->next) {
                    int value = 0;
                    for (const StateName *s = c->states; s && strcmp(s->name, guard->name) != 0; s = s->next) value++;
                    strbuf_printf(g->out, "%sself->state == %d", guard == f->guards ? "" : " || ", value);
                }
                put(g, ")");
            }
            strbuf_printf(g->out, ") {\n        %s(self", f->symbol->c_name);
            for (int i = 1; i < f->symbol->param_count; i++)
                strbuf_printf(g->out, ", args[%d].%c", i - 1, arg_member(f->symbol->params[i]->type));
            put(g, ");\n        return true;\n    }\n");
        }
        put(g, "    return false;\n}\n");
    }
}

static bool is_component_variable(const Type *type) {
    while (type->kind == TY_ARRAY) type = type->base;
    return type->kind == TY_RECORD && type->record->klass && type->record->klass->is_component;
}

static void gen_link_end(Gen *g, const Expr *e) {
    if (e->type->kind == TY_ARRAY) {
        long long lo = 0, hi = 0;
        const Dim *dim = &e->type->dims.items[0];
        constant_lo(g, dim, &lo);
        if (dim->hi) const_int(dim->hi, &hi);
        put(g, "plike_id(&");
        gen_expr(g, e, P_POSTFIX);
        strbuf_printf(g->out, "[0]), %lld", dim->lo ? hi - lo + 1 : hi);
    } else {
        gen_component_id(g, e);
        put(g, ", 1");
    }
}

/* Everything as at the beginning, the tests one by one, and where it all starts. */
static void gen_system_main(Gen *g) {
    const Program *p = g->program;
    long long seed = 1;
    if (p->seed) const_int(p->seed, &seed);
    put(g, "\n/* Everything as at the beginning: each test starts from there. */\nstatic void plike_reset(void) {\n");
    for (const VarDecl *v = p->globals; v; v = v->next)
        strbuf_printf(g->out, "    memset(&%s, 0, sizeof %s);\n", v->symbol->c_name, v->symbol->c_name);
    strbuf_printf(g->out, "    memset(&plike_sim, 0, sizeof plike_sim);\n    plike_sim.current = plike_sim.sender = -1;\n    plike_seed = %uu;\n", (unsigned)seed);
    for (const VarDecl *v = p->globals; v; v = v->next) {
        const Symbol *sym = v->symbol;
        if (!is_component_variable(sym->type)) continue;
        const Type *type = sym->type;
        const Class *c = (type->kind == TY_ARRAY ? type->base : type)->record->klass;
        const char *record = c->record->c_name;
        bool starts = find_method(p, c, "start", true) != NULL;
        if (type->kind != TY_ARRAY) {
            strbuf_printf(g->out, "    plike_add(&%s, \"%s\", -1, %d, %s, %s_receive);\n", sym->c_name, sym->name, class_index(g, c),
                          starts ? "true" : "false", record);
            continue;
        }
        long long lo = 0, hi = 0;
        const Dim *dim = &type->dims.items[0];
        constant_lo(g, dim, &lo);
        if (dim->hi) const_int(dim->hi, &hi);
        strbuf_printf(g->out, "    for (int i = 0; i < %lld; i++) plike_add(&%s[i], \"%s\", i + %lld, %d, %s, %s_receive);\n",
                      dim->lo ? hi - lo + 1 : hi, sym->c_name, sym->name, lo, class_index(g, c), starts ? "true" : "false", record);
    }
    g->indent = 1;
    for (const Link *l = p->links; l; l = l->next) {
        indent(g);
        put(g, "plike_link(");
        gen_link_end(g, l->from);
        put(g, ", ");
        gen_link_end(g, l->to);
        put(g, l->both_ways ? ", true, " : ", false, ");
        if (l->delay) gen_expr(g, l->delay, P_LOWEST);
        else put(g, "1");
        put(g, ", ");
        if (l->delay_max) gen_expr(g, l->delay_max, P_LOWEST);
        else put(g, "-1");
        put(g, ", ");
        if (l->loss) gen_expr(g, l->loss, P_LOWEST);
        else put(g, "0");
        put(g, ");\n");
    }
    g->indent = 0;
    put(g, "    plike_start();\n}\n");
    int tests = 0;
    for (const Function *f = p->functions; f; f = f->next) tests += f->is_test;
    if (tests) {
        put(g, "\nstatic int plike_run_tests(void) {\n    static const struct {\n        const char *name;\n        void (*run)(void);\n    } tests[] = {\n");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) strbuf_printf(g->out, "        {\"%s\", %s},\n", f->name, f->symbol->c_name);
        strbuf_printf(g->out, "    };\n    int failed = 0;\n    for (int t = 0; t < %d; t++) {\n        plike_reset();\n        tests[t].run();\n"
                              "        bool ok = true;\n        for (int i = 0; i < plike_sim.check_count; i++) ok = ok && plike_sim.checks[i].ok;\n"
                              "        printf(\"%%s %%s\\n\", ok ? \"✓\" : \"✗\", tests[t].name);\n        if (!ok) failed++;\n"
                              "        for (int i = 0; i < plike_sim.check_count; i++)\n"
                              "            if (!plike_sim.checks[i].ok) printf(\"    ✗ line %%d: %%s\\n\", plike_sim.checks[i].line, plike_sim.checks[i].text);\n"
                              "    }\n    printf(\"%d tests, %%d failed\\n\", failed);\n    return failed ? 1 : 0;\n}\n", tests, tests);
    }
    bool run = has_main(p), components = has_components(p);
    put(g, "\nint main(int argc, char **argv) {\n");
    if (tests && (run || components)) put(g, "    if (argc > 1 && strcmp(argv[1], \"test\") == 0) return plike_run_tests();\n");
    else if (tests) put(g, "    if (argc > 0) return plike_run_tests();\n");
    put(g, "    (void)argc;\n    (void)argv;\n    plike_reset();\n");
    if (run) put(g, "    program_main();\n");
    if (components) put(g, "    plike_run(-1);\n");
    put(g, "    return 0;\n}\n");
}
