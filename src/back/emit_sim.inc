/* Components, messages and tests: what the simulation needs from each program.
 * The simulation itself (time, the messages on their way, what gets lost) is
 * the same for every program: see runtime.c. */

static bool can_simulate(const Gen *g) {
    return g->lang != LANG_C;
}

/* A component, as the simulation holds it. */
static void component_ref(Gen *g, const Expr *e) {
    if (RUST) { /* its number */
        if (!e || e->type->kind == TY_VOID) {
            put(g, "-1");
        } else if (e->kind == EX_NAME && e->symbol && e->symbol->kind == SYM_SENDER) {
            put(g, "(*SIM).sender");
        } else if (e->type->kind == TY_POINTER) {
            put(g, "(*");
            expr(g, e, P_UNARY);
            put(g, ").sim_id");
        } else {
            expr(g, e, P_POSTFIX);
            put(g, ".sim_id");
        }
        return;
    }
    if (!e || e->type->kind == TY_VOID) { /* whoever is outside: the test */
        put(g, null_text(g));
    } else if (CPP && e->type->kind != TY_POINTER) {
        if (is_self(e)) {
            put(g, "this");
        } else {
            put(g, "&");
            expr(g, e, P_UNARY);
        }
    } else if (TS && e->type->kind == TY_POINTER) {
        expr(g, e, P_POSTFIX);
        put(g, "!");
    } else {
        expr(g, e, P_LOWEST);
    }
}

/* A value that travels with a message: of exactly the type that is awaited. */
static void message_value(Gen *g, const Expr *e, const Type *want) {
    TypeKind from = e->type->kind, to = want->kind;
    if (RUST) {
        static const char *const ARGS[] = {[TY_INT] = "Int", [TY_REAL] = "Real", [TY_BOOL] = "Bool", [TY_CHAR] = "Char",
                                           [TY_STRING] = "Text", [TY_POINTER] = "Who"};
        putf(g, "Arg::%s(", to <= TY_POINTER && ARGS[to] ? ARGS[to] : "Int");
        value(g, e, want, P_LOWEST);
        put(g, ")");
        return;
    }
    if (CPP && to == TY_STRING) {
        put(g, "std::string(");
        value(g, e, want, P_LOWEST);
        put(g, ")");
    } else if ((CPP || JAVA) && from != to && (to == TY_REAL || to == TY_INT)) {
        putf(g, CPP ? "(%s)" : "(%s) ", scalar_type(g, want));
        value(g, e, want, P_UNARY);
    } else {
        value(g, e, want, P_LOWEST);
    }
}

static void string_literal(Gen *g, const char *text) {
    put(g, "\"");
    for (const char *c = text; *c; c++) {
        if (*c == '"' && (c == text || c[-1] != '\\')) put(g, "\\");
        putf(g, "%c", *c);
    }
    put(g, "\"");
}

/* The text of a value, as the checks show it. */
static void shown_value(Gen *g, const Type *type, const char *name) {
    if (type->states) {
        if (RUST) putf(g, "String::from(%s_STATES[%s as usize])", class_name(g, type->states), name);
        else putf(g, CPP ? "%s::state_names[%s]" : "%s.STATES[%s]", class_name(g, type->states), name);
        return;
    }
    if (RUST) {
        switch (type->kind) {
            case TY_REAL: putf(g, "shown(%s)", name); break;
            case TY_STRING: putf(g, "format!(\"\\\"{}\\\"\", %s)", name); break;
            case TY_CHAR: putf(g, "format!(\"'{}'\", %s)", name); break;
            case TY_INT: case TY_BOOL: putf(g, "%s.to_string()", name); break;
            default: put(g, "String::from(\"?\")");
        }
        return;
    }
    switch (type->kind) {
        case TY_INT: putf(g, CPP ? "std::to_string(%s)" : JAVA ? "String.valueOf(%s)" : PY ? "str(%s)" : "String(%s)", name); break;
        case TY_REAL: putf(g, "shown(%s)", name); break;
        case TY_BOOL: putf(g, PY ? "(\"true\" if %s else \"false\")" : CPP ? "std::string(%s ? \"true\" : \"false\")" : "(%s ? \"true\" : \"false\")", name); break;
        case TY_STRING: putf(g, PY ? "'\"' + %s + '\"'" : "\"\\\"\" + %s + \"\\\"\"", name); break;
        case TY_CHAR: putf(g, CPP ? "std::string(\"'\") + %s + \"'\"" : PY ? "\"'\" + %s + \"'\"" : "\"'\" + %s + \"'\"", name); break;
        default: put(g, "\"?\"");
    }
}

static void check_statement(Gen *g, const Stmt *s) {
    const Expr *e = s->value;
    StrBuf text = {0};
    strbuf_puts(&text, "");
    if (s->text) strbuf_puts(&text, s->text);
    else print_expr(NULL, e, NULL, 0, "", "", "", "", &text);
    bool compared = e->kind == EX_BINARY && e->op != TOK_AND && e->op != TOK_OR;
    TypeKind kind = compared ? e->base->type->kind : TY_VOID;
    if (compared && kind != TY_RECORD && kind != TY_ARRAY && kind != TY_POINTER) {
        /* the left side is kept: if the check fails, its value is shown */
        const char *temp = arena_printf(g->arena, "left%d", ++g->serial);
        Symbol *sym = ARENA_NEW(g->arena, Symbol);
        sym->kind = SYM_VAR;
        sym->name = sym->c_name = temp;
        sym->type = e->base->type;
        Expr *held = ARENA_NEW(g->arena, Expr), *again = ARENA_NEW(g->arena, Expr);
        held->kind = EX_NAME;
        held->loc = e->base->loc;
        held->text = temp;
        held->symbol = sym;
        held->type = e->base->type;
        *again = *e;
        again->base = held;
        putf(g, CPP ? "auto %s = " : JAVA ? "var %s = " : PY ? "%s = " : RUST ? "let %s = " : "const %s = ", temp);
        value(g, e->base, e->base->type, P_LOWEST);
        end(g);
        indent(g);
        put(g, RUST ? "sim_check(" : "sim.check(");
        value(g, again, &(Type){.kind = TY_BOOL}, P_LOWEST);
        putf(g, ", %d, ", s->loc.line);
        if (s->text) putf(g, "\"%s\"", s->text);
        else string_literal(g, text.data);
        put(g, ", ");
        shown_value(g, e->base->type, temp);
        put(g, ")");
    } else {
        put(g, RUST ? "sim_check(" : "sim.check(");
        value(g, e, &(Type){.kind = TY_BOOL}, P_LOWEST);
        putf(g, ", %d, ", s->loc.line);
        if (s->text) putf(g, "\"%s\"", s->text);
        else string_literal(g, text.data);
        if (CPP || JAVA) put(g, ", \"\"");
        if (RUST) put(g, ", String::new()");
        put(g, ")");
    }
    end(g);
    strbuf_free(&text);
}

static void system_statement(Gen *g, const Stmt *s) {
    if (!can_simulate(g)) {
        put(g, PY ? "pass\n" : "// ...\n");
        return;
    }
    switch (s->kind) {
        case ST_SEND: {
            const Symbol *handler = s->value->symbol;
            if (s->to_all && RUST) {
                putf(g, "sim_send_all(\"%s\", ", class_name(g, s->to_all));
            } else if (RUST) {
                put(g, "sim_send(");
                component_ref(g, s->target);
                put(g, ", ");
            } else if (s->to_all) {
                if (CPP) putf(g, "sim.send_all<%s>(", class_name(g, s->to_all));
                else putf(g, PY ? "sim.send_all(%s, " : JAVA ? "sim.sendAll(%s.class, " : "sim.sendAll(%s, ", class_name(g, s->to_all));
            } else {
                put(g, "sim.send(");
                component_ref(g, s->target);
                put(g, ", ");
            }
            putf(g, "\"%s\", ", s->name);
            if (s->limit) value(g, s->limit, &(Type){.kind = TY_INT}, P_LOWEST);
            else put(g, "0");
            int n = s->value->args.count;
            if (n && CPP) put(g, ", {");
            if (RUST) put(g, n ? ", vec![" : ", Vec::new()");
            for (int i = 0; i < n; i++) {
                if (i || !(CPP || RUST)) put(g, ", ");
                message_value(g, s->value->args.items[i], handler->params[i + 1]->type);
            }
            if (n && CPP) put(g, "}");
            if (n && RUST) put(g, "]");
            put(g, ")");
            end(g);
            break;
        }
        case ST_STOP:
            put(g, RUST ? "sim_stop()" : "sim.stop()");
            end(g);
            break;
        case ST_CANCEL:
            putf(g, RUST ? "sim_cancel(\"%s\")" : "sim.cancel(\"%s\")", s->name);
            end(g);
            break;
        case ST_RUN:
            put(g, RUST ? "sim_run(" : "sim.run(");
            if (s->value) value(g, s->value, &(Type){.kind = TY_INT}, P_LOWEST);
            else if (JAVA || RUST) put(g, "-1");
            put(g, ")");
            end(g);
            break;
        case ST_EXPECT:
            put(g, RUST ? "sim_expect(" : "sim.expect(");
            component_ref(g, s->target);
            put(g, ", ");
            component_ref(g, s->limit);
            putf(g, ", \"%s\", %d)", s->name, s->loc.line);
            end(g);
            break;
        case ST_CHECK: check_statement(g, s); break;
        case ST_STEP: putf(g, PY ? "# %s\n" : "// %s\n", s->text); break;
        default: put(g, PY ? "pass\n" : "// ...\n"); break;
    }
}

static const Function *start_handler(const Gen *g, const Class *c) { return find_method(g->program, c, "start", true); }

/* A message in, a method called: by its name, and by the state it finds. */
static void receive_body(Gen *g, const Class *c) {
    for (const Function *f = g->program->functions; f; f = f->next) {
        if (f->owner != c || !f->is_handler) continue;
        indent(g);
        putf(g, JAVA ? "if (name.equals(\"%s\")" : JS ? "if (name === \"%s\"" : PY || RUST ? "if name == \"%s\"" : "if (name == \"%s\"", f->name);
        if (f->guards) {
            put(g, PY ? " and (" : " && (");
            for (const StateName *s = f->guards; s; s = s->next) {
                if (s != f->guards) put(g, PY ? " or " : " || ");
                if (CPP || JAVA) putf(g, "state == %s", safe(g, s->name));
                else if (RUST) putf(g, "self.state == %s_%s", class_name(g, c), s->name);
                else putf(g, JS ? "this.state === %s.%s" : "self.state == %s.%s", class_name(g, c), safe(g, s->name));
            }
            put(g, ")");
        }
        put(g, PY ? ":\n" : RUST ? " {\n" : ") {\n");
        g->indent++;
        indent(g);
        putf(g, CPP || JAVA ? "%s(" : "%s.%s(", CPP || JAVA ? function_name(g, f) : self_name(g), function_name(g, f));
        for (int i = 1; i < f->symbol->param_count; i++) {
            const Type *type = f->symbol->params[i]->type;
            if (i > 1) put(g, ", ");
            if (CPP) {
                putf(g, "std::any_cast<%s>(args[%d])", type_text(g, type), i - 1);
            } else if (RUST) {
                static const char *const TAKES[] = {[TY_INT] = "int", [TY_REAL] = "real", [TY_BOOL] = "bool", [TY_CHAR] = "char",
                                                    [TY_STRING] = "text", [TY_POINTER] = "who"};
                putf(g, "args[%d].%s()", i - 1, type->kind <= TY_POINTER && TAKES[type->kind] ? TAKES[type->kind] : "int");
                if (type->kind == TY_POINTER && type->base->kind == TY_RECORD) putf(g, " as %s", type_text(g, type));
            } else if (JAVA) {
                static const char *const BOXES[] = {[TY_INT] = "Integer", [TY_REAL] = "Double", [TY_BOOL] = "Boolean",
                                                    [TY_CHAR] = "Character", [TY_STRING] = "String"};
                const char *box = type->kind <= TY_STRING && BOXES[type->kind] ? BOXES[type->kind] : type_text(g, type);
                putf(g, "(%s) args[%d]", box, i - 1);
            } else {
                putf(g, "args[%d]", i - 1);
            }
        }
        put(g, ")");
        end(g);
        indent(g);
        put(g, PY ? "return True\n" : "return true;\n");
        g->indent--;
        if (!PY) {
            indent(g);
            put(g, "}\n");
        }
    }
    indent(g);
    put(g, PY ? "return False\n" : RUST ? "false\n" : "return false;\n");
}

/* part 0: what a component declares first; part 1: how it takes a message. */
static void component_parts(Gen *g, const Class *c, int part) {
    if (!can_simulate(g)) return;
    const char *name = class_name(g, c);
    if (RUST) { /* its states before it, how it takes part after */
        int count = 0, value_ = 0;
        for (const StateName *s = c->states; s; s = s->next) count++;
        if (part == 2 && count) {
            for (const StateName *s = c->states; s; s = s->next, value_++) putf(g, "const %s_%s: i32 = %d;\n", name, s->name, value_);
            putf(g, "const %s_STATES: [&str; %d] = [", name, count);
            for (const StateName *s = c->states; s; s = s->next) putf(g, "%s\"%s\"", s == c->states ? "" : ", ", s->name);
            put(g, "];\n\n");
        }
        if (part != 3) return;
        putf(g, "impl Component for %s {\n    fn id(&self) -> i32 {\n        self.sim_id\n    }\n"
                "    fn name(&self) -> String {\n        self.sim_name.clone()\n    }\n"
                "    fn kind(&self) -> &'static str {\n        \"%s\"\n    }\n"
                "    fn register(&mut self, id: i32, name: String) {\n        self.sim_id = id;\n        self.sim_name = name;\n    }\n",
             name, name);
        if (start_handler(g, c)) put(g, "    fn starts(&self) -> bool {\n        true\n    }\n");
        put(g, "    unsafe fn receive(&mut self, name: &str, args: &[Arg]) -> bool {\n");
        g->indent = 2;
        receive_body(g, c);
        g->indent = 0;
        put(g, "    }\n}\n\n");
        return;
    }
    if (part == 0) {
        int count = 0;
        for (const StateName *s = c->states; s; s = s->next) count++;
        if (count && CPP) {
            put(g, "    enum {");
            for (const StateName *s = c->states; s; s = s->next) putf(g, "%s %s", s == c->states ? "" : ",", safe(g, s->name));
            put(g, " };\n    static constexpr const char *state_names[] = {");
            for (const StateName *s = c->states; s; s = s->next) putf(g, "%s\"%s\"", s == c->states ? "" : ", ", s->name);
            put(g, "};\n");
        } else if (count) {
            int value_ = 0;
            for (const StateName *s = c->states; s; s = s->next, value_++) {
                if (JAVA) putf(g, "    static final int %s = %d;\n", safe(g, s->name), value_);
                else if (PY) putf(g, "    %s = %d\n", safe(g, s->name), value_);
                else putf(g, "    static %s = %d;\n", safe(g, s->name), value_);
            }
            put(g, JAVA ? "    static final String[] STATES = {" : PY ? "    STATES = [" : "    static STATES = [");
            for (const StateName *s = c->states; s; s = s->next) putf(g, "%s\"%s\"", s == c->states ? "" : ", ", s->name);
            put(g, JAVA ? "};\n\n" : PY ? "]\n\n" : "];\n\n");
        }
        if (CPP) {
            if (start_handler(g, c)) put(g, "    bool starts() override { return true; }\n");
            put(g, "    bool receive(const std::string &name, const std::vector<std::any> &args) override;\n");
        }
        return;
    }
    if (CPP) return; /* written with the other definitions */
    if (start_handler(g, c)) {
        if (PY) put(g, "    def starts(self):\n        return True\n\n");
        else if (JAVA) put(g, "    boolean starts() {\n        return true;\n    }\n\n");
        else putf(g, "    starts()%s {\n        return true;\n    }\n\n", TS ? ": boolean" : "");
    }
    if (PY) put(g, "    def receive(self, name, args):\n");
    else if (JAVA) put(g, "    boolean receive(String name, Object[] args) {\n");
    else putf(g, "    receive(name%s, args%s)%s {\n", TS ? ": string" : "", TS ? ": any[]" : "", TS ? ": boolean" : "");
    g->indent = 2;
    receive_body(g, c);
    g->indent = 1;
    put(g, PY ? "\n" : "    }\n\n");
    (void)name;
}

static bool is_component_type(const Type *type) {
    while (type->kind == TY_ARRAY) type = type->base;
    return type->kind == TY_RECORD && type->record->klass && type->record->klass->is_component;
}

/* The first component of what a link names, and how many there are. */
static void link_end(Gen *g, const Expr *e) {
    if (e->type->kind == TY_ARRAY) {
        put(g, CPP ? "&" : "");
        expr(g, e, P_POSTFIX);
        put(g, "[0], ");
        if (CPP) put(g, "(int)");
        expr(g, e, P_POSTFIX);
        put(g, CPP ? ".size()" : ".length");
    } else {
        component_ref(g, e);
        put(g, ", 1");
    }
}

static void link_end_py(Gen *g, const Expr *e) {
    if (e->type->kind == TY_ARRAY) {
        expr(g, e, P_POSTFIX);
        put(g, "[0], len(");
        expr(g, e, P_LOWEST);
        put(g, ")");
    } else {
        component_ref(g, e);
        put(g, ", 1");
    }
}

/* Everything as at the beginning: each test starts from there. */
static void link_end_rust(Gen *g, const Expr *e) {
    if (e->type->kind == TY_ARRAY) {
        expr(g, e, P_POSTFIX);
        put(g, "[0].sim_id, ");
        expr(g, e, P_POSTFIX);
        put(g, ".len() as i32");
    } else {
        component_ref(g, e);
        put(g, ", 1");
    }
}

static void rust_system(Gen *g) {
    const Program *p = g->program;
    long long seed = 1;
    if (p->seed) const_int(p->seed, &seed);
    put(g, "// Everything as at the beginning: each test starts from there.\nunsafe fn reset() {\n");
    if (p->globals) put(g, "    G = Box::into_raw(Box::new(Globals::new()));\n");
    putf(g, "    sim_new();\n    SEED.with(|s| s.set(%lld));\n", seed);
    for (const VarDecl *v = p->globals; v; v = v->next) {
        const Symbol *sym = v->symbol;
        if (!is_component_type(sym->type)) continue;
        const char *name = safe(g, sym->c_name);
        if (sym->type->kind != TY_ARRAY) {
            putf(g, "    sim_add(std::ptr::addr_of_mut!((*G).%s), String::from(\"%s\"));\n", name, sym->name);
            continue;
        }
        long long lo = g->config->index_base;
        if (sym->type->dims.items[0].lo) const_int(sym->type->dims.items[0].lo, &lo);
        putf(g, "    for i in 0..(*G).%s.len() {\n        sim_add(std::ptr::addr_of_mut!((*G).%s[i]), format!(\"%s[{}]\", i as i32 + %lld));\n    }\n",
             name, name, sym->name, lo);
    }
    g->indent = 1;
    for (const Link *l = p->links; l; l = l->next) {
        indent(g);
        put(g, "sim_link(");
        link_end_rust(g, l->from);
        put(g, ", ");
        link_end_rust(g, l->to);
        put(g, l->both_ways ? ", true, " : ", false, ");
        if (l->delay) value(g, l->delay, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "1");
        put(g, ", ");
        if (l->delay_max) value(g, l->delay_max, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "-1");
        put(g, ", ");
        if (l->loss) value(g, l->loss, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "0");
        put(g, ");\n");
    }
    g->indent = 0;
    put(g, "    sim_start();\n}\n\n");
    int count = 0;
    for (const Function *f = p->functions; f; f = f->next) count += f->is_test;
    if (count) {
        put(g, "unsafe fn run_tests() {\n    let tests: Vec<(&str, unsafe fn())> = vec![\n");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "        (\"%s\", %s as unsafe fn()),\n", f->name, function_name(g, f));
        put(g, "    ];\n    let mut failed = 0;\n    for (title, body) in tests.iter() {\n        reset();\n        body();\n"
               "        let ok = (*SIM).checks.iter().all(|c| c.0);\n"
               "        println!(\"{} {}\", if ok { \"✓\" } else { \"✗\" }, title);\n"
               "        if !ok {\n            failed += 1;\n        }\n"
               "        for c in (*SIM).checks.iter() {\n            if !c.0 {\n"
               "                println!(\"    ✗ line {}: {}\", c.1, c.2);\n            }\n        }\n    }\n");
        putf(g, "    println!(\"{} tests, {} failed\", %d, failed);\n    std::process::exit(if failed > 0 { 1 } else { 0 });\n}\n\n", count);
    }
    bool run = has_main_function(p), components = has_components(p);
    put(g, "fn main() {\n    unsafe {\n");
    if (count && (run || components)) put(g, "        if std::env::args().nth(1).as_deref() == Some(\"test\") {\n            run_tests();\n        }\n");
    else if (count) put(g, "        run_tests();\n");
    put(g, "        reset();\n");
    if (run) put(g, "        run();\n");
    if (components) put(g, "        sim_run(-1);\n");
    put(g, "    }\n}\n");
}

static void reset_function(Gen *g) {
    const Program *p = g->program;
    long long seed = 1;
    if (p->seed) const_int(p->seed, &seed);
    put(g, CPP ? "void reset() {\n" : JAVA ? "static void reset() {\n" : PY ? "def reset():\n" : TS ? "function reset(): void {\n" : "function reset() {\n");
    g->indent = 1;
    if (PY) {
        put(g, "    global sim, _seed");
        for (const VarDecl *v = p->globals; v; v = v->next) putf(g, ", %s", safe(g, v->symbol->c_name));
        put(g, "\n");
    }
    for (const VarDecl *v = p->globals; v; v = v->next) {
        indent(g);
        putf(g, "%s = ", safe(g, v->symbol->c_name));
        zero(g, v->symbol->type, 0);
        end(g);
    }
    indent(g);
    put(g, CPP ? "sim = Sim()" : PY ? "sim = Sim()" : "sim = new Sim()");
    end(g);
    indent(g);
    putf(g, CPP ? "seed_ = %lldu" : PY ? "_seed = %lld" : "seed = %lld", seed);
    end(g);
    for (const VarDecl *v = p->globals; v; v = v->next) {
        const Symbol *sym = v->symbol;
        if (!is_component_type(sym->type)) continue;
        const char *name = safe(g, sym->c_name);
        indent(g);
        if (sym->type->kind != TY_ARRAY) {
            putf(g, CPP ? "sim.add(&%s, \"%s\")" : "sim.add(%s, \"%s\")", name, sym->name);
            end(g);
            continue;
        }
        long long lo = g->config->index_base;
        if (sym->type->dims.items[0].lo) const_int(sym->type->dims.items[0].lo, &lo);
        if (CPP) putf(g, "for (int i = 0; i < (int)%s.size(); i++) sim.add(&%s[i], \"%s[\" + std::to_string(i + %lld) + \"]\");\n", name, name, sym->name, lo);
        else if (JAVA) putf(g, "for (int i = 0; i < %s.length; i++) sim.add(%s[i], \"%s[\" + (i + %lld) + \"]\");\n", name, name, sym->name, lo);
        else if (PY) putf(g, "for i, c in enumerate(%s):\n        sim.add(c, f\"%s[{i + %lld}]\")\n", name, sym->name, lo);
        else putf(g, "%s.forEach((c, i) => sim.add(c, `%s[${i + %lld}]`));\n", name, sym->name, lo);
    }
    for (const Link *l = p->links; l; l = l->next) {
        indent(g);
        put(g, "sim.link(");
        if (PY) link_end_py(g, l->from);
        else link_end(g, l->from);
        put(g, ", ");
        if (PY) link_end_py(g, l->to);
        else link_end(g, l->to);
        put(g, l->both_ways ? (PY ? ", True, " : ", true, ") : (PY ? ", False, " : ", false, "));
        if (l->delay) value(g, l->delay, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "1");
        put(g, ", ");
        if (l->delay_max) value(g, l->delay_max, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "-1");
        put(g, ", ");
        if (l->loss) value(g, l->loss, &(Type){.kind = TY_INT}, P_LOWEST);
        else put(g, "0");
        put(g, ")");
        end(g);
    }
    indent(g);
    put(g, "sim.start()");
    end(g);
    g->indent = 0;
    put(g, PY ? "\n\n" : "}\n\n");
}

static void tests_function(Gen *g) {
    const Program *p = g->program;
    int count = 0;
    for (const Function *f = p->functions; f; f = f->next) count += f->is_test;
    if (CPP) {
        put(g, "int run_tests() {\n    struct {\n        const char *name;\n        void (*run)();\n    } tests[] = {\n");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "        {\"%s\", %s},\n", f->name, function_name(g, f));
        put(g, "    };\n    int failed = 0;\n    for (auto &test : tests) {\n        reset();\n        test.run();\n"
               "        bool ok = true;\n        for (const Check &c : sim.checks) ok = ok && c.ok;\n"
               "        std::cout << (ok ? \"✓ \" : \"✗ \") << test.name << \"\\n\";\n        if (!ok) failed++;\n"
               "        for (const Check &c : sim.checks)\n"
               "            if (!c.ok) std::cout << \"    ✗ line \" << c.line << \": \" << c.text << \"\\n\";\n    }\n");
        putf(g, "    std::cout << %d << \" tests, \" << failed << \" failed\\n\";\n    return failed ? 1 : 0;\n}\n\n", count);
    } else if (JAVA) {
        put(g, "static void runTests() {\n    String[] names = {");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "\"%s\", ", f->name);
        put(g, "};\n    Runnable[] tests = {");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "%s::%s, ", g->main_class, function_name(g, f));
        put(g, "};\n    int failed = 0;\n    for (int i = 0; i < tests.length; i++) {\n        reset();\n        tests[i].run();\n"
               "        boolean ok = true;\n        for (Check c : sim.checks) ok &= c.ok();\n"
               "        System.out.println((ok ? \"✓ \" : \"✗ \") + names[i]);\n        if (!ok) failed++;\n"
               "        for (Check c : sim.checks)\n"
               "            if (!c.ok()) System.out.println(\"    ✗ line \" + c.line() + \": \" + c.text());\n    }\n"
               "    System.out.println(tests.length + \" tests, \" + failed + \" failed\");\n"
               "    System.exit(failed > 0 ? 1 : 0);\n}\n\n");
    } else if (PY) {
        put(g, "def run_tests():\n    tests = [\n");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "        (\"%s\", %s),\n", f->name, function_name(g, f));
        put(g, "    ]\n    failed = 0\n    for title, body in tests:\n        reset()\n        body()\n"
               "        ok = all(c[0] for c in sim.checks)\n        print((\"✓ \" if ok else \"✗ \") + title)\n"
               "        failed += not ok\n        for good, line, text in sim.checks:\n            if not good:\n"
               "                print(f\"    ✗ line {line}: {text}\")\n"
               "    print(f\"{len(tests)} tests, {failed} failed\")\n    sys.exit(1 if failed else 0)\n\n\n");
    } else {
        putf(g, "function runTests()%s {\n    const tests%s = [\n", TS ? ": void" : "", TS ? ": [string, () => void][]" : "");
        for (const Function *f = p->functions; f; f = f->next)
            if (f->is_test) putf(g, "        [\"%s\", %s],\n", f->name, function_name(g, f));
        put(g, "    ];\n    let failed = 0;\n    for (const [name, test] of tests) {\n        reset();\n        test();\n"
               "        const ok = sim.checks.every((c) => c.ok);\n        console.log((ok ? \"✓ \" : \"✗ \") + name);\n"
               "        if (!ok) failed++;\n        for (const c of sim.checks) {\n"
               "            if (!c.ok) console.log(`    ✗ line ${c.line}: ${c.text}`);\n        }\n    }\n"
               "    console.log(`${tests.length} tests, ${failed} failed`);\n    process.exit(failed > 0 ? 1 : 0);\n}\n\n");
    }
}

/* part 0: before the classes; part 1: after the subprograms. */
static void simulation(Gen *g, int part) {
    const Program *p = g->program;
    if (!can_simulate(g)) return;
    g->needs |= N_SIM;
    if (!JAVA) g->needs |= N_PROCESS;
    if (part == 0) return;
    if (RUST) {
        rust_system(g);
        return;
    }
    bool tests = has_tests(p), main_too = has_main_function(p), components = has_components(p);
    if (CPP) { /* how each component takes its messages */
        for (const Class *c = p->classes; c; c = c->next) {
            if (!c->is_component) continue;
            putf(g, "bool %s::receive(const std::string &name, const std::vector<std::any> &args) {\n", class_name(g, c));
            g->indent = 1;
            const Function *before = g->function;
            for (const Function *f = p->functions; f; f = f->next)
                if (f->owner == c) g->function = f; /* inside the class: its states by their name */
            receive_body(g, c);
            g->function = before;
            g->indent = 0;
            put(g, "}\n\n");
        }
    }
    reset_function(g);
    if (tests) tests_function(g);
    const char *run = main_too ? (CPP || JAVA ? "run()" : "main()") : NULL;
    if (CPP) {
        put(g, "int main(int argc, char **argv) {\n");
        if (g->needs & N_FIXED) put(g, "    std::cout << std::fixed;\n");
        if (tests) putf(g, "    if (%sargc > 1 && std::string(argv[1]) == \"test\") return run_tests();\n", run || components ? "" : "true || ");
        put(g, "    reset();\n");
        if (run) putf(g, "    %s;\n", run);
        if (components) put(g, "    sim.run();\n");
        put(g, "    (void)argc;\n    (void)argv;\n    return 0;\n}\n");
    } else if (JAVA) {
        put(g, "public static void main(String[] args) {\n"
               "    System.setOut(new java.io.PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), true, java.nio.charset.StandardCharsets.UTF_8));\n"
               "    java.util.Locale.setDefault(java.util.Locale.ROOT);\n");
        if (tests) putf(g, "    if (%sargs.length > 0 && args[0].equals(\"test\")) runTests();\n", run || components ? "" : "true || ");
        put(g, "    reset();\n");
        if (run) putf(g, "    %s;\n", run);
        if (components) put(g, "    sim.run(-1);\n");
        put(g, "}\n");
    } else if (PY) {
        put(g, "if __name__ == \"__main__\":\n");
        if (tests && (run || components)) put(g, "    if sys.argv[1:] == [\"test\"]:\n        run_tests()\n");
        else if (tests) put(g, "    run_tests()\n");
        put(g, "    reset()\n");
        if (run) putf(g, "    %s\n", run);
        if (components) put(g, "    sim.run()\n");
    } else {
        if (tests && (run || components)) put(g, "if (process.argv[2] === \"test\") runTests();\n");
        else if (tests) put(g, "runTests();\n");
        put(g, "reset();\n");
        if (run) putf(g, "%s;\n", run);
        if (components) put(g, "sim.run();\n");
    }
}
