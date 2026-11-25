#include "plike/vm.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Code *code;
    const Config *config;
    const Function *function;
    int slots;
} Compiler;

static void value(Compiler *c, const Expr *expr);
static void address(Compiler *c, const Expr *expr);
static void statements(Compiler *c, const Stmt *stmt);

static Instr *emit(Compiler *c, Op op) {
    Code *code = c->code;
    if (code->count == code->capacity) {
        code->capacity = code->capacity ? code->capacity * 2 : 256;
        code->items = realloc(code->items, (size_t)code->capacity * sizeof(Instr));
        if (!code->items) abort();
    }
    Instr *in = &code->items[code->count++];
    memset(in, 0, sizeof *in);
    in->op = op;
    in->function = c->function;
    return in;
}

static int here(const Compiler *c) { return c->code->count; }
static void patch(Compiler *c, int at) { c->code->items[at].a = here(c); }

static int jump(Compiler *c, Op op, int target) {
    emit(c, op)->a = target;
    return here(c) - 1;
}

static bool is_array(const Type *type) { return type->kind == TY_ARRAY; }
static bool is_number(const Type *t) {
    return t->kind == TY_INT || t->kind == TY_REAL || t->kind == TY_BOOL || t->kind == TY_CHAR;
}

static void convert(Compiler *c, Type *to) {
    if (is_number(to) || to->kind == TY_POINTER || to->kind == TY_STRING) emit(c, OP_CONVERT)->type = to;
}

static void note(Compiler *c, const Expr *expr) { emit(c, OP_EVAL)->expr = expr; }

static void constant(Compiler *c, Value v) {
    v.known = true;
    emit(c, OP_CONST)->value = v;
}

/* ---- expressions ---- */

/* The variable an array access starts from, if its bounds are needed at run
 * time, and how many indices were applied before. */
static const Symbol *root_symbol(const Expr *base, int *skipped) {
    *skipped = 0;
    while ((base->kind == EX_INDEX || base->kind == EX_APPLY) && !base->is_call &&
           is_array(base->base->type)) {
        *skipped += base->args.count;
        base = base->base;
    }
    return base->kind == EX_NAME ? base->symbol : NULL;
}

static void index_address(Compiler *c, const Expr *expr) {
    value(c, expr->base); /* an array or a pointer: in both cases a place */
    for (int i = 0; i < expr->args.count; i++) value(c, expr->args.items[i]);
    Instr *in = emit(c, OP_INDEX);
    in->expr = expr;
    in->type = expr->base->type;
    in->a = expr->args.count;
    if (is_array(expr->base->type)) {
        int skipped;
        in = &c->code->items[here(c) - 1];
        in->symbol = root_symbol(expr->base, &skipped);
        in->b = skipped;
    }
}

static void address(Compiler *c, const Expr *expr) {
    if (expr->is_call) { /* a record returned by a function: a temporary */
        value(c, expr);
        return;
    }
    switch (expr->kind) {
        case EX_NAME:
            emit(c, OP_ADDR)->symbol = expr->symbol;
            return;
        case EX_APPLY: case EX_INDEX:
            index_address(c, expr);
            return;
        case EX_FIELD:
            if (expr->base->type->kind == TY_POINTER) value(c, expr->base);
            else address(c, expr->base);
            {
                const Type *record = expr->base->type->kind == TY_POINTER ? expr->base->type->base : expr->base->type;
                Instr *in = emit(c, OP_FIELD);
                in->a = expr->symbol->slot;
                if (record->kind == TY_RECORD && record->record->klass && record->record->klass->has_derived) {
                    in->b = 1; /* the object may be of a class that extends this one */
                    in->symbol = expr->symbol;
                    in->type = (Type *)record;
                }
            }
            return;
        case EX_UNARY:
            if (expr->op == TOK_STAR) {
                value(c, expr->base);
                return;
            }
            if (expr->op == TOK_AT && !(expr->base->symbol && expr->base->symbol->by_reference)) {
                address(c, expr->base);
                return;
            }
            break;
        default:
            break;
    }
    emit(c, OP_UNSUPPORTED)->expr = expr;
}

static void call(Compiler *c, const Expr *expr) {
    const Symbol *callee = expr->symbol;
    if (!callee->function && !callee->builtin) {
        emit(c, OP_EXTERNAL)->expr = expr;
        return;
    }
    for (int i = 0; i < expr->args.count; i++) {
        const Expr *arg = expr->args.items[i];
        const Symbol *param = callee->params ? callee->params[i] : NULL;
        if (param && param->by_reference) {
            /* swap(&a, @b): the pointer is already there */
            bool pointer = arg->kind == EX_UNARY && (arg->op == TOK_AMP || arg->op == TOK_AT);
            if (pointer) value(c, arg);
            else address(c, arg);
        } else {
            value(c, arg);
            if (param) convert(c, param->type);
        }
    }
    Instr *in = emit(c, callee->builtin ? OP_BUILTIN : OP_CALL);
    in->expr = expr;
    in->symbol = callee;
    in->a = expr->args.count;
    in->b = expr->is_static;
}

static void binary(Compiler *c, const Expr *expr) {
    if (expr->op == TOK_AND || expr->op == TOK_OR) {
        value(c, expr->base);
        int skip = jump(c, expr->op == TOK_AND ? OP_AND : OP_OR, 0);
        value(c, expr->right);
        emit(c, OP_TRUTH);
        patch(c, skip);
        return;
    }
    value(c, expr->base);
    value(c, expr->right);
    Instr *in = emit(c, OP_BINARY);
    in->expr = expr;
    in->a = (int)expr->op;
}

static void number(Compiler *c, const Expr *expr) {
    Value v = {0};
    if (expr->kind == EX_INT) {
        long long n = 0;
        const_int(expr, &n);
        v.kind = V_INT;
        v.as.i = (int32_t)(uint32_t)(unsigned long long)n;
    } else {
        size_t length = strlen(expr->text);
        char last = expr->text[length - 1];
        double d = strtod(expr->text, NULL);
        if (last == 'f' || last == 'F') {
            v.kind = V_FLOAT;
            v.as.f = strtof(expr->text, NULL);
        } else {
            v.kind = V_DOUBLE;
            v.as.d = d;
        }
    }
    constant(c, v);
}

static int escape(const char *text) {
    if (text[0] != '\\') return (signed char)text[0];
    switch (text[1]) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        case '0': return 0;
        default: return text[1];
    }
}

static void value(Compiler *c, const Expr *expr) {
    switch (expr->kind) {
        case EX_INT: case EX_FLOAT:
            number(c, expr);
            return;
        case EX_BOOL: {
            Value v = {.kind = V_INT};
            v.as.i = expr->op == TOK_TRUE;
            constant(c, v);
            return;
        }
        case EX_CHAR: {
            Value v = {.kind = V_INT};
            v.as.i = escape(expr->text);
            constant(c, v);
            return;
        }
        case EX_STRING: {
            Code *code = c->code;
            if (code->string_count == code->string_capacity) {
                code->string_capacity = code->string_capacity ? code->string_capacity * 2 : 16;
                code->strings = realloc(code->strings,
                                        (size_t)code->string_capacity * sizeof *code->strings);
                if (!code->strings) abort();
            }
            code->strings[code->string_count] = expr;
            emit(c, OP_STRING)->a = code->string_count++;
            return;
        }
        case EX_NAME: case EX_APPLY: case EX_INDEX: case EX_FIELD:
            if (expr->kind == EX_NAME && expr->symbol && expr->symbol->kind == SYM_STATE) {
                constant(c, (Value){.kind = V_INT, .as.i = (int32_t)expr->symbol->value});
                return;
            }
            if (expr->kind == EX_NAME && expr->symbol && expr->symbol->kind == SYM_NOW) {
                emit(c, OP_NOW);
                note(c, expr);
                return;
            }
            if (expr->kind == EX_NAME && expr->symbol && expr->symbol->kind == SYM_SENDER) {
                emit(c, OP_SENDER);
                return;
            }
            if (expr->is_call) {
                call(c, expr);
                if (expr->type->kind != TY_VOID) note(c, expr);
                return;
            }
            address(c, expr);
            if (is_array(expr->type) || expr->type->kind == TY_RECORD) return;
            {
                Instr *in = emit(c, OP_LOAD);
                in->expr = expr;
                in->type = expr->type;
            }
            note(c, expr);
            return;
        case EX_UNARY:
            if (expr->op == TOK_AMP) {
                address(c, expr->base);
            } else if (expr->op == TOK_AT) {
                if (expr->base->symbol && expr->base->symbol->by_reference)
                    emit(c, OP_ADDR)->symbol = expr->base->symbol;
                else
                    value(c, expr->base);
                return;
            } else if (expr->op == TOK_STAR) {
                value(c, expr->base);
                if (expr->type->kind == TY_RECORD || is_array(expr->type)) return;
                Instr *in = emit(c, OP_LOAD);
                in->expr = expr;
                in->type = expr->type;
            } else {
                value(c, expr->base);
                Instr *in = emit(c, OP_UNARY);
                in->a = (int)expr->op;
                in->expr = expr;
            }
            note(c, expr);
            return;
        case EX_BINARY:
            binary(c, expr);
            note(c, expr);
            return;
    }
}

/* Where a component is, given the component or a pointer to it. */
static void place(Compiler *c, const Expr *expr) {
    if (expr->type->kind == TY_VOID) constant(c, (Value){.kind = V_POINTER}); /* "test" */
    else if (expr->type->kind == TY_POINTER) value(c, expr);
    else address(c, expr);
}

/* ---- statements ---- */

static void mark(Compiler *c, const Stmt *stmt, int phase) {
    Instr *in = emit(c, OP_STMT);
    in->stmt = stmt;
    in->a = phase;
}

static void store(Compiler *c, const Stmt *stmt, const Expr *target, Type *from) {
    (void)from;
    if (target->kind == EX_UNARY && target->op == TOK_AT && target->base->symbol &&
        target->base->symbol->by_reference) {
        Instr *in = emit(c, OP_REBIND);
        in->symbol = target->base->symbol;
        in->stmt = stmt;
        return;
    }
    if (target->type->kind == TY_RECORD) {
        address(c, target);
        Instr *in = emit(c, OP_COPY);
        in->a = vm_slots(c->config, target->type);
        in->stmt = stmt;
        in->expr = target;
        return;
    }
    convert(c, target->type);
    address(c, target);
    Instr *in = emit(c, OP_STORE);
    in->stmt = stmt;
    in->expr = target;
    in->type = target->type;
}

static void condition(Compiler *c, const Stmt *stmt) {
    Instr *in = emit(c, OP_CONDITION);
    in->stmt = stmt;
    in->expr = stmt->value;
}

static void allocate(Compiler *c, const VarDecl *vars) {
    for (; vars; vars = vars->next) {
        Symbol *sym = vars->symbol;
        int rank = 0;
        if (is_array(sym->type)) {
            rank = sym->type->dims.count;
            for (int i = 0; i < rank; i++) {
                const Dim *dim = &sym->type->dims.items[i];
                if (dim->lo) value(c, dim->lo);
                value(c, dim->hi);
            }
        }
        Instr *in = emit(c, OP_ALLOC);
        in->symbol = sym;
        in->a = rank;
    }
}

static void branch(Compiler *c, const Stmt *stmt) {
    emit(c, OP_BLOCK);
    statements(c, stmt);
    emit(c, OP_BLOCK_END);
}

static void if_statement(Compiler *c, const Stmt *stmt) {
    int ends[64], n = 0;
    int *exits = ends, capacity = 64;
    for (const Stmt *b = stmt; b; b = b->otherwise) {
        if (b->kind != ST_IF) {
            branch(c, b->body);
            break;
        }
        mark(c, b, PHASE_WHOLE);
        value(c, b->value);
        condition(c, b);
        int next = jump(c, OP_JUMP_IF_FALSE, 0);
        branch(c, b->body);
        if (b->otherwise) {
            if (n == capacity) {
                int *grown = malloc((size_t)capacity * 2 * sizeof(int));
                if (!grown) abort();
                memcpy(grown, exits, (size_t)n * sizeof(int));
                if (exits != ends) free(exits);
                exits = grown;
                capacity *= 2;
            }
            exits[n++] = jump(c, OP_JUMP, 0);
        }
        patch(c, next);
    }
    for (int i = 0; i < n; i++) patch(c, exits[i]);
    if (exits != ends) free(exits);
}

static void for_statement(Compiler *c, const Stmt *stmt) {
    mark(c, stmt, PHASE_WHOLE);
    value(c, stmt->value);
    store(c, stmt, stmt->target, stmt->value->type);

    int top = here(c);
    mark(c, stmt, PHASE_CONDITION);
    long long step = 1;
    bool known = !stmt->step || const_int(stmt->step, &step);
    bool negative = known ? step < 0
                          : stmt->step->kind == EX_UNARY && stmt->step->op == TOK_MINUS &&
                            stmt->step->base->kind == EX_FLOAT;
    bool sign_known = known || negative || stmt->step->kind == EX_FLOAT;
    if (!sign_known) value(c, stmt->step);
    value(c, stmt->target);
    value(c, stmt->limit);
    Instr *in = emit(c, OP_BINARY);
    in->a = sign_known ? (int)(negative ? TOK_GE : TOK_LE) : -1; /* -1: by the sign below */
    in->stmt = stmt;
    in = emit(c, OP_CONDITION);
    in->stmt = stmt;
    int out = jump(c, OP_JUMP_IF_FALSE, 0);

    branch(c, stmt->body);
    mark(c, stmt, PHASE_STEP);
    value(c, stmt->target);
    if (stmt->step) value(c, stmt->step);
    else constant(c, (Value){.kind = V_INT, .as.i = 1});
    in = emit(c, OP_BINARY);
    in->a = (int)TOK_PLUS;
    in->stmt = stmt;
    store(c, stmt, stmt->target, stmt->target->type);
    jump(c, OP_JUMP, top);
    patch(c, out);
}

static void return_statement(Compiler *c, const Stmt *stmt) {
    const Symbol *sym = c->function->symbol;
    if (sym->type->kind != TY_VOID) {
        if (stmt && stmt->value) {
            value(c, stmt->value);
            convert(c, sym->type);
        } else {
            emit(c, OP_ADDR)->symbol = sym->result;
            if (sym->type->kind != TY_RECORD) {
                Instr *in = emit(c, OP_LOAD);
                in->type = sym->type;
            }
        }
    }
    Instr *in = emit(c, OP_RETURN);
    in->type = sym->type;
    in->stmt = stmt;
}

static void statement(Compiler *c, const Stmt *stmt) {
    Instr *in;
    switch (stmt->kind) {
        case ST_IF:
            if_statement(c, stmt);
            return;
        case ST_FOR:
            for_statement(c, stmt);
            return;
        case ST_WHILE: {
            int top = here(c);
            mark(c, stmt, PHASE_CONDITION);
            value(c, stmt->value);
            condition(c, stmt);
            int out = jump(c, OP_JUMP_IF_FALSE, 0);
            branch(c, stmt->body);
            jump(c, OP_JUMP, top);
            patch(c, out);
            return;
        }
        case ST_REPEAT: {
            int top = here(c);
            branch(c, stmt->body);
            mark(c, stmt, PHASE_CONDITION);
            value(c, stmt->value);
            condition(c, stmt);
            jump(c, OP_JUMP_IF_FALSE, top);
            return;
        }
        case ST_BLOCK:
            branch(c, stmt->body);
            return;
        default:
            break;
    }
    mark(c, stmt, PHASE_WHOLE);
    switch (stmt->kind) {
        case ST_ASSIGN:
            value(c, stmt->value);
            store(c, stmt, stmt->target, stmt->value->type);
            break;
        case ST_CALL:
            call(c, stmt->value);
            if (stmt->value->type->kind != TY_VOID) emit(c, OP_POP);
            break;
        case ST_RETURN:
            return_statement(c, stmt);
            break;
        case ST_PRINT:
            for (int i = 0; i < stmt->args.count; i++) value(c, stmt->args.items[i]);
            in = emit(c, OP_PRINT);
            in->a = stmt->args.count;
            in->stmt = stmt;
            break;
        case ST_READ:
            for (int i = 0; i < stmt->args.count; i++) {
                address(c, stmt->args.items[i]);
                in = emit(c, OP_READ);
                in->expr = stmt->args.items[i];
                in->type = stmt->args.items[i]->type;
                in->stmt = stmt;
            }
            break;
        case ST_VAR:
            allocate(c, stmt->vars);
            break;
        case ST_SEND: {
            const Symbol *handler = stmt->value->symbol;
            for (int i = 0; i < stmt->value->args.count; i++) {
                value(c, stmt->value->args.items[i]);
                convert(c, handler->params[i + 1]->type);
            }
            if (!stmt->to_all) place(c, stmt->target);
            if (stmt->limit) value(c, stmt->limit);
            else constant(c, (Value){.kind = V_INT});
            in = emit(c, OP_SEND);
            in->a = stmt->value->args.count;
            in->stmt = stmt;
            in->symbol = handler;
            break;
        }
        case ST_STOP: emit(c, OP_STOP)->stmt = stmt; break;
        case ST_CANCEL: emit(c, OP_CANCEL)->stmt = stmt; break;
        case ST_CHECK:
            value(c, stmt->value);
            emit(c, OP_TRUTH);
            in = emit(c, OP_CHECK);
            in->stmt = stmt;
            in->expr = stmt->value;
            break;
        case ST_RUN:
            if (stmt->value) value(c, stmt->value);
            else constant(c, (Value){.kind = V_INT, .as.i = -1});
            emit(c, OP_RUN)->stmt = stmt;
            in = emit(c, OP_LOOP);
            in->a = 1;
            in->stmt = stmt;
            break;
        case ST_EXPECT:
            place(c, stmt->target);
            place(c, stmt->limit);
            emit(c, OP_EXPECT)->stmt = stmt;
            break;
        case ST_STEP: emit(c, OP_STEP)->stmt = stmt; break;
        case ST_TODO: emit(c, OP_TODO)->stmt = stmt; break;
        default:
            break;
    }
}

static void statements(Compiler *c, const Stmt *stmt) {
    for (; stmt; stmt = stmt->next) statement(c, stmt);
}

/* ---- functions ---- */

static void number_vars(Compiler *c, const VarDecl *vars) {
    for (; vars; vars = vars->next)
        if (vars->symbol) vars->symbol->slot = c->slots++;
}

static void number_statement_vars(Compiler *c, const Stmt *stmt) {
    for (; stmt; stmt = stmt->next) {
        number_vars(c, stmt->vars);
        number_statement_vars(c, stmt->body);
        for (const Stmt *o = stmt->otherwise; o; o = o->otherwise) number_statement_vars(c, o->body);
    }
}

static void function(Compiler *c, const Function *f) {
    Symbol *sym = f->symbol;
    c->function = f;
    c->slots = 0;
    for (int i = 0; i < sym->param_count; i++) sym->params[i]->slot = c->slots++;
    if (sym->result) sym->result->slot = c->slots++;
    number_vars(c, f->locals);
    number_statement_vars(c, f->body);

    int index = c->code->function_count++;
    c->code->functions[index].function = f;
    c->code->functions[index].entry = here(c);
    c->code->functions[index].vars = c->slots;

    for (int i = 0; i < sym->param_count; i++) {
        Symbol *param = sym->params[i];
        if (!is_array(param->type)) continue;
        for (int d = 0; d < param->type->dims.count; d++) {
            const Dim *dim = &param->type->dims.items[d];
            if (dim->lo) value(c, dim->lo);
            if (dim->hi) value(c, dim->hi);
        }
        Instr *in = emit(c, OP_PARAM_BOUNDS);
        in->symbol = param;
        in->a = param->type->dims.count;
    }
    if (sym->result) emit(c, OP_ALLOC)->symbol = sym->result;
    allocate(c, f->locals);
    emit(c, OP_ENTER);
    statements(c, f->body);
    return_statement(c, NULL);
}

static void number_fields(const Config *config, const Program *program) {
    for (const Record *r = program->records; r; r = r->next) {
        int slot = 0;
        for (const VarDecl *f = r->fields; f; f = f->next) {
            f->symbol->slot = slot;
            slot += vm_slots(config, f->symbol->type);
        }
    }
}

void compile(const Program *program, const Config *config, Code *code) {
    Compiler compiler = {.code = code, .config = config};
    Compiler *c = &compiler;
    memset(code, 0, sizeof *code);

    number_fields(config, program);
    int count = 0;
    for (const Function *f = program->functions; f; f = f->next) count++;
    code->functions = calloc((size_t)count + 1, sizeof *code->functions);
    if (!code->functions) abort();

    number_vars(c, program->globals);
    code->global_vars = c->slots;
    for (const Function *f = program->functions; f; f = f->next) function(c, f);

    c->function = NULL;
    code->start = here(c);
    allocate(c, program->globals);
    for (const Link *link = program->links; link; link = link->next) {
        value(c, link->from); /* a component, or an array of them */
        value(c, link->to);
        if (link->delay) value(c, link->delay);
        else constant(c, (Value){.kind = V_INT, .as.i = 1});
        if (link->delay_max) value(c, link->delay_max);
        else constant(c, (Value){.kind = V_INT, .as.i = -1});
        if (link->loss) value(c, link->loss);
        else constant(c, (Value){.kind = V_INT});
        Instr *in = emit(c, OP_LINK);
        in->a = vm_slots(config, link->from->type);
        in->b = vm_slots(config, link->to->type);
        in->value.as.i = link->both_ways;
    }
    if (program->seed) {
        value(c, program->seed);
        emit(c, OP_SEED);
    }
    code->ready = here(c);
    emit(c, OP_READY);
    const Function *main_function = NULL;
    for (const Function *f = program->functions; f; f = f->next)
        if (!f->is_test && !f->owner && strcmp(f->symbol->c_name, "main") == 0) main_function = f;
    if (main_function) {
        Instr *in = emit(c, OP_CALL);
        in->symbol = main_function->symbol;
    }
    if (has_components(program)) emit(c, OP_LOOP);
    emit(c, OP_END);
}

int code_entry(const Code *code, const Function *f, int *vars) {
    for (int i = 0; i < code->function_count; i++) {
        if (code->functions[i].function == f) {
            *vars = code->functions[i].vars;
            return code->functions[i].entry;
        }
    }
    return -1;
}

void code_free(Code *code) {
    free(code->items);
    free(code->functions);
    free(code->strings);
    memset(code, 0, sizeof *code);
}
