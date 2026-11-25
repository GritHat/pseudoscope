/* Part of parser.c: what the other dialects write differently. */

static Stmt *portugol_statement(Parser *p);

static TypeSpec *simple_spec(Parser *p, TokenKind base, Loc loc) {
    TypeSpec *spec = ARENA_NEW(p->arena, TypeSpec);
    spec->loc = loc;
    spec->base = base;
    return spec;
}

static bool is_type_word(const Parser *p) {
    return check(p, TOK_INTEGER) || check(p, TOK_REAL) || check(p, TOK_LOGICAL) || check(p, TOK_STRINGTYPE);
}

static VarDecl *find_decl(VarDecl *list, const char *name) {
    for (; list; list = list->next)
        if (strcmp(list->name, name) == 0) return list;
    return NULL;
}

static void append_decl(VarDecl **list, VarDecl *decl) {
    while (*list) list = &(*list)->next;
    decl->next = NULL;
    *list = decl;
}

/* The variables declared by the code being read belong to this list. */
static VarDecl **scope_of(Parser *p) { return p->function ? &p->function->locals : &p->program->globals; }

static VarDecl *new_decl(Parser *p, const Token *name) {
    VarDecl *decl = ARENA_NEW(p->arena, VarDecl);
    decl->loc = name->loc;
    decl->name = name->text;
    return decl;
}

/* Definir a, b Como Entero */
static void pseint_define(Parser *p) {
    const Token *names[64];
    int count = 0;
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a variable name");
            return;
        }
        if (count < 64) names[count++] = cur(p);
        advance(p);
    } while (match(p, TOK_COMMA));
    if (!expect(p, TOK_AS, "':' and a type")) return;
    if (!is_type_word(p)) {
        syntax_error(p, "a type");
        return;
    }
    TypeSpec *spec = simple_spec(p, cur(p)->kind, cur(p)->loc);
    advance(p);
    for (int i = 0; i < count; i++) {
        VarDecl *param = p->function ? find_decl(p->function->params, names[i]->text) : NULL;
        VarDecl *known = find_decl(*scope_of(p), names[i]->text);
        for (Dimensioned *d = p->dimensioned; d && !known; d = d->next)
            if (strcmp(d->decl->name, names[i]->text) == 0) known = d->decl;
        if (param) param->spec = spec;
        else if (known) known->spec = spec;
        else {
            VarDecl *decl = new_decl(p, names[i]);
            decl->spec = spec;
            append_decl(scope_of(p), decl);
        }
    }
}

/* Dimension A[10], B[n, m]: it happens where it is written. */
static Stmt *pseint_dimension(Parser *p) {
    Stmt *stmt = new_stmt(p, ST_VAR);
    VarDecl **tail = &stmt->vars;
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a variable name");
            return NULL;
        }
        VarDecl *decl = new_decl(p, advance(p));
        decl->has_dims = true;
        if (!check(p, TOK_LBRACKET)) {
            syntax_error(p, "'['");
            return NULL;
        }
        if (!parse_dims(p, &decl->dims)) return NULL;
        for (VarDecl **link = scope_of(p); *link; link = &(*link)->next) { /* Definir came first */
            if (strcmp((*link)->name, decl->name) != 0) continue;
            decl->spec = (*link)->spec;
            *link = (*link)->next;
            break;
        }
        Dimensioned *d = ARENA_NEW(p->arena, Dimensioned); /* so that a later Definir finds it */
        d->decl = decl;
        d->next = p->dimensioned;
        p->dimensioned = d;
        *tail = decl;
        tail = &decl->next;
    } while (match(p, TOK_COMMA));
    return stmt;
}

/* 1, 2: ... Otherwise: ... as a chain of ifs. */
static bool is_case_label(const Parser *p) {
    if (p->config->dialect == DIALECT_VISUALG || p->config->dialect == DIALECT_PORTUGOL) return check(p, TOK_CASE);
    if (!cur(p)->starts_line) return false;
    for (int i = 0; i < 40; i++) {
        const Token *t = peek(p, i);
        if (i > 0 && t->starts_line) return false;
        if (t->kind == TOK_COLON) return i > 0;
        if (t->kind != TOK_INT && t->kind != TOK_FLOAT && t->kind != TOK_STRING && t->kind != TOK_COMMA &&
            t->kind != TOK_MINUS && t->kind != TOK_TRUE && t->kind != TOK_FALSE && t->kind != TOK_TO)
            return false;
    }
    return false;
}

static Expr *binary_expr(Parser *p, TokenKind op, Loc loc, Expr *left, Expr *right) {
    Expr *e = new_expr(p, EX_BINARY, loc, left, right);
    e->op = op;
    return e;
}

static Stmt *foreign_switch(Parser *p) {
    Stmt *first = NULL, **link = &first;
    Dialect dialect = p->config->dialect;
    Loc loc = cur(p)->loc;
    advance(p);
    Expr *subject = parse_expr(p);
    if (!subject) return NULL;
    match(p, TOK_DO);
    if (dialect == DIALECT_PORTUGOL && !expect(p, TOK_LBRACE, "'{'")) return NULL;
    p->switches++;
    while (!p->panic) {
        while (match(p, TOK_SEMICOLON)) {}
        if (check(p, TOK_OTHERWISE)) {
            Stmt *block = new_stmt(p, ST_BLOCK);
            advance(p);
            match(p, TOK_COLON);
            block->body = parse_statements(p);
            if (dialect == DIALECT_PORTUGOL) match(p, TOK_ENDSWITCH);
            *link = block;
            if (!first) first = block;
            break;
        }
        if (!is_case_label(p)) break;
        match(p, TOK_CASE);
        Stmt *branch = new_stmt(p, ST_IF);
        do {
            Expr *value = parse_expr(p);
            if (!value) break;
            Expr *test;
            if (match(p, TOK_TO) || match(p, TOK_DOTDOT)) { /* 1 TO 5 */
                Expr *hi = parse_expr(p);
                if (!hi) break;
                test = binary_expr(p, TOK_AND, value->loc, binary_expr(p, TOK_GE, value->loc, subject, value),
                                   binary_expr(p, TOK_LE, value->loc, subject, hi));
            } else {
                test = binary_expr(p, TOK_EQ, value->loc, subject, value);
            }
            branch->value = branch->value ? binary_expr(p, TOK_OR, value->loc, branch->value, test) : test;
        } while (match(p, TOK_COMMA));
        if (!branch->value) break;
        match(p, TOK_COLON);
        branch->body = parse_statements(p);
        if (dialect == DIALECT_PORTUGOL) match(p, TOK_ENDSWITCH); /* pare */
        *link = branch;
        link = &branch->otherwise;
    }
    p->switches--;
    if (p->panic) return NULL;
    if (dialect == DIALECT_PORTUGOL) {
        while (match(p, TOK_SEMICOLON)) {}
        if (!expect(p, TOK_RBRACE, "'}'")) return NULL;
    } else if (!expect(p, TOK_ENDSWITCH, "the end of the cases")) {
        return NULL;
    }
    if (!first) {
        first = ARENA_NEW(p->arena, Stmt);
        first->kind = ST_BLOCK;
        first->loc = loc;
    }
    return first;
}

/* DECLARE a, b : INTEGER */
static void cambridge_declare(Parser *p, VarDecl **list) {
    VarDecl *first = NULL, **tail = &first;
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a variable name");
            return;
        }
        VarDecl *decl = new_decl(p, advance(p));
        *tail = decl;
        tail = &decl->next;
    } while (match(p, TOK_COMMA));
    if (!expect(p, TOK_COLON, "':' and a type")) return;
    TypeSpec *spec = parse_type(p);
    if (!spec) return;
    for (VarDecl *d = first; d;) {
        VarDecl *next = d->next;
        d->spec = spec;
        append_decl(list, d);
        d = next;
    }
}

static void foreign_constant(Parser *p) {
    if (p->config->dialect == DIALECT_PORTUGOL && is_type_word(p)) advance(p);
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, "a variable name");
        return;
    }
    Constant *c = ARENA_NEW(p->arena, Constant);
    c->name = advance(p)->text;
    if (!match(p, TOK_EQ) && !match(p, TOK_ASSIGN)) {
        syntax_error(p, "'='");
        return;
    }
    c->value = parse_expr(p);
    c->next = p->constants;
    p->constants = c;
}

/* Statements that only some dialects have. */
static Stmt *foreign_statement(Parser *p, bool *handled) {
    Stmt *stmt;
    *handled = true;
    if (p->config->dialect == DIALECT_PORTUGOL) return portugol_statement(p);
    if (p->config->cormen && check(p, TOK_IDENT) && strcmp(cur(p)->text, "todo_") == 0) { /* a line in words: left to do */
        stmt = new_stmt(p, ST_TODO);
        advance(p);
        return stmt;
    }
    switch (cur(p)->kind) {
        case TOK_DECLARE:
            advance(p);
            if (p->config->dialect == DIALECT_PSEINT) pseint_define(p);
            else cambridge_declare(p, scope_of(p));
            stmt = new_stmt(p, ST_BLOCK);
            stmt->name = "declaration";
            return p->panic ? NULL : stmt;
        case TOK_DIMENSION:
            advance(p);
            return pseint_dimension(p);
        case TOK_CONST:
            advance(p);
            foreign_constant(p);
            stmt = new_stmt(p, ST_BLOCK);
            return p->panic ? NULL : stmt;
        case TOK_SWITCH:
            return foreign_switch(p);
        case TOK_IGNORE:
            advance(p);
            return new_stmt(p, ST_BLOCK);
        case TOK_CALL:
            advance(p);
            return parse_simple(p);
        case TOK_PRINT: case TOK_PRINTLN: {
            bool line = check(p, TOK_PRINTLN) || p->config->dialect != DIALECT_VISUALG;
            stmt = parse_io(p, ST_PRINT);
            if (!stmt) return NULL;
            stmt->joined = true;
            stmt->same_line = !line;
            if (strstr(stmt->text ? stmt->text : "", "altar")) stmt->same_line = true;
            if (match(p, TOK_NOLINE)) stmt->same_line = true;
            return stmt;
        }
        default:
            *handled = false;
            return NULL;
    }
}

static Function *new_function(Parser *p, const Token *name, bool is_procedure) {
    Function *f = ARENA_NEW(p->arena, Function);
    f->loc = name->loc;
    f->name = f->full_name = name->text;
    f->is_procedure = is_procedure;
    *p->functions_tail = f;
    p->functions_tail = &f->next;
    p->function = f;
    p->dimensioned = NULL;
    return f;
}

static Stmt *return_of(Parser *p, const char *name, Loc loc) {
    Stmt *stmt = ARENA_NEW(p->arena, Stmt);
    stmt->kind = ST_RETURN;
    stmt->loc = loc;
    stmt->value = new_expr(p, EX_NAME, loc, NULL, NULL);
    stmt->value->text = name;
    return stmt;
}

static void append_stmt(Stmt **list, Stmt *stmt) {
    while (*list) list = &(*list)->next;
    *list = stmt;
}

/* Funcion r <- nombre(a, b Por Referencia) ... FinFuncion */
static void pseint_function(Parser *p) {
    advance(p);
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, "a function name");
        return;
    }
    const Token *first = advance(p), *result = NULL;
    if (match(p, TOK_ASSIGN) || match(p, TOK_EQ)) {
        result = first;
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a function name");
            return;
        }
        first = advance(p);
    }
    Function *f = new_function(p, first, result == NULL);
    f->result_name = result ? result->text : NULL;
    if (match(p, TOK_LPAREN)) {
        while (!check(p, TOK_RPAREN) && !p->panic) {
            if (!check(p, TOK_IDENT)) {
                syntax_error(p, "a parameter name");
                return;
            }
            VarDecl *param = new_decl(p, advance(p));
            if (match(p, TOK_BYREF)) param->mode = MODE_INOUT;
            else match(p, TOK_IGNORE);
            append_decl(&f->params, param);
            if (!match(p, TOK_COMMA)) break;
        }
        if (!expect(p, TOK_RPAREN, "')' after the parameters")) return;
    }
    f->body = parse_statements(p);
    if (result) append_stmt(&f->body, return_of(p, result->text, cur(p)->loc));
    f->last_line = cur(p)->loc.line;
    expect(p, TOK_ENDFUNCTION, "'endfunction'");
    p->function = NULL;
}

/* VisuAlg and Cambridge: name(a, b: tipo; var c: tipo) */
static void typed_function(Parser *p) {
    bool is_procedure = advance(p)->kind == TOK_PROCEDURE, cambridge = p->config->dialect == DIALECT_CAMBRIDGE;
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, is_procedure ? "a procedure name" : "a function name");
        return;
    }
    Function *f = new_function(p, advance(p), is_procedure);
    if (match(p, TOK_LPAREN)) {
        ParamMode mode = MODE_IN;
        while (!check(p, TOK_RPAREN) && !p->panic) {
            if (match(p, TOK_VAR) || match(p, TOK_BYREF)) mode = MODE_INOUT;
            else if (match(p, TOK_IGNORE) || !cambridge) mode = MODE_IN; /* BYREF goes on in Cambridge */
            VarDecl *group[32];
            int count = 0;
            do {
                if (!check(p, TOK_IDENT)) {
                    syntax_error(p, "a parameter name");
                    return;
                }
                VarDecl *param = new_decl(p, advance(p));
                param->mode = mode;
                append_decl(&f->params, param);
                if (count < 32) group[count++] = param;
            } while (match(p, TOK_COMMA));
            if (!expect(p, TOK_COLON, "':' and a type")) return;
            TypeSpec *spec = parse_type(p);
            for (int i = 0; spec && i < count; i++) group[i]->spec = spec;
            if (!match(p, TOK_SEMICOLON) && !match(p, TOK_COMMA)) break;
        }
        if (p->panic || !expect(p, TOK_RPAREN, "')' after the parameters")) return;
    }
    if (!is_procedure && (match(p, TOK_COLON) || match(p, TOK_RETURNS))) f->result = parse_type(p);
    if (p->panic) return;
    if (match(p, TOK_VAR)) {
        VarDecl **tail = &f->locals;
        while (check(p, TOK_IDENT) && !p->panic) tail = parse_var_group(p, tail);
    }
    match(p, TOK_BEGIN);
    f->body = parse_statements(p);
    f->last_line = cur(p)->loc.line;
    expect(p, is_procedure ? TOK_ENDPROCEDURE : TOK_ENDFUNCTION, is_procedure ? "'endprocedure'" : "'endfunction'");
    p->function = NULL;
}

static Function *make_main(Parser *p, Loc loc) {
    Token name = {TOK_IDENT, loc, "main", true, 4};
    return new_function(p, &name, true);
}

static void cambridge_type(Parser *p) {
    advance(p);
    if (!check(p, TOK_IDENT)) {
        syntax_error(p, "a type name");
        return;
    }
    TypeDecl *decl = ARENA_NEW(p->arena, TypeDecl);
    decl->loc = cur(p)->loc;
    decl->name = advance(p)->text;
    decl->record = ARENA_NEW(p->arena, Record);
    decl->record->loc = decl->loc;
    decl->record->name = decl->name;
    while (match(p, TOK_DECLARE) && !p->panic) cambridge_declare(p, &decl->record->fields);
    if (p->panic || !expect(p, TOK_ENDTYPE, "'end' after the record fields")) return;
    *p->types_tail = decl;
    p->types_tail = &decl->next;
}

/* ---- Portugol Studio: braces and C-like statements ---- */

static Stmt *portugol_block(Parser *p) {
    if (match(p, TOK_LBRACE)) {
        Stmt *body = parse_statements(p);
        expect(p, TOK_RBRACE, "'}'");
        return body;
    }
    Stmt *one = parse_statement(p);
    match(p, TOK_SEMICOLON);
    return one;
}

static Stmt *assign_stmt(Parser *p, Loc loc, Expr *target, Expr *value) {
    Stmt *stmt = new_stmt(p, ST_ASSIGN);
    stmt->loc = loc;
    stmt->target = target;
    stmt->value = value;
    return stmt;
}

static Expr *int_expr(Parser *p, Loc loc, const char *text) {
    Expr *e = new_expr(p, EX_INT, loc, NULL, NULL);
    e->text = text;
    e->op = TOK_INT;
    return e;
}

/* x = e, x++, x += e, f(x) */
static Stmt *portugol_simple(Parser *p) {
    Loc loc = cur(p)->loc;
    Expr *e = parse_postfix(p);
    if (!e) return NULL;
    if (match(p, TOK_ASSIGN)) {
        Expr *value = parse_expr(p);
        return value ? assign_stmt(p, loc, e, value) : NULL;
    }
    if (check(p, TOK_INCREMENT) || check(p, TOK_DECREMENT)) {
        TokenKind op = advance(p)->kind == TOK_INCREMENT ? TOK_PLUS : TOK_MINUS;
        return assign_stmt(p, loc, e, binary_expr(p, op, loc, e, int_expr(p, loc, "1")));
    }
    if (check(p, TOK_ADD_ASSIGN) || check(p, TOK_SUB_ASSIGN)) {
        TokenKind op = advance(p)->kind == TOK_ADD_ASSIGN ? TOK_PLUS : TOK_MINUS;
        Expr *value = parse_expr(p);
        return value ? assign_stmt(p, loc, e, binary_expr(p, op, loc, e, value)) : NULL;
    }
    if (e->kind != EX_APPLY && e->kind != EX_NAME) {
        syntax_error(p, "an assignment");
        return NULL;
    }
    Stmt *stmt = new_stmt(p, ST_CALL);
    stmt->loc = loc;
    stmt->value = e;
    return stmt;
}

/* inteiro a, b = 2, v[10], w[] = {1, 2}: a chain of declarations and assignments */
static Stmt *portugol_declaration(Parser *p) {
    TypeSpec *spec = simple_spec(p, cur(p)->kind, cur(p)->loc);
    Stmt *first = NULL;
    advance(p);
    do {
        if (!check(p, TOK_IDENT)) {
            syntax_error(p, "a variable name");
            return NULL;
        }
        Stmt *stmt = new_stmt(p, ST_VAR);
        VarDecl *decl = new_decl(p, advance(p));
        decl->spec = spec;
        decl->has_dims = check(p, TOK_LBRACKET);
        if (!parse_dims(p, &decl->dims)) return NULL;
        stmt->vars = decl;
        append_stmt(&first, stmt);
        if (!match(p, TOK_ASSIGN)) continue;
        if (match(p, TOK_LBRACE)) {
            int n = 0;
            do {
                Expr *value = parse_expr(p);
                if (!value) return NULL;
                Expr *name = new_expr(p, EX_NAME, decl->loc, NULL, NULL);
                name->text = decl->name;
                Expr *place = new_expr(p, EX_INDEX, decl->loc, name, NULL);
                list_add(p, &place->args, int_expr(p, decl->loc, arena_printf(p->arena, "%d", n++)));
                append_stmt(&first, assign_stmt(p, decl->loc, place, value));
            } while (match(p, TOK_COMMA));
            if (!expect(p, TOK_RBRACE, "'}'")) return NULL;
            if (decl->dims.count == 1 && !decl->dims.items[0].hi)
                decl->dims.items[0].hi = int_expr(p, decl->loc, arena_printf(p->arena, "%d", n));
        } else {
            Expr *value = parse_expr(p);
            if (!value) return NULL;
            Expr *name = new_expr(p, EX_NAME, decl->loc, NULL, NULL);
            name->text = decl->name;
            append_stmt(&first, assign_stmt(p, decl->loc, name, value));
        }
    } while (match(p, TOK_COMMA));
    return first;
}

static bool same_name(const Expr *a, const Expr *b) {
    return a->kind == EX_NAME && b->kind == EX_NAME && strcmp(a->text, b->text) == 0;
}

/* para (i = 0; i < n; i++): a counting loop when it looks like one */
static Stmt *portugol_for(Parser *p) {
    Loc loc = cur(p)->loc;
    advance(p);
    if (!expect(p, TOK_LPAREN, "'('")) return NULL;
    Stmt *block = new_stmt(p, ST_BLOCK), *start = NULL;
    if (is_type_word(p)) start = portugol_declaration(p);
    else if (!check(p, TOK_SEMICOLON)) start = portugol_simple(p);
    if (p->panic || !expect(p, TOK_SEMICOLON, "';'")) return NULL;
    Expr *test = parse_expr(p);
    if (!test || !expect(p, TOK_SEMICOLON, "';'")) return NULL;
    Stmt *step = portugol_simple(p);
    if (!step || !expect(p, TOK_RPAREN, "')'")) return NULL;
    Stmt *body = portugol_block(p);
    if (p->panic) return NULL;

    Stmt *init = start;
    while (init && init->next) init = init->next;
    bool counts = init && init->kind == ST_ASSIGN && step->kind == ST_ASSIGN && test->kind == EX_BINARY &&
                  same_name(init->target, step->target) && same_name(test->base, step->target) &&
                  step->value->kind == EX_BINARY && same_name(step->value->base, step->target) &&
                  step->value->right->kind == EX_INT;
    bool up = counts && step->value->op == TOK_PLUS && (test->op == TOK_LT || test->op == TOK_LE);
    bool down = counts && step->value->op == TOK_MINUS && (test->op == TOK_GT || test->op == TOK_GE);
    Stmt *loop = new_stmt(p, up || down ? ST_FOR : ST_WHILE);
    loop->loc = loc;
    if (up || down) {
        loop->target = init->target;
        loop->value = init->value;
        loop->limit = test->right;
        if (test->op == TOK_LT || test->op == TOK_GT) {
            bool number = test->right->kind == EX_INT;
            long n = number ? strtol(test->right->text, NULL, 10) + (up ? -1 : 1) : 0;
            loop->limit = number ? int_expr(p, test->loc, arena_printf(p->arena, "%ld", n < 0 ? 0 : n))
                                 : binary_expr(p, up ? TOK_MINUS : TOK_PLUS, test->loc, test->right, int_expr(p, test->loc, "1"));
            if (number && n < 0) {
                Expr *minus = new_expr(p, EX_UNARY, test->loc, int_expr(p, test->loc, "1"), NULL);
                minus->op = TOK_MINUS;
                loop->limit = minus;
            }
        }
        if (down) {
            loop->step = new_expr(p, EX_UNARY, step->loc, step->value->right, NULL);
            loop->step->op = TOK_MINUS;
        } else if (strcmp(step->value->right->text, "1") != 0) {
            loop->step = step->value->right;
        }
        loop->body = body;
        if (init == start) return loop;
        Stmt *before = start; /* para (inteiro i = 0; ...) */
        while (before->next != init) before = before->next;
        before->next = loop;
        block->body = start;
        return block;
    }
    append_stmt(&body, step);
    loop->value = test;
    loop->body = body;
    append_stmt(&start, loop);
    block->body = start;
    return block;
}

static Stmt *portugol_statement(Parser *p) {
    Stmt *stmt;
    switch (cur(p)->kind) {
        case TOK_INTEGER: case TOK_REAL: case TOK_LOGICAL: case TOK_STRINGTYPE: return portugol_declaration(p);
        case TOK_CONST:
            advance(p);
            foreign_constant(p);
            stmt = new_stmt(p, ST_BLOCK);
            return p->panic ? NULL : stmt;
        case TOK_IF: {
            stmt = new_stmt(p, ST_IF);
            advance(p);
            if (!expect(p, TOK_LPAREN, "'('")) return NULL;
            stmt->value = parse_expr(p);
            if (!stmt->value || !expect(p, TOK_RPAREN, "')'")) return NULL;
            stmt->body = portugol_block(p);
            if (!p->panic && check(p, TOK_ELSE)) {
                advance(p);
                if (check(p, TOK_IF)) {
                    stmt->otherwise = portugol_statement(p);
                } else {
                    stmt->otherwise = new_stmt(p, ST_BLOCK);
                    stmt->otherwise->body = portugol_block(p);
                }
            }
            return p->panic ? NULL : stmt;
        }
        case TOK_WHILE:
            stmt = new_stmt(p, ST_WHILE);
            advance(p);
            if (!expect(p, TOK_LPAREN, "'('")) return NULL;
            stmt->value = parse_expr(p);
            if (!stmt->value || !expect(p, TOK_RPAREN, "')'")) return NULL;
            stmt->body = portugol_block(p);
            return p->panic ? NULL : stmt;
        case TOK_DO: { /* faca { } enquanto (c) */
            stmt = new_stmt(p, ST_REPEAT);
            advance(p);
            stmt->body = portugol_block(p);
            if (p->panic || !expect(p, TOK_WHILE, "'while'") || !expect(p, TOK_LPAREN, "'('")) return NULL;
            Expr *test = parse_expr(p);
            if (!test || !expect(p, TOK_RPAREN, "')'")) return NULL;
            stmt->value = new_expr(p, EX_UNARY, test->loc, test, NULL);
            stmt->value->op = TOK_NOT;
            return stmt;
        }
        case TOK_FOR: return portugol_for(p);
        case TOK_SWITCH: return foreign_switch(p);
        case TOK_RETURN: return parse_return(p);
        case TOK_PRINT:
            stmt = parse_io(p, ST_PRINT);
            if (stmt) stmt->joined = stmt->same_line = true;
            return stmt;
        case TOK_READ: return parse_io(p, ST_READ);
        case TOK_IGNORE:
            advance(p);
            if (match(p, TOK_LPAREN)) expect(p, TOK_RPAREN, "')'");
            return new_stmt(p, ST_BLOCK);
        case TOK_LBRACE:
            stmt = new_stmt(p, ST_BLOCK);
            stmt->body = portugol_block(p);
            return p->panic ? NULL : stmt;
        case TOK_IDENT: return portugol_simple(p);
        default:
            syntax_error(p, "a statement");
            return NULL;
    }
}

static void portugol_program(Parser *p) {
    if (!expect(p, TOK_PROGRAM, "'programa'") || !expect(p, TOK_LBRACE, "'{'")) return;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF) && !p->diag->too_many) {
        if (match(p, TOK_SEMICOLON)) continue;
        if (match(p, TOK_CONST)) {
            foreign_constant(p);
        } else if (is_type_word(p)) { /* globals; their first values are set at the beginning */
            Stmt *list = portugol_declaration(p);
            for (Stmt *s = list; s;) {
                Stmt *next = s->next;
                s->next = NULL;
                if (s->kind == ST_VAR) append_decl(&p->program->globals, s->vars);
                else append_stmt(&p->initial, s);
                s = next;
            }
        } else if (match(p, TOK_FUNCTION)) {
            TypeSpec *result = NULL;
            if (is_type_word(p)) {
                result = simple_spec(p, cur(p)->kind, cur(p)->loc);
                advance(p);
            } else {
                match(p, TOK_IGNORE); /* vazio */
            }
            if (!check(p, TOK_IDENT)) {
                syntax_error(p, "a function name");
            } else {
                Token name = *advance(p);
                bool start = strcmp(name.text, "inicio") == 0;
                if (start) name.text = "main";
                Function *f = new_function(p, &name, result == NULL);
                f->result = result;
                if (expect(p, TOK_LPAREN, "'('")) {
                    while (!check(p, TOK_RPAREN) && !p->panic) {
                        if (!is_type_word(p)) {
                            syntax_error(p, "a type");
                            break;
                        }
                        TypeSpec *spec = simple_spec(p, cur(p)->kind, cur(p)->loc);
                        advance(p);
                        bool byref = match(p, TOK_BYREF);
                        if (!check(p, TOK_IDENT)) {
                            syntax_error(p, "a parameter name");
                            break;
                        }
                        VarDecl *param = new_decl(p, advance(p));
                        param->spec = spec;
                        param->mode = byref ? MODE_INOUT : MODE_IN;
                        param->has_dims = check(p, TOK_LBRACKET);
                        if (!parse_dims(p, &param->dims)) break;
                        append_decl(&f->params, param);
                        if (!match(p, TOK_COMMA)) break;
                    }
                    if (!p->panic && expect(p, TOK_RPAREN, "')' after the parameters") && expect(p, TOK_LBRACE, "'{'")) {
                        f->body = parse_statements(p);
                        f->last_line = cur(p)->loc.line;
                        expect(p, TOK_RBRACE, "'}'");
                    }
                }
                p->function = NULL;
            }
        } else {
            syntax_error(p, "a function name");
        }
        if (p->panic) {
            p->panic = false;
            advance(p);
            while (!check(p, TOK_EOF) && !check(p, TOK_FUNCTION)) advance(p);
        }
    }
    for (Function *f = p->program->functions; f; f = f->next) {
        if (strcmp(f->name, "main") != 0 || !p->initial) continue;
        Stmt *body = f->body;
        f->body = p->initial;
        append_stmt(&f->body, body);
    }
}

static void foreign_program(Parser *p) {
    Dialect dialect = p->config->dialect;
    if (dialect == DIALECT_PORTUGOL) {
        portugol_program(p);
        return;
    }
    Function *main_function = NULL;
    while (!check(p, TOK_EOF) && !p->diag->too_many) {
        if (match(p, TOK_SEMICOLON)) continue;
        if (check(p, TOK_FUNCTION) || check(p, TOK_PROCEDURE)) {
            if (dialect == DIALECT_PSEINT) pseint_function(p);
            else typed_function(p);
        } else if (check(p, TOK_PROGRAM)) {
            Loc loc = cur(p)->loc;
            advance(p);
            if (!match(p, TOK_IDENT)) match(p, TOK_STRING);
            if (dialect == DIALECT_PSEINT) {
                main_function = make_main(p, loc);
                main_function->body = parse_statements(p);
                main_function->last_line = cur(p)->loc.line;
                expect(p, TOK_ENDPROGRAM, "the end of the program");
                p->function = NULL;
            }
        } else if (dialect == DIALECT_VISUALG && check(p, TOK_VAR)) {
            advance(p);
            VarDecl **tail = &p->program->globals;
            while (*tail) tail = &(*tail)->next;
            while (check(p, TOK_IDENT) && !p->panic) tail = parse_var_group(p, tail);
        } else if (dialect == DIALECT_VISUALG && check(p, TOK_BEGIN)) {
            main_function = make_main(p, cur(p)->loc);
            advance(p);
            main_function->body = parse_statements(p);
            main_function->last_line = cur(p)->loc.line;
            expect(p, TOK_ENDPROGRAM, "the end of the program");
            p->function = NULL;
        } else if (dialect == DIALECT_CAMBRIDGE && check(p, TOK_TYPE)) {
            cambridge_type(p);
        } else if (dialect == DIALECT_CAMBRIDGE) { /* the main program is what is outside */
            int before = p->pos;
            Function *was = p->function;
            p->function = NULL;
            Stmt *list = parse_statements(p);
            p->function = was;
            if (!main_function) {
                Loc loc = p->tokens[before].loc;
                main_function = make_main(p, loc);
                p->function = NULL;
            }
            append_stmt(&main_function->body, list);
            main_function->last_line = p->tokens[p->pos > 0 ? p->pos - 1 : 0].loc.line;
            if (p->pos == before) syntax_error(p, "a statement");
        } else {
            syntax_error(p, "a declaration (function, procedure, var or type)");
        }
        if (p->panic) {
            p->panic = false;
            p->function = NULL;
            p->depth = p->brackets = p->switches = 0;
            advance(p);
            while (!check(p, TOK_EOF) && !check(p, TOK_FUNCTION) && !check(p, TOK_PROCEDURE) &&
                   !check(p, TOK_PROGRAM) && !check(p, TOK_BEGIN))
                advance(p);
        }
    }
}

/* ---- types that were not written ---- */

/* What a name holds. A collection also says what is in it, when that is known:
 * IN_SET alone, IN_SET + IN_INT, IN_SET + IN_OBJECT. */
enum { IN_UNKNOWN, IN_BOOL, IN_INT, IN_REAL, IN_TEXT, IN_OBJECT, IN_CELL, IN_SET = 8, IN_PAIRS = IN_SET + 6 /* of two objects: edges */ };

/* Objects: what has attributes (x.key, x.next) and can be nothing (nil_). No
 * pseudocode without declarations says what they are made of, so there is one
 * kind of object, with every attribute that is ever named; what an attribute
 * holds is worked out like the type of a variable. They are records of the
 * base language, taken one by one from a store: see add_ours.
 *
 * Collections: sets, lists, queues, and what is indexed by anything (x.c[i],
 * G.Adj[u], w[u, v]). They too are records of the base language: a chain of
 * cells, each with a key of one or two parts (whole numbers or objects) and,
 * where something is indexed, the value kept under that key. What is done
 * with them is written with names of ours (set_of_, in_, first_...) by who
 * rewrites the pseudocode, and turned here into calls of the procedures that
 * do it, written in the base language and added to the program. */
static struct {
    const char *name;
    int kind;
    bool indexed;        /* x.c[i]: under each key, something of that kind */
} object_fields[64];
static int object_field_count;
static bool objects_used, collections_used;
static bool zeros_used;   /* zero_(): nothing, of whatever kind it is put into */
static Loc objects_loc;
static bool infer_changed;
static bool infer_last;   /* nothing more will tell: what comes out of a collection of who knows what is a whole number */

static void ours_at(Loc loc) {
    if (!objects_used && !collections_used) objects_loc = loc;
}

static int *object_field(const char *name, Loc loc) {
    for (int i = 0; i < object_field_count; i++)
        if (strcmp(object_fields[i].name, name) == 0) return &object_fields[i].kind;
    static int spare;
    if (object_field_count == 64) return &spare;
    ours_at(loc);
    objects_used = true;
    object_fields[object_field_count].name = name;
    object_fields[object_field_count].kind = IN_UNKNOWN;
    object_fields[object_field_count].indexed = false;
    infer_changed = true;
    return &object_fields[object_field_count++].kind;
}

static void field_indexed(const char *name, Loc loc) {
    object_field(name, loc);
    for (int i = 0; i < object_field_count; i++) {
        if (strcmp(object_fields[i].name, name) != 0 || object_fields[i].indexed) continue;
        object_fields[i].indexed = true;
        ours_at(loc);
        collections_used = infer_changed = true;
    }
}

static bool field_is_indexed(const char *name) {
    for (int i = 0; i < object_field_count; i++)
        if (strcmp(object_fields[i].name, name) == 0) return object_fields[i].indexed;
    return false;
}

typedef struct {
    Parser *p;
    Function *function;
} Infer;

static int spec_kind(const TypeSpec *spec) {
    if (!spec) return IN_UNKNOWN;
    if (spec->base == TOK_IDENT && spec->pointers) return spec->holds ? spec->holds : IN_OBJECT;
    switch (spec->base) {
        case TOK_INTEGER: return IN_INT;
        case TOK_REAL: return IN_REAL;
        case TOK_LOGICAL: return IN_BOOL;
        case TOK_STRINGTYPE: return IN_TEXT;
        default: return IN_UNKNOWN;
    }
}

/* What a collection of this is said to hold: numbers, texts, objects, other collections (7), or it is not known. */
static int elem_of(int kind) {
    if (kind >= IN_SET) return 7;
    return kind == IN_BOOL ? IN_INT : kind == IN_CELL ? IN_UNKNOWN : kind;
}
static int held(int kind) { return kind < IN_SET || kind == IN_PAIRS ? IN_UNKNOWN : kind - IN_SET == 7 ? IN_SET : kind - IN_SET; }

static bool calls(const Expr *e, const char *name) {
    return e && e->kind == EX_APPLY && e->base->kind == EX_NAME && strcmp(e->base->text, name) == 0;
}

static int infer_expr(Infer *in, const Expr *e);

/* What a collection holds, from one thing that goes into it: two objects together are an edge. */
static int elem_from(Infer *in, const Expr *e) {
    if (!calls(e, "pair_")) return elem_of(infer_expr(in, e));
    return e->args.count == 2 && infer_expr(in, e->args.items[0]) == IN_OBJECT && infer_expr(in, e->args.items[1]) == IN_OBJECT ? 6 : IN_UNKNOWN;
}

static VarDecl *find_in_statements(Stmt *s, const char *name) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            VarDecl *d = find_decl(b->vars, name);
            if (!d) d = find_in_statements(b->body, name);
            if (d) return d;
        }
    }
    return NULL;
}

static VarDecl *infer_find(Infer *in, const char *name) {
    VarDecl *d = find_decl(in->function->params, name);
    if (!d) d = find_decl(in->function->locals, name);
    if (!d) d = find_in_statements(in->function->body, name);
    if (!d) d = find_decl(in->p->program->globals, name);
    return d;
}

static Function *function_named(Parser *p, const char *name) {
    for (Function *f = p->program->functions; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

/* A name with brackets after it that is neither a procedure nor a function of ours: w(u, v). */
static bool applied_variable(Infer *in, const Expr *e) {
    return in->p->config->cormen && e->kind == EX_APPLY && e->base->kind == EX_NAME && !function_named(in->p, e->base->text) &&
           infer_find(in, e->base->text);
}

/* Somewhere a value can be kept: a name, an attribute, an element. */
static bool is_place(Infer *in, const Expr *e) {
    if (e->kind == EX_NAME) return strcmp(e->text, "nil_") != 0 && !function_named(in->p, e->text);
    return e->kind == EX_FIELD || e->kind == EX_INDEX || applied_variable(in, e);
}

static int infer_expr(Infer *in, const Expr *e) {
    if (e->kind == EX_NAME && strcmp(e->text, "nil_") == 0) return IN_OBJECT;
    if (e->kind == EX_FIELD) return *object_field(e->text, e->loc);
    if (e->kind == EX_APPLY && e->base->kind == EX_NAME) { /* what is ours */
        const char *n = e->base->text;
        const Expr *first = e->args.count ? e->args.items[0] : NULL;
        if (strcmp(n, "new_object_") == 0) return IN_OBJECT;
        if (strcmp(n, "set_") == 0) return IN_SET;
        if (strcmp(n, "set_of_") == 0) {
            int elem = IN_UNKNOWN;
            for (int i = 0; i < e->args.count; i++) {
                int kind = elem_from(in, e->args.items[i]);
                if (kind > elem) elem = kind;
            }
            return IN_SET + elem;
        }
        if (strcmp(n, "in_") == 0 || strcmp(n, "empty_") == 0 || strcmp(n, "more_") == 0 || strcmp(n, "inside_") == 0) return IN_BOOL;
        if ((strcmp(n, "copy_of_") == 0 || strncmp(n, "sort_", 5) == 0) && first) {
            int kind = infer_expr(in, first);
            return kind >= IN_SET ? kind : IN_SET;
        }
        if (strcmp(n, "count_") == 0) return IN_INT;
        if (strcmp(n, "first_") == 0 || strcmp(n, "next_") == 0) return IN_CELL;
        if (strcmp(n, "item_") == 0) return e->args.count > 1 ? held(infer_expr(in, e->args.items[1])) : IN_UNKNOWN;
        if (strcmp(n, "item1_") == 0 || strcmp(n, "item2_") == 0)
            return e->args.count > 1 && infer_expr(in, e->args.items[1]) == IN_PAIRS ? IN_OBJECT : IN_UNKNOWN;
        if (strcmp(n, "pair_") == 0) return IN_UNKNOWN;
        if (strcmp(n, "put_last_") == 0 && e->args.count == 2) {
            int was = infer_expr(in, first);
            int now = IN_SET + elem_from(in, e->args.items[1]);
            return was > now ? was : now;
        }
        if (strncmp(n, "take_", 5) == 0 && first) return held(infer_expr(in, first));
    }
    switch (e->kind) {
        case EX_INT: return IN_INT;
        case EX_FLOAT: return IN_REAL;
        case EX_BOOL: return IN_BOOL;
        case EX_STRING: case EX_CHAR: return IN_TEXT;
        case EX_NAME: {
            VarDecl *d = infer_find(in, e->text);
            return d ? spec_kind(d->spec) : IN_UNKNOWN;
        }
        case EX_INDEX: return infer_expr(in, e->base);
        case EX_APPLY: {
            if (e->base->kind != EX_NAME) return IN_UNKNOWN;
            const char *n = e->base->text;
            Function *f = function_named(in->p, n);
            if (f) return spec_kind(f->result);
            if (applied_variable(in, e)) return spec_kind(infer_find(in, n)->spec);
            const Builtin *b = builtin_named(n);
            if (b && b->result == 'i') return IN_INT;
            if (b && b->result == 't') return IN_TEXT;
            if (b && b->result == 'b') return IN_BOOL;
            if (b && b->result == 'n' && e->args.count) {
                int widest = IN_UNKNOWN;
                for (int i = 0; i < e->args.count; i++) {
                    int kind = infer_expr(in, e->args.items[i]);
                    if (kind > widest) widest = kind;
                }
                return widest;
            }
            return IN_REAL;
        }
        case EX_UNARY: return e->op == TOK_NOT ? IN_BOOL : infer_expr(in, e->base);
        case EX_BINARY:
            switch (e->op) {
                case TOK_AND: case TOK_OR: case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE:
                    return IN_BOOL;
                case TOK_RDIV: return IN_REAL;
                case TOK_CONCAT: return IN_TEXT;
                case TOK_MOD: case TOK_IDIV: return IN_INT;
                default: {
                    int a = infer_expr(in, e->base), b = infer_expr(in, e->right);
                    return a > b ? a : b;
                }
            }
        default: return IN_UNKNOWN;
    }
}

static TypeSpec *kind_spec(Parser *p, int kind, Loc loc) {
    static const TokenKind BASES[] = {TOK_REAL, TOK_LOGICAL, TOK_INTEGER, TOK_REAL, TOK_STRINGTYPE};
    if (kind >= IN_OBJECT) { /* it is known by where it is: a pointer to the one kind of object, or to a collection */
        TypeSpec *spec = simple_spec(p, TOK_IDENT, loc);
        spec->name = kind == IN_OBJECT ? "object" : kind == IN_CELL ? "cell_" : "set_";
        spec->pointers = 1;
        spec->holds = kind == IN_OBJECT ? 0 : kind;
        ours_at(loc);
        if (kind == IN_OBJECT) objects_used = true;
        else collections_used = true;
        return spec;
    }
    return simple_spec(p, BASES[kind], loc);
}

/* What is indexed by anything is not an array: w[u, v], d[v] where v is an object. */
static void mark_map(VarDecl *d, Loc loc) {
    if (d->is_map) return;
    ours_at(loc);
    d->is_map = true;
    d->has_dims = false;
    d->dims = (DimList){0};
    collections_used = infer_changed = true;
}

/* What is stored in a name tells its type; more than one: the widest. */
static void infer_store(Infer *in, const Expr *target, int kind) {
    bool indexed = target->kind == EX_INDEX || applied_variable(in, target);
    if (target->kind == EX_FIELD || (indexed && target->base->kind == EX_FIELD)) { /* what an attribute is given tells what it holds */
        const Expr *field = indexed ? target->base : target;
        int *kept = object_field(field->text, field->loc);
        if (kind > *kept) {
            *kept = kind;
            infer_changed = true;
        }
        return;
    }
    const Expr *name = indexed ? target->base : target;
    if (name->kind != EX_NAME) return;
    VarDecl *d = infer_find(in, name->text);
    if (!d) {
        if (function_named(in->p, name->text)) return;
        d = ARENA_NEW(in->p->arena, VarDecl);
        d->loc = name->loc;
        d->name = name->text;
        d->inferred = true;
        append_decl(&in->function->locals, d);
        infer_changed = true;
    }
    if (d->spec && !d->inferred) return;
    if (!d->spec) d->inferred = true;
    if (kind > spec_kind(d->spec) || (!d->spec && kind != IN_UNKNOWN)) {
        d->spec = kind_spec(in->p, kind, d->loc);
        infer_changed = true;
    }
}

static bool is_ours(const char *name) {
    static const char *const OURS[] = {"set_", "set_of_", "in_", "empty_", "more_", "count_", "first_", "next_", "item_", "item2_", "pair_",
                                       "put_last_", "take_first_", "take_last_", "take_min_", "take_max_", "inside_", "copy_of_", "sort_", "item1_",
                                       "sort_by_", NULL};
    for (int i = 0; OURS[i]; i++)
        if (strcmp(OURS[i], name) == 0) return true;
    return false;
}

static void infer_uses(Infer *in, const Expr *e) {
    if (!e) return;
    bool cormen = in->p->config->cormen;
    if (e->kind == EX_NAME && strcmp(e->text, "nil_") == 0) objects_used = true;
    if (e->kind == EX_FIELD) { /* what has an attribute is an object */
        object_field(e->text, e->loc);
        if (e->base->kind == EX_NAME || e->base->kind == EX_INDEX || e->base->kind == EX_FIELD) infer_store(in, e->base, IN_OBJECT);
    }
    if (e->kind == EX_BINARY && e->right && (e->op == TOK_EQ || e->op == TOK_NE)) { /* and so is what is compared with one */
        int a = infer_expr(in, e->base), b = infer_expr(in, e->right);
        if (a == IN_OBJECT && b < IN_OBJECT) infer_store(in, e->right, IN_OBJECT);
        if (b == IN_OBJECT && a < IN_OBJECT) infer_store(in, e->base, IN_OBJECT);
        if (a >= IN_SET && b < IN_SET && is_place(in, e->right)) infer_store(in, e->right, IN_SET); /* and with a collection */
        if (b >= IN_SET && a < IN_SET && is_place(in, e->base)) infer_store(in, e->base, IN_SET);
    }
    bool indexing = e->kind == EX_INDEX || applied_variable(in, e);
    if (indexing && e->base->kind == EX_FIELD && cormen) field_indexed(e->base->text, e->base->loc);
    if (indexing && e->base->kind == EX_NAME) {
        VarDecl *d = infer_find(in, e->base->text);
        bool param = d && find_decl(in->function->params, e->base->text) == d;
        bool array = d && (d->has_dims || (d->spec && d->spec->is_array));
        if (cormen && d && !d->is_map) { /* by an object, or never made as an array: indexed by anything */
            bool by_object = false;
            for (int i = 0; i < e->args.count; i++) by_object |= infer_expr(in, e->args.items[i]) == IN_OBJECT;
            if (by_object || (!param && !array)) mark_map(d, e->loc);
        }
        if (param && !d->is_map && !array) { /* a parameter that is indexed is an array */
            d->has_dims = true;
            for (int i = 0; i < e->args.count; i++) dim_add(in->p, &d->dims, (Dim){NULL, NULL});
            infer_changed = true;
        }
    }
    if (calls(e, "zero_")) zeros_used = true;
    if (e->kind == EX_APPLY && e->base->kind == EX_NAME && is_ours(e->base->text)) {
        ours_at(e->loc);
        collections_used = true;
        bool through = calls(e, "first_") || calls(e, "count_") || calls(e, "empty_") || strncmp(e->base->text, "take_", 5) == 0;
        if (through && e->args.count && is_place(in, e->args.items[0]) && infer_expr(in, e->args.items[0]) < IN_SET) {
            const Expr *s = e->args.items[0]; /* what is gone through, counted, taken from: a collection */
            VarDecl *array = s->kind == EX_NAME ? infer_find(in, s->text) : NULL;
            if (!(array && array->has_dims)) infer_store(in, s, IN_SET);
        }
        if (calls(e, "in_") && e->args.count == 2) { /* x ∈ S: S holds what x is, and x is what S holds */
            const Expr *x = e->args.items[0], *s = e->args.items[1];
            int kx = elem_from(in, x), ks = infer_expr(in, s);
            VarDecl *array = s->kind == EX_NAME ? infer_find(in, s->text) : NULL;
            if (is_place(in, s) && IN_SET + kx > ks && !(array && array->has_dims)) infer_store(in, s, IN_SET + kx);
            if (held(ks) == IN_OBJECT && kx != IN_OBJECT && is_place(in, x)) infer_store(in, x, IN_OBJECT);
        }
    }
    if (e->kind == EX_APPLY && e->base->kind == EX_NAME) {
        Function *f = function_named(in->p, e->base->text);
        VarDecl *param = f ? f->params : NULL;
        for (int i = 0; i < e->args.count && param; i++, param = param->next) {
            const Expr *arg = e->args.items[i];
            int kind = infer_expr(in, arg);
            if ((!param->spec || param->inferred) && kind > spec_kind(param->spec)) {
                param->spec = kind_spec(in->p, kind, param->loc);
                param->inferred = true;
                infer_changed = true;
            }
            /* what is handed to who takes an object, or a collection, is one */
            int wanted = spec_kind(param->spec);
            if (cormen && wanted >= IN_OBJECT && kind < wanted && param->mode == MODE_IN && is_place(in, arg)) infer_store(in, arg, wanted);
            /* what a procedure fills in exists from then on, and is what the procedure makes of it */
            if (param->mode != MODE_IN && !param->has_dims && arg->kind == EX_NAME && !function_named(in->p, arg->text))
                infer_store(in, arg, spec_kind(param->spec));
            /* what is indexed by anything begins to be where it is first handed to who fills it in */
            if (cormen && param->is_map && arg->kind == EX_NAME && !infer_find(in, arg->text) && !function_named(in->p, arg->text))
                infer_store(in, arg, spec_kind(param->spec));
            VarDecl *given = cormen && arg->kind == EX_NAME ? infer_find(in, arg->text) : NULL;
            if (given && given->is_map != param->is_map) { /* indexed by anything on one side: on the other too */
                mark_map(given, arg->loc);
                mark_map(param, arg->loc);
            }
            /* what is kept under the keys is what who fills them in puts there */
            if (given && given->is_map && param->is_map && kind == IN_UNKNOWN && wanted != IN_UNKNOWN) infer_store(in, arg, wanted);
            /* an array handed to who never looks inside it is an array there too; and what an array holds
             * is what who fills it in puts there */
            if (given && !given->is_map && (given->has_dims || (given->spec && given->spec->is_array))) {
                if (!param->is_map && !param->has_dims && !(param->spec && param->spec->is_array)) {
                    param->has_dims = true;
                    for (int k = 0; k < (given->dims.count ? given->dims.count : 1); k++) dim_add(in->p, &param->dims, (Dim){NULL, NULL});
                    infer_changed = true;
                }
                if (kind == IN_UNKNOWN && wanted != IN_UNKNOWN && wanted < IN_OBJECT) infer_store(in, arg, wanted);
            }
            /* what is handed on as an array is one, even where nobody looks inside it */
            if (param->has_dims && arg->kind == EX_NAME) {
                VarDecl *mine = find_decl(in->function->params, arg->text);
                if (mine && !mine->is_map && !mine->has_dims && !(mine->spec && mine->spec->is_array)) {
                    mine->has_dims = true;
                    for (int k = 0; k < param->dims.count; k++) dim_add(in->p, &mine->dims, (Dim){NULL, NULL});
                    infer_changed = true;
                }
            }
            /* a matrix: its sizes come from what is passed */
            if (param->has_dims && param->dims.count > 1 && !param->dims.items[1].hi && arg->kind == EX_NAME) {
                VarDecl *from = infer_find(in, arg->text);
                if (from && from->dims.count == param->dims.count) {
                    param->dims = from->dims;
                    infer_changed = true;
                }
            }
        }
    }
    infer_uses(in, e->base);
    infer_uses(in, e->right);
    for (int i = 0; i < e->args.count; i++) infer_uses(in, e->args.items[i]);
}

static void infer_statements(Infer *in, Stmt *s) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            infer_uses(in, b->target);
            infer_uses(in, b->value);
            infer_uses(in, b->limit);
            infer_uses(in, b->step);
            for (int i = 0; i < b->args.count; i++) infer_uses(in, b->args.items[i]);
            if (b->kind == ST_ASSIGN) {
                int value = infer_expr(in, b->value);
                infer_store(in, b->target, value);
                int target = infer_expr(in, b->target);
                bool place = b->value->kind == EX_FIELD || b->value->kind == EX_NAME || b->value->kind == EX_INDEX;
                /* and backwards: what goes into an object is one (x = L.head, where x has attributes) */
                if (target == IN_OBJECT && value != IN_OBJECT) {
                    if (place) infer_store(in, b->value, IN_OBJECT);
                    /* and what an object is taken from holds objects */
                    if (calls(b->value, "item_") && b->value->args.count > 1) infer_store(in, b->value->args.items[1], IN_SET + IN_OBJECT);
                    if ((calls(b->value, "item1_") || calls(b->value, "item2_")) && b->value->args.count > 1 && is_place(in, b->value->args.items[1]))
                        infer_store(in, b->value->args.items[1], IN_PAIRS);
                    if (b->value->kind == EX_APPLY && b->value->base->kind == EX_NAME && strncmp(b->value->base->text, "take_", 5) == 0 &&
                        b->value->args.count)
                        infer_store(in, b->value->args.items[0], IN_SET + IN_OBJECT);
                }
                if (target >= IN_SET && value < target && place) infer_store(in, b->value, target);
                if ((target == IN_REAL || target == IN_TEXT || target >= IN_SET) && value == IN_UNKNOWN) {
                    if (calls(b->value, "item_") && b->value->args.count > 1) infer_store(in, b->value->args.items[1], IN_SET + elem_of(target));
                    if (b->value->kind == EX_APPLY && b->value->base->kind == EX_NAME && strncmp(b->value->base->text, "take_", 5) == 0 &&
                        b->value->args.count)
                        infer_store(in, b->value->args.items[0], IN_SET + elem_of(target));
                }
                bool taken = b->value->kind == EX_APPLY && b->value->base->kind == EX_NAME &&
                             (strncmp(b->value->base->text, "item", 4) == 0 || strncmp(b->value->base->text, "take_", 5) == 0);
                if (infer_last && taken && target == IN_UNKNOWN) infer_store(in, b->target, IN_INT);
            }
            if (b->kind == ST_FOR) {
                int kind = infer_expr(in, b->value);
                infer_store(in, b->target, kind == IN_REAL ? IN_REAL : IN_INT);
            }
            if (b->kind == ST_READ)
                for (int i = 0; i < b->args.count; i++) infer_store(in, b->args.items[i], IN_UNKNOWN);
            infer_statements(in, b->body);
        }
    }
}

/* ---- what is ours, in the base language ---- */

/* The procedures that do what is done with collections. `needs` names those
 * each one calls. In the text, $K is the key as it is taken, $A as it is
 * handed on, $C that of the cell c, $M whether c has it, $Y the attribute
 * objects are ordered by; ?x{...} is kept only where x is in use: o objects,
 * and what is kept under a key: b i r t p (an object) s (a collection). */
typedef struct {
    const char *name, *needs, *text;
} Helper;

static const char OURS_TYPES[] =
    "type cell_ : record\n    k1_ : integer\n    k2_ : integer\n?o{    o1_ : object*\n    o2_ : object*\n}"
    "?R{    r1_ : real\n    r2_ : real\n}?T{    t1_ : string\n    t2_ : string\n}?S{    s1_ : set_*\n    s2_ : set_*\n}?Q{    rank_ : real\n}"
    "?b{    vb_ : logical\n}?i{    vi_ : integer\n}?r{    vr_ : real\n}?t{    vt_ : string\n}?p{    vo_ : object*\n}?s{    vs_ : set_*\n}"
    "    next_ : cell_*\n    free_ : cell_*\nend\n"
    "type set_ : record\n    first_ : cell_*\n    last_ : cell_*\n    count_ : integer\nend\n"
    "var cells_ : array [1..2048] of cell_\nvar cells_used_ : integer\nvar spare_ : cell_*\n"
    "var sets_ : array [1..512] of set_\nvar sets_used_ : integer\nvar none_ : cell_\n";

static const Helper HELPERS[] = {
    {"new_set_", "", "function new_set_() : set_*\nbegin\n    sets_used_ := sets_used_ + 1\n    new_set_ := &sets_[sets_used_]\nend new_set_\n"},
    {"find_", "",
     "function find_(s : set_*, $K) : cell_*\nvar c : cell_*\nbegin\n    c := 0\n    if s != 0 then\n        c := s.first_\n"
     "        while c != 0 and not ($M) do\n            c := c.next_\n        endwhile\n    endif\n    find_ := c\nend find_\n"},
    {"append_", "new_set_",
     "function append_(s : set_*, $K) : set_*\nvar c : cell_*\nbegin\n    if s = 0 then\n        s := new_set_()\n    endif\n"
     "    if spare_ != 0 then\n        c := spare_\n        spare_ := c.free_\n    else\n        cells_used_ := cells_used_ + 1\n"
     "        c := &cells_[cells_used_]\n    endif\n    c.k1_ := k1\n    c.k2_ := k2\n?o{    c.o1_ := o1\n    c.o2_ := o2\n}"
     "?R{    c.r1_ := r1\n    c.r2_ := r2\n}?T{    c.t1_ := t1\n    c.t2_ := t2\n}?S{    c.s1_ := s1\n    c.s2_ := s2\n}"
     "?b{    c.vb_ := false\n}?i{    c.vi_ := 0\n}?r{    c.vr_ := 0\n}?t{    c.vt_ := \"\"\n}?p{    c.vo_ := 0\n}?s{    c.vs_ := 0\n}"
     "    c.next_ := 0\n    if s.last_ = 0 then\n        s.first_ := c\n    else\n        s.last_.next_ := c\n    endif\n"
     "    s.last_ := c\n    s.count_ := s.count_ + 1\n    append_ := s\nend append_\n"},
    {"add_", "find_ append_",
     "function add_(s : set_*, $K) : set_*\nbegin\n    if find_(s, $A) = 0 then\n        s := append_(s, $A)\n    endif\n    add_ := s\nend add_\n"},
    {"has_", "find_", "function has_(s : set_*, $K) : logical\nbegin\n    has_ := find_(s, $A) != 0\nend has_\n"},
    {"count_", "", "function count_(s : set_*) : integer\nbegin\n    count_ := 0\n    if s != 0 then\n        count_ := s.count_\n    endif\nend count_\n"},
    {"first_", "", "function first_(s : set_*) : cell_*\nbegin\n    first_ := 0\n    if s != 0 then\n        first_ := s.first_\n    endif\nend first_\n"},
    {"unlink_", "",
     "function unlink_(s : set_*, c : cell_*) : cell_*\nvar before : cell_*\nbegin\n    before := 0\n    if s.first_ = c then\n"
     "        s.first_ := c.next_\n    else\n        before := s.first_\n        while before.next_ != c do\n            before := before.next_\n"
     "        endwhile\n        before.next_ := c.next_\n    endif\n    if s.last_ = c then\n        s.last_ := before\n    endif\n"
     "    s.count_ := s.count_ - 1\n    c.free_ := spare_\n    spare_ := c\n    unlink_ := c\nend unlink_\n"},
    {"remove_", "find_ unlink_",
     "function remove_(s : set_*, $K) : set_*\nvar c : cell_*\nbegin\n    c := find_(s, $A)\n    if c != 0 then\n        c := unlink_(s, c)\n"
     "    endif\n    remove_ := s\nend remove_\n"},
    {"take_first_", "unlink_",
     "function take_first_(s : set_*) : cell_*\nbegin\n    take_first_ := &none_\n    if s != 0 then\n        if s.first_ != 0 then\n"
     "            take_first_ := unlink_(s, s.first_)\n        endif\n    endif\nend take_first_\n"},
    {"take_last_", "unlink_",
     "function take_last_(s : set_*) : cell_*\nbegin\n    take_last_ := &none_\n    if s != 0 then\n        if s.last_ != 0 then\n"
     "            take_last_ := unlink_(s, s.last_)\n        endif\n    endif\nend take_last_\n"},
    {"take_least_", "first_ unlink_",
     "function take_least_(s : set_*, largest : integer) : cell_*\nvar c, best : cell_*\nvar value, least : real\nbegin\n    best := 0\n"
     "    least := 0\n    c := first_(s)\n    while c != 0 do\n        value := c.k1_\n        if largest = 1 then\n            value := -value\n"
     "        endif\n        if best = 0 or value < least then\n            best := c\n            least := value\n        endif\n"
     "        c := c.next_\n    endwhile\n    take_least_ := &none_\n    if best != 0 then\n        take_least_ := unlink_(s, best)\n    endif\n"
     "end take_least_\n"},
    {"take_least_object_", "first_ unlink_",
     "function take_least_object_(s : set_*, largest : integer) : cell_*\nvar c, best : cell_*\nvar value, least : real\nbegin\n    best := 0\n"
     "    least := 0\n    c := first_(s)\n    while c != 0 do\n        value := c.o1_.$Y\n        if largest = 1 then\n            value := -value\n"
     "        endif\n        if best = 0 or value < least then\n            best := c\n            least := value\n        endif\n"
     "        c := c.next_\n    endwhile\n    take_least_object_ := &none_\n    if best != 0 then\n        take_least_object_ := unlink_(s, best)\n"
     "    endif\nend take_least_object_\n"},
    {"add_all_", "first_ add_",
     "function add_all_(a : set_*, b : set_*) : set_*\nvar c : cell_*\nbegin\n    c := first_(b)\n    while c != 0 do\n"
     "        a := add_(a, $C)\n        c := c.next_\n    endwhile\n    add_all_ := a\nend add_all_\n"},
    {"copy_", "new_set_ first_ append_",
     "function copy_(s : set_*) : set_*\nvar c : cell_*\nvar made : set_*\nbegin\n    made := new_set_()\n    c := first_(s)\n"
     "    while c != 0 do\n        made := append_(made, $C)\n"
     "?b{        made.last_.vb_ := c.vb_\n}?i{        made.last_.vi_ := c.vi_\n}?r{        made.last_.vr_ := c.vr_\n}"
     "?t{        made.last_.vt_ := c.vt_\n}?p{        made.last_.vo_ := c.vo_\n}?s{        made.last_.vs_ := c.vs_\n}"
     "        c := c.next_\n    endwhile\n    copy_ := made\nend copy_\n"},
    {"union_", "copy_ add_all_", "function union_(a : set_*, b : set_*) : set_*\nbegin\n    union_ := add_all_(copy_(a), b)\nend union_\n"},
    {"remove_all_", "first_ remove_",
     "function remove_all_(a : set_*, b : set_*) : set_*\nvar c : cell_*\nbegin\n    c := first_(b)\n    while c != 0 do\n"
     "        a := remove_(a, $C)\n        c := c.next_\n    endwhile\n    remove_all_ := a\nend remove_all_\n"},
    {"minus_", "copy_ remove_all_", "function minus_(a : set_*, b : set_*) : set_*\nbegin\n    minus_ := remove_all_(copy_(a), b)\nend minus_\n"},
    {"common_", "new_set_ first_ has_ append_",
     "function common_(a : set_*, b : set_*) : set_*\nvar c : cell_*\nvar made : set_*\nbegin\n    made := new_set_()\n    c := first_(a)\n"
     "    while c != 0 do\n        if has_(b, $C) then\n            made := append_(made, $C)\n        endif\n        c := c.next_\n"
     "    endwhile\n    common_ := made\nend common_\n"},
    {"same_", "count_ first_ has_",
     "function same_(a : set_*, b : set_*) : logical\nvar c : cell_*\nbegin\n    same_ := count_(a) = count_(b)\n    c := first_(a)\n"
     "    while c != 0 do\n        if not has_(b, $C) then\n            same_ := false\n        endif\n        c := c.next_\n    endwhile\n"
     "end same_\n"},
    {"made_", "new_set_", "function made_(s : set_*) : set_*\nbegin\n    if s = 0 then\n        s := new_set_()\n    endif\n    made_ := s\nend made_\n"},
    {"slot_", "find_ append_",
     "function slot_(s : set_*, $K) : cell_*\nvar c : cell_*\nbegin\n    c := find_(s, $A)\n    if c = 0 then\n        s := append_(s, $A)\n"
     "        c := s.last_\n    endif\n    slot_ := c\nend slot_\n"},
    {"get_", "find_",
     "function get_(s : set_*, $K) : cell_*\nvar c : cell_*\nbegin\n    c := find_(s, $A)\n    if c = 0 then\n        c := &none_\n    endif\n"
     "    get_ := c\nend get_\n"},
    {"take_least_real_", "first_ unlink_",
     "function take_least_real_(s : set_*, largest : integer) : cell_*\nvar c, best : cell_*\nvar value, least : real\nbegin\n    best := 0\n"
     "    least := 0\n    c := first_(s)\n    while c != 0 do\n        value := c.r1_\n        if largest = 1 then\n            value := -value\n"
     "        endif\n        if best = 0 or value < least then\n            best := c\n            least := value\n        endif\n"
     "        c := c.next_\n    endwhile\n    take_least_real_ := &none_\n    if best != 0 then\n        take_least_real_ := unlink_(s, best)\n"
     "    endif\nend take_least_real_\n"},
    {"inside_", "first_ has_",
     "function inside_(a : set_*, b : set_*) : logical\nvar c : cell_*\nbegin\n    inside_ := true\n    c := first_(a)\n"
     "    while c != 0 do\n        if not has_(b, $C) then\n            inside_ := false\n        endif\n        c := c.next_\n    endwhile\n"
     "end inside_\n"},
    {"rank_self_", "first_",
     "function rank_self_(s : set_*) : set_*\nvar c : cell_*\nbegin\n    c := first_(s)\n    while c != 0 do\n"
     "        c.rank_ := c.k1_?R{ + c.r1_}\n        c := c.next_\n    endwhile\n    rank_self_ := s\nend rank_self_\n"},
    {"rank_by_map_", "first_ get_",
     "function rank_by_map_(s : set_*, w : set_*) : set_*\nvar c : cell_*\nbegin\n    c := first_(s)\n    while c != 0 do\n"
     "        c.rank_ := get_(w, $C).$V\n        c := c.next_\n    endwhile\n    rank_by_map_ := s\nend rank_by_map_\n"},
    {"rank_by_attr_", "first_",
     "function rank_by_attr_(s : set_*) : set_*\nvar c : cell_*\nbegin\n    c := first_(s)\n    while c != 0 do\n"
     "        c.rank_ := c.o1_.$Z\n        c := c.next_\n    endwhile\n    rank_by_attr_ := s\nend rank_by_attr_\n"},
    {"order_", "",
     "function order_(s : set_*, descending : integer) : set_*\nvar rest, best, before, prev, c : cell_*\nbegin\n    if s != 0 then\n"
     "        rest := s.first_\n        s.first_ := 0\n        s.last_ := 0\n        while rest != 0 do\n            best := rest\n"
     "            before := 0\n            prev := rest\n            c := rest.next_\n            while c != 0 do\n"
     "                if (descending = 0 and c.rank_ < best.rank_) or (descending = 1 and c.rank_ > best.rank_) then\n"
     "                    best := c\n                    before := prev\n                endif\n                prev := c\n"
     "                c := c.next_\n            endwhile\n            if before = 0 then\n                rest := best.next_\n"
     "            else\n                before.next_ := best.next_\n            endif\n            best.next_ := 0\n"
     "            if s.last_ = 0 then\n                s.first_ := best\n            else\n                s.last_.next_ := best\n"
     "            endif\n            s.last_ := best\n        endwhile\n    endif\n    order_ := s\nend order_\n"},
    {"show_part_", "",
     "procedure show_part_(k : integer?o{, o : object*}?R{, r : real}?T{, t : string}?S{, s : set_*})\nvar shown : logical\nbegin\n"
     "    shown := false\n?o{    if o != 0 then\n?n{        print(o.$N)\n}?u{        print(\"\xe2\x80\xa2\")\n}        shown := true\n    endif\n}"
     "?S{    if not shown and s != 0 then\n        show_(s)\n        shown := true\n    endif\n}"
     "?T{    if not shown and t != \"\" then\n        print(t)\n        shown := true\n    endif\n}"
     "?R{    if not shown and r != 0 then\n        print(r)\n        shown := true\n    endif\n}"
     "    if not shown then\n        print(k)\n    endif\nend show_part_\n"},
    {"show_", "first_ show_part_",
     "procedure show_(s : set_*)\nvar c : cell_*\nvar pair : logical\nbegin\n    print(\"{\")\n    c := first_(s)\n    while c != 0 do\n"
     "        pair := c.k2_ != 0?o{ or c.o2_ != 0}?R{ or c.r2_ != 0}?T{ or c.t2_ != \"\"}?S{ or c.s2_ != 0}\n"
     "        if pair then\n            print(\"(\")\n        endif\n        show_part_($P)\n        if pair then\n"
     "            print(\", \")\n            show_part_($p)\n            print(\")\")\n        endif\n"
     "?i{        if c.vi_ != 0 then\n            print(\": \", c.vi_)\n        endif\n}"
     "?r{        if c.vr_ != 0 then\n            print(\": \", c.vr_)\n        endif\n}"
     "?t{        if c.vt_ != \"\" then\n            print(\": \", c.vt_)\n        endif\n}"
     "?p{        if c.vo_ != 0 then\n            print(\": \")\n            show_part_(0, c.vo_?R{, 0}?T{, \"\"}?S{, 0})\n        endif\n}"
     "?s{        if c.vs_ != 0 then\n            print(\": \")\n            show_(c.vs_)\n        endif\n}"
     "        c := c.next_\n        if c != 0 then\n            print(\", \")\n        endif\n    endwhile\n    print(\"}\")\nend show_\n"},
};
enum { HELPER_COUNT = sizeof HELPERS / sizeof HELPERS[0] };

static unsigned long helpers_needed;
static unsigned values_needed;   /* which of b i r t p s are kept under keys */
static unsigned keys_needed;     /* which keys there are besides whole numbers and objects: 1 reals, 2 texts, 4 collections */
static const char *least_key;    /* the attribute objects are ordered by, when the least of them is taken */
static const char *shown_by;     /* the attribute an object is printed by */
static const char *sort_value;   /* where the weights are, when a collection is sorted by them */
static const char *sort_attr;    /* the attribute objects are sorted by */
static bool sort_used;

static void need(const char *name) {
    for (int i = 0; i < HELPER_COUNT; i++) {
        size_t n = strlen(HELPERS[i].name);
        if (strncmp(HELPERS[i].name, name, n) != 0 || (name[n] && name[n] != ' ')) continue;
        if (helpers_needed & (1ul << i)) return;
        helpers_needed |= 1ul << i;
        for (const char *more = HELPERS[i].needs; *more; more += strcspn(more, " "), more += *more == ' ') need(more);
        return;
    }
}

static void ours_text(StrBuf *out, const char *text, size_t length) {
    static const char VALUES[] = "birtps";
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '$' && i + 1 < length) {
            const char *macro = NULL;
            switch (text[++i]) {
                case 'K':
                    macro = "k1 : integer, k2 : integer?o{, o1 : object*, o2 : object*}?R{, r1 : real, r2 : real}?T{, t1 : string, t2 : string}"
                            "?S{, s1 : set_*, s2 : set_*}";
                    break;
                case 'A': macro = "k1, k2?o{, o1, o2}?R{, r1, r2}?T{, t1, t2}?S{, s1, s2}"; break;
                case 'C': macro = "c.k1_, c.k2_?o{, c.o1_, c.o2_}?R{, c.r1_, c.r2_}?T{, c.t1_, c.t2_}?S{, c.s1_, c.s2_}"; break;
                case 'M':
                    macro = "c.k1_ = k1 and c.k2_ = k2?o{ and c.o1_ = o1 and c.o2_ = o2}?R{ and c.r1_ = r1 and c.r2_ = r2}"
                            "?T{ and c.t1_ = t1 and c.t2_ = t2}?S{ and same_(c.s1_, s1) and same_(c.s2_, s2)}";
                    break;
                case 'P': macro = "c.k1_?o{, c.o1_}?R{, c.r1_}?T{, c.t1_}?S{, c.s1_}"; break;
                case 'p': macro = "c.k2_?o{, c.o2_}?R{, c.r2_}?T{, c.t2_}?S{, c.s2_}"; break;
                case 'N': macro = shown_by ? shown_by : "key"; break;
                case 'V': macro = sort_value ? sort_value : "vi_"; break;
                case 'Z': macro = sort_attr ? sort_attr : "key"; break;
                default: macro = least_key ? least_key : "key"; break;
            }
            ours_text(out, macro, strlen(macro));
        } else if (text[i] == '?' && i + 2 < length && text[i + 2] == '{') {
            char what = text[i + 1];
            const char *close = NULL, *value = strchr(VALUES, what);
            int depth = 1; /* one may be inside another */
            for (size_t k = i + 3; k < length && !close; k++) {
                if (text[k] == '?' && k + 2 < length && text[k + 2] == '{') depth++;
                else if (text[k] == '}' && --depth == 0) close = text + k;
            }
            if (!close) return;
            bool kept = what == 'o' ? objects_used : value && (values_needed & (1u << (value - VALUES)));
            if (what == 'R' || what == 'T' || what == 'S') kept = keys_needed & (what == 'R' ? 1u : what == 'T' ? 2u : 4u);
            if (what == 'Q') kept = sort_used;
            if (what == 'n' || what == 'u') kept = objects_used && (what == 'n') == (shown_by != NULL);
            if (kept) ours_text(out, text + i + 3, (size_t)(close - text - i - 3));
            i = (size_t)(close - text);
        } else {
            strbuf_append(out, text + i, 1);
        }
    }
}

static Expr *ours_name(Parser *p, Loc loc, const char *name) {
    Expr *e = new_expr(p, EX_NAME, loc, NULL, NULL);
    e->text = name;
    e->op = TOK_IDENT;
    return e;
}

static Expr *ours_call(Parser *p, Loc loc, const char *name, Expr *a, Expr *b) {
    Expr *e = new_expr(p, EX_APPLY, loc, ours_name(p, loc, name), NULL);
    if (a) list_add(p, &e->args, a);
    if (b) list_add(p, &e->args, b);
    need(name);
    return e;
}

static Expr *ours_field(Parser *p, Expr *base, const char *name) {
    Expr *e = new_expr(p, EX_FIELD, base->loc, base, NULL);
    e->op = TOK_DOT;
    e->text = name;
    return e;
}

static Expr *clone(Parser *p, const Expr *e) {
    if (!e) return NULL;
    Expr *copy = new_expr(p, e->kind, e->loc, clone(p, e->base), clone(p, e->right));
    copy->op = e->op;
    copy->text = e->text;
    for (int i = 0; i < e->args.count; i++) list_add(p, &copy->args, clone(p, e->args.items[i]));
    return copy;
}

static bool same_expr(const Expr *a, const Expr *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind || a->args.count != b->args.count) return false;
    if ((a->kind == EX_NAME || a->kind == EX_FIELD || a->kind == EX_INT) && strcmp(a->text, b->text) != 0) return false;
    if (a->kind != EX_NAME && a->kind != EX_FIELD && a->kind != EX_INT && a->kind != EX_INDEX && a->kind != EX_APPLY) return false;
    for (int i = 0; i < a->args.count; i++)
        if (!same_expr(a->args.items[i], b->args.items[i])) return false;
    return same_expr(a->base, b->base);
}

/* The field of a cell that keeps something of this kind under a key. */
static const char *value_field(int kind) {
    static const char *const FIELDS[] = {"vr_", "vb_", "vi_", "vr_", "vt_", "vo_"};
    static const unsigned BITS[] = {4, 1, 2, 4, 8, 16};
    values_needed |= kind >= IN_CELL ? 32u : BITS[kind];
    return kind >= IN_CELL ? "vs_" : FIELDS[kind];
}

static Expr *lower(Infer *in, Expr *e, int want);

/* A key, as the procedures of ours take it: two whole numbers, then, where
 * the program has them, two objects, two reals, two texts, two collections.
 * Which of those there are is known when everything has been looked at: until
 * then each key is kept aside, with the call it goes into. */
typedef struct KeyUse {
    Expr *call;
    Expr *parts[5][2]; /* whole numbers, objects, reals, texts, collections */
    struct KeyUse *next;
} KeyUse;
static KeyUse *key_uses;

static void keys(Infer *in, Expr *call, Expr *first, Expr *second, int holds) {
    Parser *p = in->p;
    if (!second && calls(first, "pair_") && first->args.count == 2) second = first->args.items[1], first = first->args.items[0], holds = IN_UNKNOWN;
    Expr *parts[2] = {first, second};
    KeyUse *use = ARENA_NEW(p->arena, KeyUse);
    use->call = call;
    use->next = key_uses;
    key_uses = use;
    for (int i = 0; i < 2; i++) {
        if (!parts[i]) continue;
        int kind = infer_expr(in, parts[i]);
        if (kind == IN_CELL) {
            diag_error(p->diag, parts[i]->loc, "this cannot be kept in a collection, nor be an index");
        } else if (kind == IN_OBJECT) {
            use->parts[1][i] = lower(in, parts[i], -1);
        } else if (kind >= IN_SET) { /* a collection in a collection: as it is now */
            keys_needed |= 4;
            Expr *inner = lower(in, parts[i], -1);
            use->parts[4][i] = is_place(in, parts[i]) ? ours_call(p, parts[i]->loc, "copy_", inner, NULL) : inner;
        } else if (kind == IN_TEXT) {
            keys_needed |= 2;
            use->parts[3][i] = lower(in, parts[i], -1);
        } else if (kind == IN_REAL || holds == IN_REAL) {
            keys_needed |= 1;
            use->parts[2][i] = lower(in, parts[i], -1);
        } else if (kind == IN_INT || kind == IN_BOOL) {
            use->parts[0][i] = lower(in, parts[i], -1);
        } else {
            use->parts[0][i] = ours_call(p, parts[i]->loc, "trunc", lower(in, parts[i], -1), NULL);
        }
    }
}

static void finish_keys(Parser *p) {
    for (KeyUse *use = key_uses; use; use = use->next) {
        for (int kind = 0; kind < 5; kind++) {
            bool there = kind == 0 || (kind == 1 ? objects_used : (keys_needed & (1u << (kind - 2))) != 0);
            for (int i = 0; i < 2 && there; i++) {
                Expr *part = use->parts[kind][i];
                if (!part) {
                    part = new_expr(p, kind == 2 ? EX_FLOAT : kind == 3 ? EX_STRING : EX_INT, use->call->loc, NULL, NULL);
                    part->text = kind == 2 ? "0.0" : kind == 3 ? "" : "0";
                    part->op = kind == 2 ? TOK_FLOAT : kind == 3 ? TOK_STRING : TOK_INT;
                }
                list_add(p, &use->call->args, part);
            }
        }
    }
    key_uses = NULL;
}

/* The part of a cell where something of this kind is. */
static const char *key_field(int kind, bool second) {
    static const char *const FIELDS[][2] = {{"k1_", "k2_"}, {"o1_", "o2_"}, {"r1_", "r2_"}, {"t1_", "t2_"}, {"s1_", "s2_"}};
    int which = kind == IN_OBJECT ? 1 : kind == IN_REAL ? 2 : kind == IN_TEXT ? 3 : kind >= IN_SET ? 4 : 0;
    if (which >= 2) keys_needed |= 1u << (which - 2);
    return FIELDS[which][second];
}

static bool is_map_access(Infer *in, const Expr *e) {
    if (e->kind != EX_INDEX && !applied_variable(in, e)) return false;
    if (e->base->kind == EX_FIELD) return field_is_indexed(e->base->text);
    VarDecl *d = e->base->kind == EX_NAME ? infer_find(in, e->base->text) : NULL;
    return d && d->is_map;
}

/* m[k]: the cell kept under k, and in it the field for what m holds. */
static Expr *map_access(Infer *in, Expr *e, bool store) {
    Parser *p = in->p;
    int kind = infer_expr(in, e);
    if (e->args.count > 2) diag_error(p->diag, e->loc, "what is indexed by anything takes one index, or two");
    Expr *call = ours_call(p, e->loc, store ? "slot_" : "get_", lower(in, e->base, -1), NULL);
    keys(in, call, e->args.items[0], e->args.count > 1 ? e->args.items[1] : NULL, IN_UNKNOWN);
    return ours_field(p, call, value_field(kind));
}

static bool is_nothing(const Expr *e) { return calls(e, "set_") || (e->kind == EX_NAME && strcmp(e->text, "nil_") == 0); }

/* An expression with what is ours in it, as calls of the procedures added to
 * the program. `want` is the kind of what it is stored into, when that tells
 * which part of a cell to read; -1 otherwise. */
static Expr *lower(Infer *in, Expr *e, int want) {
    if (!e) return NULL;
    Parser *p = in->p;
    Loc loc = e->loc;
    if (e->kind == EX_APPLY && e->base->kind == EX_NAME && is_ours(e->base->text) && (e->args.count || e->base->text[3] == '_')) {
        const char *n = e->base->text;
        Expr **a = e->args.items;
        int count = e->args.count;
        if (strcmp(n, "set_") == 0) return ours_call(p, loc, "new_set_", NULL, NULL);
        if (strcmp(n, "set_of_") == 0) {
            Expr *made = ours_call(p, loc, "new_set_", NULL, NULL);
            int holds = held(infer_expr(in, e)); /* 1 beside 2.5 is a real too, and so it is in a set of reals */
            if (want >= IN_SET && held(want) > holds) holds = held(want);
            for (int i = 0; i < count; i++) {
                made = ours_call(p, loc, "add_", made, NULL);
                keys(in, made, a[i], NULL, holds);
            }
            return made;
        }
        if (strcmp(n, "inside_") == 0 && count == 2) return ours_call(p, loc, "inside_", lower(in, a[0], -1), lower(in, a[1], -1));
        if (strcmp(n, "copy_of_") == 0) return ours_call(p, loc, "copy_", lower(in, a[0], -1), NULL);
        if (strncmp(n, "sort_", 5) == 0) { /* put in order: by what each is, by an attribute, or by what is kept under it somewhere */
            if (infer_expr(in, a[0]) < IN_SET) {
                diag_error(p->diag, loc, "a collection is sorted in words like these: an array is sorted by a procedure");
                return int_expr(p, loc, "0");
            }
            const char *by = n[5] == 'b' && count == 3 ? a[1]->text : NULL;
            Expr *ranked = NULL, *down = a[count - 1];
            VarDecl *weights = by ? infer_find(in, by) : NULL;
            sort_used = true;
            if (!by) {
                ranked = ours_call(p, loc, "rank_self_", lower(in, a[0], -1), NULL);
            } else if (weights && weights->is_map) {
                const char *field = value_field(spec_kind(weights->spec));
                if (sort_value && strcmp(sort_value, field) != 0) diag_error(p->diag, loc, "collections are sorted by whole numbers, or by reals: not by both in one program");
                sort_value = field;
                ranked = ours_call(p, loc, "rank_by_map_", lower(in, a[0], -1), ours_name(p, loc, weights->name));
            } else {
                bool attribute = false;
                for (int i = 0; i < object_field_count; i++) attribute |= strcmp(object_fields[i].name, by) == 0;
                if (!attribute || (sort_attr && strcmp(sort_attr, by) != 0))
                    diag_error(p->diag, loc, "to sort by '%s' it must be what the objects have, or where a number is kept for each: and one only in a program", by);
                sort_attr = by;
                ranked = ours_call(p, loc, "rank_by_attr_", lower(in, a[0], -1), NULL);
            }
            return ours_call(p, loc, "order_", ranked, down);
        }
        if (strcmp(n, "in_") == 0 && count == 2) {
            if (infer_expr(in, a[1]) < IN_SET) {
                diag_error(p->diag, loc, "whether something is in a collection can be asked: this is not a collection");
                return int_expr(p, loc, "0");
            }
            Expr *has = ours_call(p, loc, "has_", lower(in, a[1], -1), NULL);
            keys(in, has, a[0], NULL, held(infer_expr(in, a[1])));
            return has;
        }
        if (strcmp(n, "count_") == 0) return ours_call(p, loc, "count_", lower(in, a[0], -1), NULL);
        if (strcmp(n, "empty_") == 0 || strcmp(n, "more_") == 0) {
            bool empty = n[0] == 'e';
            Expr *left = empty ? ours_call(p, loc, "count_", lower(in, a[0], -1), NULL) : a[0];
            Expr *test = new_expr(p, EX_BINARY, loc, left, int_expr(p, loc, "0"));
            test->op = empty ? TOK_EQ : TOK_NE;
            return test;
        }
        if (strcmp(n, "first_") == 0) {
            if (infer_expr(in, a[0]) < IN_SET)
                diag_error(p->diag, loc, "'for each' goes through a collection, or through an array made here or taken with its length");
            return ours_call(p, loc, "first_", lower(in, a[0], -1), NULL);
        }
        if (strcmp(n, "next_") == 0) return ours_field(p, a[0], "next_");
        if (strncmp(n, "item", 4) == 0) { /* by what the collection holds; else by where it goes */
            int whole = count > 1 ? infer_expr(in, a[1]) : IN_UNKNOWN, kind = n[4] == '_' ? held(whole) : whole == IN_PAIRS ? IN_OBJECT : IN_UNKNOWN;
            if (kind == IN_UNKNOWN && want >= 0) kind = want;
            return ours_field(p, a[0], key_field(kind, n[4] == '2'));
        }
        if (strcmp(n, "put_last_") == 0 && count == 2) {
            int holds = held(infer_expr(in, e));
            Expr *put = ours_call(p, loc, "append_", lower(in, a[0], -1), NULL);
            keys(in, put, a[1], NULL, holds);
            return put;
        }
        if (strncmp(n, "take_", 5) == 0) {
            int kind = held(infer_expr(in, a[0]));
            if (kind == IN_UNKNOWN && want >= 0) kind = want;
            bool object = kind == IN_OBJECT;
            Expr *taken;
            if (strcmp(n, "take_first_") == 0 || strcmp(n, "take_last_") == 0) {
                taken = ours_call(p, loc, n, lower(in, a[0], -1), NULL);
            } else {
                if (object && !least_key) {
                    for (int i = 0; i < object_field_count && !least_key; i++)
                        if (strcmp(object_fields[i].name, "key") == 0) least_key = "key";
                    for (int i = 0; i < object_field_count && !least_key; i++)
                        if (strcmp(object_fields[i].name, "d") == 0) least_key = "d";
                    if (!least_key) diag_error(p->diag, loc, "the least of a collection of objects is the one with the least 'key', or 'd': these have neither");
                }
                if (kind == IN_TEXT || kind >= IN_SET) diag_error(p->diag, loc, "the least and the greatest are of numbers, or of objects with a 'key'");
                taken = ours_call(p, loc, object ? "take_least_object_" : kind == IN_REAL ? "take_least_real_" : "take_least_", lower(in, a[0], -1),
                                  int_expr(p, loc, strcmp(n, "take_max_") == 0 ? "1" : "0"));
            }
            return ours_field(p, taken, key_field(kind, false));
        }
        diag_error(p->diag, loc, "this is not something done with a collection");
        return e;
    }
    if (e->kind == EX_BINARY && e->right) {
        int left = infer_expr(in, e->base), right = infer_expr(in, e->right);
        if (left >= IN_SET || right >= IN_SET) {
            const char *joined = e->op == TOK_PLUS ? "union_" : e->op == TOK_MINUS ? "minus_" : e->op == TOK_STAR ? "common_" : NULL;
            int whole = left > right ? left : right; /* 1 joined to a set of reals is a real */
            if (joined) return ours_call(p, loc, joined, lower(in, e->base, whole), lower(in, e->right, whole));
            if (e->op == TOK_EQ || e->op == TOK_NE) {
                if (is_nothing(e->base) || is_nothing(e->right)) { /* S = ∅: there is nothing in it */
                    Expr *count = ours_call(p, loc, "count_", lower(in, is_nothing(e->right) ? e->base : e->right, -1), NULL);
                    Expr *test = new_expr(p, EX_BINARY, loc, count, int_expr(p, loc, "0"));
                    test->op = e->op;
                    return test;
                }
                Expr *same = ours_call(p, loc, "same_", lower(in, e->base, -1), lower(in, e->right, -1));
                if (e->op == TOK_EQ) return same;
                Expr *not = new_expr(p, EX_UNARY, loc, same, NULL);
                not->op = TOK_NOT;
                return not;
            }
            diag_error(p->diag, loc, "collections are joined, met, taken away from one another and compared: nothing else");
        }
    }
    if (is_map_access(in, e)) return map_access(in, e, false);
    e->base = lower(in, e->base, -1);
    e->right = lower(in, e->right, -1);
    for (int i = 0; i < e->args.count; i++) e->args.items[i] = lower(in, e->args.items[i], -1);
    return e;
}

/* An assignment. S = S ∪ {x} adds to S where it is; T = S makes T another
 * collection with the same things in it, as with sets in mathematics; what is
 * kept under a key goes into a cell, made if it was not there. */
static void lower_assign(Infer *in, Stmt **at) {
    Parser *p = in->p;
    Stmt *s = *at;
    Expr *t = s->target, *v = s->value;
    int target = infer_expr(in, t), value = infer_expr(in, v);
    bool collections = target >= IN_SET || value >= IN_SET;
    bool ours = (t->kind == EX_NAME && t->text[strlen(t->text) - 1] == '_') || (v->kind == EX_NAME && v->text[strlen(v->text) - 1] == '_');
    if (collections && v->kind == EX_BINARY && v->right && (v->op == TOK_PLUS || v->op == TOK_MINUS) && same_expr(t, v->base)) {
        bool plus = v->op == TOK_PLUS;
        Expr *made = lower(in, v->base, -1);
        if (calls(v->right, "set_of_")) {
            int holds = held(target > value ? target : value);
            for (int i = 0; i < v->right->args.count; i++) {
                made = ours_call(p, v->loc, plus ? "add_" : "remove_", made, NULL);
                keys(in, made, v->right->args.items[i], NULL, holds);
            }
        } else {
            made = ours_call(p, v->loc, plus ? "add_all_" : "remove_all_", made, lower(in, v->right, -1));
        }
        s->value = made;
    } else if (calls(v, "zero_")) {
        Expr *zero = new_expr(p, target == IN_REAL ? EX_FLOAT : target == IN_TEXT ? EX_STRING : target == IN_BOOL ? EX_BOOL : EX_INT, v->loc, NULL, NULL);
        zero->text = target == IN_REAL ? "0.0" : target == IN_TEXT ? "" : target == IN_BOOL ? "false" : "0";
        zero->op = target == IN_REAL ? TOK_FLOAT : target == IN_TEXT ? TOK_STRING : target == IN_BOOL ? TOK_FALSE : TOK_INT;
        s->value = zero;
    } else if (collections && !ours && is_place(in, v)) {
        s->value = ours_call(p, v->loc, "copy_", lower(in, v, -1), NULL);
    } else {
        s->value = lower(in, v, target);
    }
    if (!is_map_access(in, t)) {
        s->target = lower(in, t, -1);
        return;
    }
    if (t->base->kind == EX_FIELD) { /* x.c[i] = ...: what x.c is kept in is made the first time */
        Stmt *make = assign_stmt(p, s->loc, lower(in, clone(p, t->base), -1), ours_call(p, t->loc, "made_", lower(in, clone(p, t->base), -1), NULL));
        make->next = s;
        *at = make;
    }
    s->target = map_access(in, t, true);
}

static bool is_collection_value(Infer *in, const Expr *e) {
    if (infer_expr(in, e) >= IN_SET) return true;
    VarDecl *d = e->kind == EX_NAME ? infer_find(in, e->text) : NULL;
    return (d && d->is_map) || (e->kind == EX_FIELD && field_is_indexed(e->text));
}

/* print S: what is before it, then S as mathematics writes it, then what is after. */
static Stmt *lower_print(Infer *in, Stmt **at) {
    Parser *p = in->p;
    Stmt *s = *at, *first = NULL, *last = NULL, *run = NULL;
    for (int i = 0; i < s->args.count; i++) {
        Expr *arg = s->args.items[i];
        Stmt *one = run;
        if (is_collection_value(in, arg)) {
            one = ARENA_NEW(p->arena, Stmt);
            one->kind = ST_CALL;
            one->loc = s->loc;
            one->value = ours_call(p, arg->loc, "show_", lower(in, arg, -1), NULL);
            run = NULL;
        } else if (!run) {
            one = run = ARENA_NEW(p->arena, Stmt);
            *run = *s;
            run->args = (ExprList){0};
            run->same_line = true;
        }
        if (one == run && run) list_add(p, &run->args, lower(in, arg, -1));
        if (one != last) {
            if (last) last->next = one;
            else first = one;
            last = one;
        }
    }
    if (last->kind == ST_PRINT) {
        last->same_line = s->same_line;
    } else if (!s->same_line) { /* the end of the line */
        Stmt *end = ARENA_NEW(p->arena, Stmt);
        *end = *s;
        end->args = (ExprList){0};
        Expr *nothing = new_expr(p, EX_STRING, s->loc, NULL, NULL);
        nothing->text = "";
        nothing->op = TOK_STRING;
        list_add(p, &end->args, nothing);
        last->next = end;
        last = end;
    }
    last->next = s->next;
    *at = first;
    return last;
}

static void lower_statements(Infer *in, Stmt **list) {
    Stmt **at = list;
    while (*at) {
        Stmt *s = *at;
        if (s->kind == ST_ASSIGN) {
            lower_assign(in, at); /* something may have been put before it */
            at = &s->next;
            continue;
        }
        bool whole = false;
        for (int i = 0; s->kind == ST_PRINT && i < s->args.count; i++) whole |= is_collection_value(in, s->args.items[i]);
        if (whole) {
            at = &lower_print(in, at)->next;
            continue;
        }
        for (Stmt *b = s; b; b = b->otherwise) {
            b->target = lower(in, b->target, -1);
            b->value = lower(in, b->value, -1);
            b->limit = lower(in, b->limit, -1);
            b->step = lower(in, b->step, -1);
            for (int i = 0; i < b->args.count; i++) b->args.items[i] = lower(in, b->args.items[i], -1);
            lower_statements(in, &b->body);
        }
        at = &s->next;
    }
}

/* nil_ is the pointer to nothing, which the base language writes 0. */
static void objects_expr(Expr *e) {
    if (!e) return;
    if (e->kind == EX_NAME && strcmp(e->text, "nil_") == 0) {
        e->kind = EX_INT;
        e->text = "0";
        return;
    }
    objects_expr(e->base);
    objects_expr(e->right);
    for (int i = 0; i < e->args.count; i++) objects_expr(e->args.items[i]);
}

static void objects_statements(Stmt *s) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            objects_expr(b->target);
            objects_expr(b->value);
            objects_expr(b->limit);
            objects_expr(b->step);
            for (int i = 0; i < b->args.count; i++) objects_expr(b->args.items[i]);
            objects_statements(b->body);
        }
    }
}

static void ours_prints(Stmt *s) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            if (b->kind == ST_PRINT) b->same_line = b->joined = true;
            ours_prints(b->body);
        }
    }
}

/* The kind of object there is, the store objects are taken from, what
 * collections are made of and the procedures that work on them: written in
 * the base language, which has all it takes, and added to the program. */
static void add_ours(Parser *p) {
    static const char *const TYPES[] = {"integer", "logical", "integer", "real", "string", "object*"};
    StrBuf text = {0};
    strbuf_puts(&text, "");
    if (objects_used) {
        strbuf_puts(&text, "type object : record\n");
        for (int i = 0; i < object_field_count; i++)
            strbuf_printf(&text, "    %s : %s\n", object_fields[i].name,
                          object_fields[i].indexed || object_fields[i].kind > IN_OBJECT ? "set_*" : TYPES[object_fields[i].kind]);
        if (!object_field_count) strbuf_puts(&text, "    key : integer\n");
        strbuf_puts(&text, "end\nvar objects_ : array [1..256] of object\nvar objects_used_ : integer\n"
                           "function new_object_() : object*\nbegin\n    objects_used_ := objects_used_ + 1\n"
                           "    new_object_ := &objects_[objects_used_]\nend new_object_\n");
    }
    if (collections_used) {
        static const char *const BY[] = {"name", "key", "id", NULL};
        shown_by = NULL;
        for (int k = 0; BY[k] && !shown_by; k++)
            for (int i = 0; i < object_field_count; i++)
                if (strcmp(object_fields[i].name, BY[k]) == 0 && !object_fields[i].indexed && object_fields[i].kind < IN_OBJECT) shown_by = BY[k];
        if (keys_needed & 4) need("same_");
        ours_text(&text, OURS_TYPES, strlen(OURS_TYPES));
        for (int i = 0; i < HELPER_COUNT; i++)
            if (helpers_needed & (1ul << i)) ours_text(&text, HELPERS[i].text, strlen(HELPERS[i].text));
    }
    Source source = {"objects", arena_strndup(p->arena, text.data, text.length), text.length, NULL};
    strbuf_free(&text);
    Config plain = CONFIG_DEFAULT;
    Program *more = parse(lex(&source, &plain, p->arena, p->diag), &plain, p->arena, p->diag);
    if (!more) return;
    for (TypeDecl *t = more->types; t; t = t->next) t->loc = objects_loc;
    TypeDecl **types = &p->program->types;
    while (*types) types = &(*types)->next;
    *types = more->types;
    VarDecl **globals = &p->program->globals;
    while (*globals) globals = &(*globals)->next;
    *globals = more->globals;
    Function **functions = &p->program->functions;
    while (*functions) functions = &(*functions)->next;
    *functions = more->functions;
    for (Function *f = more->functions; f; f = f->next) {
        f->loc = objects_loc;
        f->is_quiet = true;
        if (strncmp(f->name, "show_", 5) == 0) ours_prints(f->body); /* piece after piece, on one line */
    }
    for (Function *f = p->program->functions; f; f = f->next) objects_statements(f->body);
}

static void finish_decls(Parser *p, VarDecl *d) {
    for (; d; d = d->next)
        if (!d->spec) d->spec = kind_spec(p, IN_UNKNOWN, d->loc); /* a number, then */
}

/* In Cormen's book what a procedure takes besides its arrays is a size, a
 * position, a key to look for: a whole number, when nothing says otherwise. */
static void finish_params(Parser *p, VarDecl *d) {
    for (; d; d = d->next)
        if (!d->spec) d->spec = kind_spec(p, p->config->cormen && !d->has_dims ? IN_INT : IN_UNKNOWN, d->loc);
}

static void finish_statements(Parser *p, Stmt *s) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            finish_decls(p, b->vars);
            finish_statements(p, b->body);
        }
    }
}

/* What is indexed by anything is a collection: made where it begins to be, if nobody hands it in. */
static void finish_maps(Parser *p, VarDecl *d, Function *maker) {
    for (; d; d = d->next) {
        if (!d->is_map) continue;
        d->spec = kind_spec(p, IN_SET, d->loc);
        if (!maker) continue;
        Stmt *make = assign_stmt(p, maker->loc, ours_name(p, maker->loc, d->name), ours_call(p, maker->loc, "new_set_", NULL, NULL));
        make->next = maker->body;
        maker->body = make;
    }
}

static void infer_types(Parser *p) {
    object_field_count = 0;
    objects_used = collections_used = zeros_used = false;
    helpers_needed = values_needed = keys_needed = 0;
    least_key = sort_value = sort_attr = NULL;
    sort_used = false;
    key_uses = NULL;
    for (const char *name = p->config->cormen ? p->config->shared : NULL; name && *name; name += strcspn(name, " "), name += *name == ' ') {
        VarDecl *d = ARENA_NEW(p->arena, VarDecl); /* what the procedures of Cormen's book share */
        d->name = arena_strndup(p->arena, name, strcspn(name, " "));
        d->inferred = true;
        if (!find_decl(p->program->globals, d->name)) append_decl(&p->program->globals, d);
    }
    infer_last = false;
    for (int round = 0; round < 24; round++) {
        infer_changed = false;
        for (Function *f = p->program->functions; f; f = f->next) {
            Infer in = {p, f};
            infer_statements(&in, f->body);
            if (f->result_name) {
                VarDecl *r = infer_find(&in, f->result_name);
                if (r && r->spec && spec_kind(r->spec) != spec_kind(f->result)) {
                    f->result = r->spec;
                    infer_changed = true;
                }
            }
        }
        if (!infer_changed && (infer_last || !collections_used)) break;
        if (!infer_changed) infer_last = true;
    }
    Function *main_function = NULL;
    for (Function *f = p->program->functions; f; f = f->next) {
        finish_params(p, f->params);
        finish_decls(p, f->locals);
        finish_statements(p, f->body);
        if (!f->is_procedure && !f->result) f->result = kind_spec(p, IN_UNKNOWN, f->loc);
        if (strcmp(f->name, "main") == 0) main_function = f;
    }
    finish_decls(p, p->program->globals);
    for (int i = 0; i < object_field_count; i++)
        if (object_fields[i].kind == IN_UNKNOWN) object_fields[i].kind = IN_INT;
    if (collections_used || zeros_used) {
        for (Function *f = p->program->functions; f; f = f->next) {
            Infer in = {p, f};
            lower_statements(&in, &f->body);
        }
        for (Function *f = p->program->functions; f; f = f->next) {
            finish_maps(p, f->params, NULL);
            finish_maps(p, f->locals, f);
        }
        finish_maps(p, p->program->globals, main_function);
        finish_keys(p);
    }
    if (objects_used || collections_used) add_ours(p);
}
