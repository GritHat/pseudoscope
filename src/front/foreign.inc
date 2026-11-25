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

enum { IN_UNKNOWN, IN_BOOL, IN_INT, IN_REAL, IN_TEXT };

typedef struct {
    Parser *p;
    Function *function;
} Infer;

static int spec_kind(const TypeSpec *spec) {
    if (!spec) return IN_UNKNOWN;
    switch (spec->base) {
        case TOK_INTEGER: return IN_INT;
        case TOK_REAL: return IN_REAL;
        case TOK_LOGICAL: return IN_BOOL;
        case TOK_STRINGTYPE: return IN_TEXT;
        default: return IN_UNKNOWN;
    }
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

static int infer_expr(Infer *in, const Expr *e) {
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
            if (strcmp(n, "random") == 0 || strcmp(n, "random_below") == 0 || strcmp(n, "length") == 0 ||
                strcmp(n, "trunc") == 0 || strcmp(n, "round") == 0)
                return IN_INT;
            if (strcmp(n, "upper") == 0 || strcmp(n, "lower") == 0 || strcmp(n, "substring") == 0 || strcmp(n, "concat") == 0)
                return IN_TEXT;
            if (strcmp(n, "abs") == 0 && e->args.count) return infer_expr(in, e->args.items[0]);
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
                    if (a == IN_UNKNOWN || b == IN_UNKNOWN) return a > b ? a : b;
                    return a > b ? a : b;
                }
            }
        default: return IN_UNKNOWN;
    }
}

static TypeSpec *kind_spec(Parser *p, int kind, Loc loc) {
    static const TokenKind BASES[] = {TOK_REAL, TOK_LOGICAL, TOK_INTEGER, TOK_REAL, TOK_STRINGTYPE};
    return simple_spec(p, BASES[kind], loc);
}

static bool infer_changed;

/* What is stored in a name tells its type; more than one: the widest. */
static void infer_store(Infer *in, const Expr *target, int kind) {
    bool indexed = target->kind == EX_INDEX;
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

static void infer_uses(Infer *in, const Expr *e) {
    if (!e) return;
    if (e->kind == EX_INDEX && e->base->kind == EX_NAME) { /* a parameter that is indexed is an array */
        VarDecl *d = find_decl(in->function->params, e->base->text);
        if (d && !d->has_dims && !(d->spec && d->spec->is_array)) {
            d->has_dims = true;
            for (int i = 0; i < e->args.count; i++) dim_add(in->p, &d->dims, (Dim){NULL, NULL});
            infer_changed = true;
        }
    }
    if (e->kind == EX_APPLY && e->base->kind == EX_NAME) {
        Function *f = function_named(in->p, e->base->text);
        VarDecl *param = f ? f->params : NULL;
        for (int i = 0; i < e->args.count && param; i++, param = param->next) {
            int kind = infer_expr(in, e->args.items[i]);
            if ((!param->spec || param->inferred) && kind > spec_kind(param->spec)) {
                param->spec = kind_spec(in->p, kind, param->loc);
                param->inferred = true;
                infer_changed = true;
            }
            /* a matrix: its sizes come from what is passed */
            if (param->has_dims && param->dims.count > 1 && !param->dims.items[1].hi && e->args.items[i]->kind == EX_NAME) {
                VarDecl *arg = infer_find(in, e->args.items[i]->text);
                if (arg && arg->dims.count == param->dims.count) {
                    param->dims = arg->dims;
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
            if (b->kind == ST_ASSIGN) infer_store(in, b->target, infer_expr(in, b->value));
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

static void finish_decls(Parser *p, VarDecl *d) {
    for (; d; d = d->next)
        if (!d->spec) d->spec = kind_spec(p, IN_UNKNOWN, d->loc); /* a number, then */
}

static void finish_statements(Parser *p, Stmt *s) {
    for (; s; s = s->next) {
        for (Stmt *b = s; b; b = b->otherwise) {
            finish_decls(p, b->vars);
            finish_statements(p, b->body);
        }
    }
}

static void infer_types(Parser *p) {
    for (int round = 0; round < 8; round++) {
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
        if (!infer_changed) break;
    }
    for (Function *f = p->program->functions; f; f = f->next) {
        finish_decls(p, f->params);
        finish_decls(p, f->locals);
        finish_statements(p, f->body);
        if (!f->is_procedure && !f->result) f->result = kind_spec(p, IN_UNKNOWN, f->loc);
    }
    finish_decls(p, p->program->globals);
}
