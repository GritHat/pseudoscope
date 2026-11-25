#include "plike/vm.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 20000
#define MAX_RANK 16
#define LOG_SIZE 8192
#define MAX_NODES 100000
#define MAX_SNAPSHOT_BYTES (64u << 20)

/* A growable array that lives in the state, so that snapshots can copy it. */
#define ARRAY(type) struct { type *items; int count, capacity; }

/* Everything that going back must restore. */
typedef struct {
    ARRAY(Value) cells;
    ARRAY(Object) objects;
    ARRAY(Bounds) bounds;
    ARRAY(Var) vars;
    ARRAY(Frame) frames;
    ARRAY(Value) stack;
    ARRAY(Eval) evals;
    int pc;
    VmStatus status;
    const char *message;
    int exit_status;
    size_t input_pos, output_length;
    long log_count;
    int node_count, node;
    long long steps;
    uint32_t serial;
} State;

typedef struct {
    State state;
    size_t bytes;
} Snapshot;

struct Vm {
    const Program *program;
    const Config *config;
    Code code;
    State s;

    /* Append-only: a replay writes the same things again. */
    char *output;
    size_t output_capacity;
    char *input;
    size_t input_length;
    bool input_ended;
    LogEntry *log;
    CallNode *nodes;
    int node_capacity;

    ARRAY(Touch) touched;
    Level event;      /* of the instruction just executed */
    Level last_stop;
    int trial_base, trial_count;

    Snapshot *snapshots;
    int snapshot_count, snapshot_capacity;
    size_t snapshot_bytes;
    long long interval;
};

static void *grow(void *items, int *capacity, int needed, size_t size) {
    if (needed <= *capacity) return items;
    int wanted = *capacity ? *capacity : 64;
    while (wanted < needed) wanted *= 2;
    items = realloc(items, (size_t)wanted * size);
    if (!items) {
        fputs("plike: out of memory\n", stderr);
        exit(3);
    }
    *capacity = wanted;
    return items;
}

#define RESERVE(array, extra)                                                                      \
    ((array).items = grow((array).items, &(array).capacity, (array).count + (extra),               \
                          sizeof *(array).items))
#define PUSH(array, ...) (RESERVE(array, 1), (array).items[(array).count++] = (__VA_ARGS__))

/* ---- sizes ---- */

static long const_extent(const Config *config, const Dim *dim) {
    long long lo = config->index_base, hi = 0;
    if (!dim->hi || !const_int(dim->hi, &hi)) return 0;
    if (!dim->lo) return (long)hi;
    if (!const_int(dim->lo, &lo)) return 0;
    return (long)(hi - lo + 1);
}

int vm_slots(const Config *config, const Type *type) {
    int count = 0;
    switch (type->kind) {
        case TY_RECORD:
            for (const VarDecl *f = type->record->fields; f; f = f->next)
                count += vm_slots(config, f->symbol->type);
            return count;
        case TY_ARRAY:
            count = vm_slots(config, type->base);
            for (int i = 0; i < type->dims.count; i++)
                count *= (int)const_extent(config, &type->dims.items[i]);
            return count;
        default:
            return 1;
    }
}

int vm_sizeof(const Config *config, const Type *type, int *align) {
    int size = 4, a = 4;
    switch (type->kind) {
        case TY_BOOL: case TY_CHAR: size = a = 1; break;
        case TY_POINTER: case TY_STRING: size = a = 8; break;
        case TY_ARRAY:
            size = vm_sizeof(config, type->base, &a);
            for (int i = 0; i < type->dims.count; i++)
                size *= (int)const_extent(config, &type->dims.items[i]);
            break;
        case TY_RECORD: {
            size = 0;
            a = 1;
            for (const VarDecl *f = type->record->fields; f; f = f->next) {
                int fa, fs = vm_sizeof(config, f->symbol->type, &fa);
                if (fa > a) a = fa;
                size = (size + fa - 1) / fa * fa + fs;
            }
            size = (size + a - 1) / a * a;
            break;
        }
        default: break;
    }
    if (align) *align = a;
    return size;
}

const Type *vm_slot_type(const Config *config, const Type *type, int slot, int *byte) {
    if (type->kind == TY_ARRAY) {
        int per = vm_slots(config, type->base);
        int size = vm_sizeof(config, type->base, NULL);
        if (per <= 0) return type->base;
        if (byte) *byte += slot / per * size;
        return vm_slot_type(config, type->base, slot % per, byte);
    }
    if (type->kind == TY_RECORD) {
        int offset = 0;
        for (const VarDecl *f = type->record->fields; f; f = f->next) {
            int align, size = vm_sizeof(config, f->symbol->type, &align);
            int count = vm_slots(config, f->symbol->type);
            offset = (offset + align - 1) / align * align;
            if (slot < count) {
                if (byte) *byte += offset;
                return vm_slot_type(config, f->symbol->type, slot, byte);
            }
            slot -= count;
            offset += size;
        }
    }
    return type;
}

/* ---- memory ---- */

static const Object *object_of(const Vm *vm, Pointer p) {
    if (p.object <= 0 || p.object >= vm->s.objects.count) return NULL;
    const Object *object = &vm->s.objects.items[p.object];
    return object->serial == p.serial ? object : NULL;
}

static Value *cell_of(const Vm *vm, Pointer p) {
    const Object *object = object_of(vm, p);
    if (!object || p.offset < 0 || p.offset >= object->count) return NULL;
    return &vm->s.cells.items[object->base + p.offset];
}

static void add_log(Vm *vm, LogEntry entry) {
    entry.number = vm->s.log_count;
    entry.depth = vm->s.frames.count;
    vm->log[vm->s.log_count % LOG_SIZE] = entry;
    vm->s.log_count++;
}

static const Stmt *current_stmt(const Vm *vm) {
    return vm->s.frames.count ? vm->s.frames.items[vm->s.frames.count - 1].stmt : NULL;
}

static void fail(Vm *vm, const char *message) {
    if (vm->s.status == VM_FAILED) return;
    vm->s.status = VM_FAILED;
    vm->s.message = message;
    vm->s.exit_status = 1;
    vm->event = LEVEL_CALL;
    add_log(vm, (LogEntry){.kind = LOG_ERROR, .text = message, .stmt = current_stmt(vm)});
}

static void warn(Vm *vm, const char *message, const Expr *expr) {
    add_log(vm, (LogEntry){.kind = LOG_WARNING, .text = message, .stmt = current_stmt(vm), .expr = expr});
}

static int frame_index(const Vm *vm) { return vm->s.frames.count - 1; }
static Frame *top_frame(Vm *vm) { return &vm->s.frames.items[vm->s.frames.count - 1]; }

static Pointer new_object(Vm *vm, const Symbol *symbol, Type *type, int count, bool zero) {
    State *s = &vm->s;
    Object object = {.base = s->cells.count, .count = count, .serial = ++s->serial, .symbol = symbol,
                     .type = type, .frame = frame_index(vm)};
    int align = 4, size = 4 * count;
    if (type) {
        const Type *base = type->kind == TY_ARRAY ? type->base : type;
        int per = vm_slots(vm->config, base);
        size = vm_sizeof(vm->config, base, &align) * (per > 0 ? count / per : 0);
    }
    const Object *last = s->objects.count > 1 ? &s->objects.items[s->objects.count - 1] : NULL;
    uint32_t top = last ? last->address : 0x7ffff000u;
    object.address = (top - (uint32_t)size) / (uint32_t)align * (uint32_t)align;
    RESERVE(s->cells, count);
    for (int i = 0; i < count; i++) {
        int byte = 0;
        const Type *slot = type ? vm_slot_type(vm->config, type, i, &byte) : NULL;
        Value *cell = &s->cells.items[s->cells.count++];
        memset(cell, 0, sizeof *cell);
        cell->kind = !slot ? V_INT : slot->kind == TY_REAL ? V_FLOAT
                   : slot->kind == TY_POINTER || slot->kind == TY_STRING ? V_POINTER : V_INT;
        cell->known = zero;
    }
    PUSH(s->objects, object);
    return (Pointer){s->objects.count - 1, 0, object.serial};
}

static void drop_objects(Vm *vm, int count) {
    State *s = &vm->s;
    if (count >= s->objects.count) return;
    s->cells.count = s->objects.items[count].base;
    s->objects.count = count;
}

static void touch(Vm *vm, Pointer at, bool write) {
    if (vm->touched.count < 4096) PUSH(vm->touched, (Touch){at, write});
    if (vm->event < LEVEL_MEMORY) vm->event = LEVEL_MEMORY;
}

static Value *access(Vm *vm, Pointer p) {
    if (p.object == 0) {
        fail(vm, "null pointer");
        return NULL;
    }
    if (!object_of(vm, p)) {
        fail(vm, "the variable pointed to no longer exists");
        return NULL;
    }
    Value *cell = cell_of(vm, p);
    if (!cell) fail(vm, "access outside of the variable");
    return cell;
}

/* ---- stack ---- */

static void push(Vm *vm, Value v) { PUSH(vm->s.stack, v); }

static Value pop(Vm *vm) {
    if (vm->s.stack.count == 0) {
        fail(vm, "internal error: empty stack");
        return (Value){0};
    }
    return vm->s.stack.items[--vm->s.stack.count];
}

static Value make_int(int32_t i) { return (Value){.kind = V_INT, .known = true, .as.i = i}; }
static Value make_pointer(Pointer p) { return (Value){.kind = V_POINTER, .known = true, .as.p = p}; }

static double as_double(Value v) {
    return v.kind == V_INT ? v.as.i : v.kind == V_FLOAT ? (double)v.as.f
         : v.kind == V_DOUBLE ? v.as.d : 0;
}

static bool truth(Value v) {
    return v.kind == V_POINTER ? v.as.p.object != 0 : as_double(v) != 0;
}

static int32_t as_int(Vm *vm, Value v) {
    if (v.kind == V_INT) return v.as.i;
    if (v.kind == V_POINTER) return v.as.p.object;
    double d = as_double(v);
    if (d >= 2147483648.0 || d <= -2147483649.0 || d != d) {
        warn(vm, "the number does not fit an integer", NULL);
        return INT_MIN;
    }
    return (int32_t)d;
}

static Value converted(Vm *vm, Value v, const Type *to) {
    switch (to->kind) {
        case TY_INT:
            v = (Value){.kind = V_INT, .known = v.known, .as.i = as_int(vm, v)};
            break;
        case TY_BOOL:
            v = (Value){.kind = V_INT, .known = v.known, .as.i = truth(v)};
            break;
        case TY_CHAR:
            v = (Value){.kind = V_INT, .known = v.known, .as.i = (int8_t)as_int(vm, v)};
            break;
        case TY_REAL: {
            float f = v.kind == V_FLOAT ? v.as.f : v.kind == V_INT ? (float)v.as.i : (float)v.as.d;
            v = (Value){.kind = V_FLOAT, .known = v.known, .as.f = f};
            break;
        }
        case TY_POINTER:
            if (v.kind != V_POINTER) /* only the constant 0 gets here */
                v = (Value){.kind = V_POINTER, .known = v.known, .as.p = {0, 0, 0}};
            break;
        default: break;
    }
    return v;
}

static int pointed_slots(Vm *vm, const Expr *side) {
    const Type *type = side->type;
    if (type->kind != TY_POINTER) return 1;
    return vm_slots(vm->config, type->base);
}

static Value pointer_arithmetic(Vm *vm, TokenKind op, Value a, Value b, const Expr *expr) {
    Value pointer = a.kind == V_POINTER ? a : b;
    Value number = a.kind == V_POINTER ? b : a;
    int per = expr && expr->base ? pointed_slots(vm, a.kind == V_POINTER ? expr->base : expr->right) : 1;
    int32_t n = number.as.i * per;
    pointer.as.p.offset += op == TOK_MINUS ? -n : n;
    return pointer;
}

static Value compare(TokenKind op, int order, bool unordered) {
    bool r = false;
    switch (op) {
        case TOK_EQ: r = !unordered && order == 0; break;
        case TOK_NE: r = unordered || order != 0; break;
        case TOK_LT: r = !unordered && order < 0; break;
        case TOK_GT: r = !unordered && order > 0; break;
        case TOK_LE: r = !unordered && order <= 0; break;
        case TOK_GE: r = !unordered && order >= 0; break;
        default: break;
    }
    return make_int(r);
}

static Value binary(Vm *vm, TokenKind op, Value a, Value b, const Expr *expr) {
    bool comparison = op == TOK_EQ || op == TOK_NE || op == TOK_LT || op == TOK_GT ||
                      op == TOK_LE || op == TOK_GE;
    if (a.kind == V_POINTER || b.kind == V_POINTER) {
        if (!comparison) return pointer_arithmetic(vm, op, a, b, expr);
        Pointer zero = {0, 0, 0};
        Pointer x = a.kind == V_POINTER ? a.as.p : zero, y = b.kind == V_POINTER ? b.as.p : zero;
        int order = x.object != y.object ? (x.object < y.object ? -1 : 1)
                  : x.offset != y.offset ? (x.offset < y.offset ? -1 : 1)
                  : x.object && x.serial != y.serial ? 1 : 0;
        return compare(op, order, false);
    }
    if (a.kind == V_INT && b.kind == V_INT) {
        int64_t x = a.as.i, y = b.as.i, r = 0;
        if (comparison) return compare(op, x < y ? -1 : x > y, false);
        switch (op) {
            case TOK_PLUS: r = x + y; break;
            case TOK_MINUS: r = x - y; break;
            case TOK_STAR: r = x * y; break;
            case TOK_SLASH: case TOK_MOD:
                if (y == 0) {
                    fail(vm, "division by zero");
                    return a;
                }
                r = op == TOK_SLASH ? x / y : x % y;
                break;
            case TOK_AMP: r = x & y; break;
            case TOK_PIPE: r = x | y; break;
            case TOK_CARET: r = x ^ y; break;
            case TOK_SHL: case TOK_SHR:
                if (y < 0 || y > 31) {
                    fail(vm, "shift by a negative number, or by more bits than there are");
                    return a;
                }
                if (op == TOK_SHL) r = (int32_t)((uint32_t)x << y);
                else r = x >> y;
                break;
            default: break;
        }
        if (r > INT_MAX || r < INT_MIN) warn(vm, "integer overflow", expr);
        return make_int((int32_t)(uint32_t)(uint64_t)r);
    }
    if (a.kind == V_DOUBLE || b.kind == V_DOUBLE) {
        double x = as_double(a), y = as_double(b), r = 0;
        if (comparison) return compare(op, x < y ? -1 : x > y, x != x || y != y);
        r = op == TOK_PLUS ? x + y : op == TOK_MINUS ? x - y : op == TOK_STAR ? x * y : x / y;
        return (Value){.kind = V_DOUBLE, .known = true, .as.d = r};
    }
    float x = a.kind == V_INT ? (float)a.as.i : a.as.f, y = b.kind == V_INT ? (float)b.as.i : b.as.f;
    if (comparison) return compare(op, x < y ? -1 : x > y, x != x || y != y);
    float r = op == TOK_PLUS ? x + y : op == TOK_MINUS ? x - y : op == TOK_STAR ? x * y : x / y;
    return (Value){.kind = V_FLOAT, .known = true, .as.f = r};
}

static Value unary(Vm *vm, TokenKind op, Value v) {
    switch (op) {
        case TOK_NOT: return make_int(!truth(v));
        case TOK_TILDE: return make_int(~v.as.i);
        case TOK_MINUS:
            if (v.kind == V_INT) {
                if (v.as.i == INT_MIN) warn(vm, "integer overflow", NULL);
                return make_int((int32_t)(0u - (uint32_t)v.as.i));
            }
            if (v.kind == V_FLOAT) v.as.f = -v.as.f;
            else v.as.d = -v.as.d;
            return v;
        default: return v;
    }
}

static Value builtin(Vm *vm, const char *name, Value *args, const Expr *expr) {
    if (strcmp(name, "abs") == 0 && expr->type->kind == TY_INT) {
        int32_t i = as_int(vm, args[0]);
        return make_int(i < 0 ? (int32_t)(0u - (uint32_t)i) : i);
    }
    double x = as_double(args[0]), r = 0;
    if (strcmp(name, "sqrt") == 0) r = sqrt(x);
    else if (strcmp(name, "sin") == 0) r = sin(x);
    else if (strcmp(name, "cos") == 0) r = cos(x);
    else if (strcmp(name, "tan") == 0) r = tan(x);
    else if (strcmp(name, "exp") == 0) r = exp(x);
    else if (strcmp(name, "log") == 0) r = log(x);
    else if (strcmp(name, "floor") == 0) r = floor(x);
    else if (strcmp(name, "ceil") == 0) r = ceil(x);
    else if (strcmp(name, "pow") == 0) r = pow(x, as_double(args[1]));
    else r = fabs(x);
    return (Value){.kind = V_DOUBLE, .known = true, .as.d = r};
}

/* ---- variables ---- */

static Var *var_of(Vm *vm, const Symbol *symbol) {
    const Frame *frame = symbol->is_global ? &vm->s.frames.items[0] : top_frame(vm);
    return &vm->s.vars.items[frame->vars + symbol->slot];
}

static int long_slots(Vm *vm, const Type *base, const Bounds *bounds, int rank) {
    long long count = vm_slots(vm->config, base);
    for (int i = 0; i < rank; i++) {
        long long extent = (long long)bounds[i].hi - bounds[i].lo + 1;
        if (extent < 0) return -1;
        count *= extent;
        if (count > 10000000) return -1;
    }
    return (int)count;
}

/* Pops the bounds of `type` (evaluated in order) into `out`. Open first
 * dimensions get hi = LONG_MAX, for the caller to fix. */
static void pop_bounds(Vm *vm, const Type *type, Bounds *out) {
    for (int i = type->dims.count - 1; i >= 0; i--) {
        const Dim *dim = &type->dims.items[i];
        long hi = LONG_MAX, lo = vm->config->index_base;
        if (dim->hi) hi = as_int(vm, pop(vm));
        if (dim->lo) lo = as_int(vm, pop(vm));
        else if (dim->hi) hi = lo + hi - 1; /* [n] counts elements */
        out[i] = (Bounds){lo, hi};
    }
}

static int save_bounds(Vm *vm, const Bounds *bounds, int rank) {
    int first = vm->s.bounds.count;
    for (int i = 0; i < rank; i++) PUSH(vm->s.bounds, bounds[i]);
    return first;
}

static void allocate(Vm *vm, const Symbol *symbol, int rank) {
    Type *type = symbol->type;
    int count, first = 0;
    if (type->kind == TY_ARRAY) {
        Bounds bounds[MAX_RANK];
        if (rank > MAX_RANK) {
            fail(vm, "too many dimensions");
            return;
        }
        pop_bounds(vm, type, bounds);
        count = long_slots(vm, type->base, bounds, rank);
        if (count <= 0) {
            fail(vm, "the size of the array is not valid");
            return;
        }
        first = save_bounds(vm, bounds, rank);
    } else {
        count = vm_slots(vm->config, type);
    }
    bool zero = symbol->is_global || symbol->kind == SYM_RESULT;
    Pointer at = new_object(vm, symbol, type, count, zero);
    Object *object = &vm->s.objects.items[at.object];
    object->bounds = first;
    object->rank = rank;
    *var_of(vm, symbol) = (Var){symbol, at, first, rank, true};
}

/* Bounds of an array parameter, evaluated on entry. An open first dimension
 * reaches the end of what the caller passed. */
static void param_bounds(Vm *vm, const Symbol *symbol) {
    Bounds bounds[MAX_RANK];
    Var *var = var_of(vm, symbol);
    int rank = symbol->type->dims.count;
    if (rank > MAX_RANK) {
        fail(vm, "too many dimensions");
        return;
    }
    pop_bounds(vm, symbol->type, bounds);
    if (bounds[0].hi == LONG_MAX) {
        const Object *object = object_of(vm, var->at);
        int stride = long_slots(vm, symbol->type->base, bounds + 1, rank - 1);
        long extent = object && stride > 0 ? (object->count - var->at.offset) / stride : 0;
        bounds[0].hi = bounds[0].lo + extent - 1;
    }
    var->bounds = save_bounds(vm, bounds, rank);
    var->rank = rank;
}

static void format_call(Vm *vm, const Function *function, char *out, size_t size);

static void enter_function(Vm *vm, const Function *function, const Expr *expr, int return_pc) {
    State *s = &vm->s;
    const Symbol *symbol = function->symbol;
    if (s->frames.count >= MAX_FRAMES) {
        fail(vm, "too many nested calls: is the recursion ever going to end?");
        return;
    }
    int vars;
    int entry = code_entry(&vm->code, function, &vars);
    int first_arg = s->stack.count - symbol->param_count;
    Frame frame = {.function = function, .call = expr, .vars = s->vars.count, .var_count = vars,
                   .objects = s->objects.count, .bounds = s->bounds.count, .stack = first_arg,
                   .evals = s->evals.count, .return_pc = return_pc, .node = s->node};
    PUSH(s->frames, frame);
    RESERVE(s->vars, vars + 1);
    memset(&s->vars.items[s->vars.count], 0, (size_t)vars * sizeof(Var));
    s->vars.count += vars;

    for (int i = 0; i < symbol->param_count; i++) {
        const Symbol *param = symbol->params[i];
        Value arg = s->stack.items[first_arg + i];
        Var var = {.symbol = param, .live = true};
        if (param->type->kind == TY_ARRAY || param->by_reference) {
            var.at = arg.as.p;
        } else {
            int count = vm_slots(vm->config, param->type);
            var.at = new_object(vm, param, param->type, count, false);
            Value *cells = &s->cells.items[s->objects.items[var.at.object].base];
            if (param->type->kind == TY_RECORD) {
                const Object *from = object_of(vm, arg.as.p);
                if (!from || arg.as.p.offset + count > from->count) {
                    fail(vm, "the record passed does not exist");
                    return;
                }
                memcpy(cells, &s->cells.items[from->base + arg.as.p.offset], (size_t)count * sizeof(Value));
            } else {
                *cells = arg;
            }
        }
        s->vars.items[s->frames.items[s->frames.count - 1].vars + param->slot] = var;
    }
    s->stack.count = first_arg;

    if (s->node_count < MAX_NODES) {
        vm->nodes = grow(vm->nodes, &vm->node_capacity, s->node_count + 1, sizeof *vm->nodes);
        CallNode *node = &vm->nodes[s->node_count];
        *node = (CallNode){.parent = s->node, .function = function, .start = s->steps,
                           .end = LLONG_MAX, .depth = s->frames.count - 2};
        s->node = s->node_count++;
        top_frame(vm)->node = s->node;
    } else {
        top_frame(vm)->node = -1;
    }
    s->pc = entry;
}

/* At OP_ENTER, when the parameters have their bounds. */
static void announce_call(Vm *vm) {
    Frame *frame = top_frame(vm);
    LogEntry entry = {.kind = LOG_CALL, .function = frame->function, .stmt = NULL};
    format_call(vm, frame->function, entry.value, sizeof entry.value);
    add_log(vm, entry);
    if (frame->node >= 0)
        format_call(vm, frame->function, vm->nodes[frame->node].args, sizeof vm->nodes[0].args);
    vm->event = LEVEL_CALL;
}

static void leave_function(Vm *vm, const Type *type) {
    State *s = &vm->s;
    Frame frame = *top_frame(vm);
    bool has_value = type->kind != TY_VOID;
    Value result = has_value ? pop(vm) : (Value){0};
    Value *record = NULL;
    int record_slots = 0;
    if (type->kind == TY_RECORD) { /* survives the frame as a temporary */
        record_slots = vm_slots(vm->config, type);
        Value *from = access(vm, result.as.p);
        if (!from) return;
        record = malloc((size_t)record_slots * sizeof(Value) + 1);
        if (!record) abort();
        memcpy(record, from, (size_t)record_slots * sizeof(Value));
    }
    LogEntry entry = {.kind = LOG_RETURN, .function = frame.function, .stmt = frame.stmt, .truth = has_value};
    if (has_value && type->kind != TY_RECORD)
        vm_format(vm, &result, type, entry.value, sizeof entry.value);
    else if (has_value)
        snprintf(entry.value, sizeof entry.value, "{...}");
    if (frame.node >= 0) {
        CallNode *node = &vm->nodes[frame.node];
        node->end = s->steps;
        snprintf(node->result, sizeof node->result, "%.31s", entry.value);
        s->node = node->parent;
    }
    drop_objects(vm, frame.objects);
    s->bounds.count = frame.bounds;
    s->vars.count = frame.vars;
    s->stack.count = frame.stack;
    s->evals.count = frame.evals;
    s->frames.count--;
    add_log(vm, entry);
    s->pc = frame.return_pc;
    if (record) {
        Pointer at = new_object(vm, NULL, (Type *)type, record_slots, false);
        Object *object = &s->objects.items[at.object];
        object->is_temporary = true;
        memcpy(&s->cells.items[object->base], record, (size_t)record_slots * sizeof(Value));
        free(record);
        push(vm, make_pointer(at));
    } else if (has_value) {
        push(vm, result);
    } else if (s->frames.count == 1 && vm->code.items[s->pc].op == OP_END) {
        push(vm, make_int(0));
    }
    vm->event = LEVEL_CALL;
}

/* ---- indexing ---- */

static void index_into(Vm *vm, const Instr *in) {
    Value indices[MAX_RANK];
    if (in->a > MAX_RANK) {
        fail(vm, "too many dimensions");
        return;
    }
    for (int i = in->a - 1; i >= 0; i--) indices[i] = pop(vm);
    Pointer p = pop(vm).as.p;

    if (in->type->kind == TY_POINTER) {
        p.offset += (as_int(vm, indices[0]) - vm->config->index_base) *
                    vm_slots(vm->config, in->type->base);
        push(vm, make_pointer(p));
        return;
    }
    const Type *type = in->type;
    Bounds fixed[MAX_RANK];
    const Bounds *bounds;
    int rank = type->dims.count;
    if (rank > MAX_RANK) {
        fail(vm, "too many dimensions");
        return;
    }
    if (in->symbol) {
        const Var *var = var_of(vm, in->symbol);
        bounds = vm->s.bounds.items + var->bounds + in->b;
    } else { /* inside a record: constant */
        for (int i = 0; i < rank; i++) {
            long long lo = vm->config->index_base, hi = 0;
            const Dim *dim = &type->dims.items[i];
            if (dim->lo) const_int(dim->lo, &lo);
            if (dim->hi) const_int(dim->hi, &hi);
            if (!dim->lo) hi = lo + hi - 1;
            fixed[i] = (Bounds){(long)lo, (long)hi};
        }
        bounds = fixed;
    }
    long long stride = vm_slots(vm->config, type->base);
    long long strides[MAX_RANK];
    for (int i = rank - 1; i >= 0; i--) {
        strides[i] = stride;
        stride *= bounds[i].hi - bounds[i].lo + 1;
    }
    for (int i = 0; i < in->a; i++) {
        long index = as_int(vm, indices[i]);
        if (index < bounds[i].lo || index > bounds[i].hi) {
            fail(vm, "array index out of bounds");
            return;
        }
        p.offset += (int32_t)((index - bounds[i].lo) * strides[i]);
    }
    push(vm, make_pointer(p));
}

/* ---- input and output ---- */

static void put_output(Vm *vm, const char *text, size_t length) {
    if (vm->s.output_length + length + 1 > vm->output_capacity) {
        while (vm->s.output_length + length + 1 > vm->output_capacity)
            vm->output_capacity = vm->output_capacity ? vm->output_capacity * 2 : 1024;
        vm->output = realloc(vm->output, vm->output_capacity);
        if (!vm->output) abort();
    }
    memcpy(vm->output + vm->s.output_length, text, length);
    vm->s.output_length += length;
    vm->output[vm->s.output_length] = '\0';
}

static void print_text(Vm *vm, Pointer p) {
    for (;;) {
        Value *cell = access(vm, p);
        if (!cell) {
            vm->s.message = "the text has no end: a character 0 is missing";
            return;
        }
        if (cell->as.i == 0) return;
        char c = (char)cell->as.i;
        put_output(vm, &c, 1);
        p.offset++;
    }
}

static void print_statement(Vm *vm, const Instr *in) {
    const Stmt *stmt = in->stmt;
    size_t start = vm->s.output_length;
    Value *args = &vm->s.stack.items[vm->s.stack.count - in->a];
    for (int i = 0; i < in->a && vm->s.status == VM_READY; i++) {
        char text[400];
        const Type *type = stmt->args.items[i]->type;
        Value v = args[i];
        if (i > 0) put_output(vm, " ", 1);
        text[0] = '\0';
        switch (type->kind) {
            case TY_REAL: snprintf(text, sizeof text, "%f", as_double(v)); break;
            case TY_CHAR: {
                char c = (char)v.as.i;
                put_output(vm, &c, 1);
                break;
            }
            case TY_STRING: case TY_ARRAY: print_text(vm, v.as.p); break;
            case TY_POINTER:
                if (type->base->kind == TY_CHAR) {
                    print_text(vm, v.as.p);
                } else {
                    const Object *object = object_of(vm, v.as.p);
                    if (object) snprintf(text, sizeof text, "0x%x", object->address + (unsigned)v.as.p.offset * 4);
                    else snprintf(text, sizeof text, "(nil)");
                }
                break;
            default: snprintf(text, sizeof text, "%d", v.as.i);
        }
        put_output(vm, text, strlen(text));
    }
    put_output(vm, "\n", 1);
    vm->s.stack.count -= in->a;
    LogEntry entry = {.kind = LOG_PRINT, .stmt = stmt};
    size_t length = vm->s.output_length - start - 1;
    snprintf(entry.value, sizeof entry.value, "%.*s", (int)(length > 47 ? 47 : length), vm->output + start);
    add_log(vm, entry);
}

static void place_text(const Vm *vm, const Expr *expr, char *out, size_t size);

/* Like scanf with %d, %f or " %c". Returns 1, 0 if the text does not match, or
 * -1 if there may be more to come. */
static int scan(Vm *vm, const Type *type, Value *out, size_t *next) {
    const char *text = vm->input ? vm->input : "";
    size_t i = vm->s.input_pos;
    while (i < vm->input_length && strchr(" \t\n\r\f\v", text[i])) i++;
    if (i >= vm->input_length) return vm->input_ended ? 0 : -1;
    if (type->kind == TY_CHAR) {
        *out = make_int((int8_t)text[i]);
        *next = i + 1;
        return 1;
    }
    char word[128];
    size_t n = 0;
    while (i + n < vm->input_length && n < sizeof word - 1 && !strchr(" \t\n\r\f\v", text[i + n])) {
        word[n] = text[i + n];
        n++;
    }
    word[n] = '\0';
    if (i + n >= vm->input_length && !vm->input_ended) return -1; /* maybe half a word */
    char *end;
    if (type->kind == TY_REAL) {
        float f = strtof(word, &end);
        *out = (Value){.kind = V_FLOAT, .known = true, .as.f = f};
    } else {
        long long v = strtoll(word, &end, 10);
        *out = make_int((int32_t)(uint32_t)(unsigned long long)v);
        if (type->kind == TY_BOOL) out->as.i = v != 0;
    }
    if (end == word) return 0;
    *next = i + (size_t)(end - word);
    return 1;
}

static void read_statement(Vm *vm, const Instr *in) {
    Value v = {0};
    size_t next = 0;
    int got = scan(vm, in->type, &v, &next);
    if (got < 0) {
        vm->s.status = VM_NEEDS_INPUT;
        vm->s.pc--; /* try again, as if this had not been a step */
        vm->s.steps--;
        vm->event = LEVEL_CALL;
        return;
    }
    if (got == 0) {
        fail(vm, "invalid input");
        return;
    }
    Pointer p = pop(vm).as.p;
    Value *cell = access(vm, p);
    if (!cell) return;
    vm->s.input_pos = next;
    *cell = v;
    touch(vm, p, true);
    LogEntry entry = {.kind = LOG_READ, .stmt = in->stmt, .expr = in->expr};
    place_text(vm, in->expr, entry.target, sizeof entry.target);
    vm_format(vm, &v, in->type, entry.value, sizeof entry.value);
    add_log(vm, entry);
}

/* ---- one instruction ---- */

static void store(Vm *vm, const Instr *in) {
    Pointer p = pop(vm).as.p;
    Value v = pop(vm);
    Value *cell = access(vm, p);
    if (!cell) return;
    LogEntry entry = {.kind = in->stmt && in->stmt->kind == ST_FOR ? LOG_LOOP : LOG_ASSIGN,
                      .stmt = in->stmt, .expr = in->expr, .truth = top_frame(vm)->phase == PHASE_STEP};
    place_text(vm, in->expr, entry.target, sizeof entry.target);
    vm_format(vm, cell, in->type, entry.old, sizeof entry.old);
    v.known = true;
    *cell = v;
    vm_format(vm, cell, in->type, entry.value, sizeof entry.value);
    touch(vm, p, true);
    add_log(vm, entry);
}

static void step(Vm *vm) {
    State *s = &vm->s;
    const Instr *in = &vm->code.items[s->pc++];
    vm->event = LEVEL_NONE;
    s->steps++;

    switch (in->op) {
        case OP_CONST: push(vm, in->value); break;
        case OP_STRING: push(vm, make_pointer((Pointer){in->a + 1, 0, s->objects.items[in->a + 1].serial})); break;
        case OP_ADDR: push(vm, make_pointer(var_of(vm, in->symbol)->at)); break;
        case OP_LOAD: {
            Pointer p = pop(vm).as.p;
            Value *cell = access(vm, p);
            if (!cell) break;
            if (!cell->known) warn(vm, "used before it was given a value", in->expr);
            Value v = *cell;
            v.known = true;
            push(vm, v);
            touch(vm, p, false);
            break;
        }
        case OP_STORE: store(vm, in); break;
        case OP_COPY: {
            Pointer to = pop(vm).as.p, from = pop(vm).as.p;
            const Object *a = object_of(vm, to), *b = object_of(vm, from);
            if (!access(vm, to) || !access(vm, from)) break;
            if (to.offset + in->a > a->count || from.offset + in->a > b->count) {
                fail(vm, "access outside of the variable");
                break;
            }
            memmove(&s->cells.items[a->base + to.offset], &s->cells.items[b->base + from.offset],
                    (size_t)in->a * sizeof(Value));
            for (int i = 0; i < in->a; i++)
                touch(vm, (Pointer){to.object, to.offset + i, to.serial}, true);
            LogEntry entry = {.kind = LOG_ASSIGN, .stmt = in->stmt, .expr = in->expr, .value = "{...}"};
            place_text(vm, in->expr, entry.target, sizeof entry.target);
            add_log(vm, entry);
            break;
        }
        case OP_REBIND: {
            Var *var = var_of(vm, in->symbol);
            var->at = pop(vm).as.p;
            break;
        }
        case OP_INDEX: index_into(vm, in); break;
        case OP_FIELD: {
            Value v = pop(vm);
            if (v.as.p.object == 0) {
                fail(vm, "null pointer");
                break;
            }
            v.as.p.offset += in->a;
            push(vm, v);
            break;
        }
        case OP_CONVERT: push(vm, converted(vm, pop(vm), in->type)); break;
        case OP_UNARY: push(vm, unary(vm, (TokenKind)in->a, pop(vm))); break;
        case OP_BINARY: {
            Value b = pop(vm), a = pop(vm);
            TokenKind op = (TokenKind)in->a;
            if (in->a < 0) op = as_double(pop(vm)) > 0 ? TOK_LE : TOK_GE;
            Value r = binary(vm, op, a, b, in->expr);
            if (s->status == VM_READY) push(vm, r);
            break;
        }
        case OP_TRUTH: push(vm, make_int(truth(pop(vm)))); break;
        case OP_BUILTIN: {
            s->stack.count -= in->a;
            push(vm, builtin(vm, in->symbol->builtin, &s->stack.items[s->stack.count], in->expr));
            break;
        }
        case OP_JUMP: s->pc = in->a; break;
        case OP_JUMP_IF_FALSE:
            if (!truth(pop(vm))) s->pc = in->a;
            break;
        case OP_JUMP_IF_TRUE:
            if (truth(pop(vm))) s->pc = in->a;
            break;
        case OP_AND: case OP_OR: {
            bool t = truth(pop(vm));
            if (t == (in->op == OP_OR)) {
                push(vm, make_int(t));
                s->pc = in->a;
            }
            break;
        }
        case OP_POP: pop(vm); break;
        case OP_CALL: enter_function(vm, in->symbol->function, in->expr, s->pc); break;
        case OP_RETURN: leave_function(vm, in->type); break;
        case OP_ENTER: announce_call(vm); break;
        case OP_PARAM_BOUNDS: param_bounds(vm, in->symbol); break;
        case OP_ALLOC: allocate(vm, in->symbol, in->a); break;
        case OP_BLOCK: {
            /* a marker object, to go back to */
            Object block = {.base = s->cells.count, .serial = ++s->serial, .frame = frame_index(vm),
                            .address = s->objects.items[s->objects.count - 1].address};
            PUSH(s->objects, block);
            break;
        }
        case OP_BLOCK_END: {
            int i = s->objects.count - 1;
            const Frame *frame = top_frame(vm);
            while (i >= frame->objects && (s->objects.items[i].type || s->objects.items[i].count)) i--;
            for (int v = 0; v < frame->var_count; v++) {
                Var *var = &s->vars.items[frame->vars + v];
                if (var->live && var->at.object >= i && object_of(vm, var->at) &&
                    object_of(vm, var->at)->symbol == var->symbol)
                    var->live = false;
            }
            drop_objects(vm, i);
            break;
        }
        case OP_STMT: {
            Frame *frame = top_frame(vm);
            while (s->objects.count > 1 && s->objects.items[s->objects.count - 1].is_temporary)
                drop_objects(vm, s->objects.count - 1);
            frame->stmt = in->stmt;
            frame->phase = in->a;
            s->evals.count = frame->evals;
            vm->event = LEVEL_STATEMENT;
            break;
        }
        case OP_EVAL:
            if (s->stack.count > 0) {
                PUSH(s->evals, (Eval){in->expr, s->stack.items[s->stack.count - 1]});
                vm->event = LEVEL_EXPRESSION;
            }
            break;
        case OP_CONDITION: {
            LogEntry entry = {.kind = LOG_CONDITION, .stmt = in->stmt, .expr = in->expr,
                              .truth = truth(s->stack.items[s->stack.count - 1])};
            add_log(vm, entry);
            break;
        }
        case OP_PRINT: print_statement(vm, in); break;
        case OP_READ: read_statement(vm, in); break;
        case OP_EXTERNAL: fail(vm, "this function is not defined in the file: it cannot be executed"); break;
        case OP_UNSUPPORTED: fail(vm, "this cannot be executed here"); break;
        case OP_END:
            s->pc--;
            s->status = VM_FINISHED;
            if (s->stack.count > 0) s->exit_status = as_int(vm, s->stack.items[s->stack.count - 1]) & 0xFF;
            add_log(vm, (LogEntry){.kind = LOG_END});
            vm->event = LEVEL_CALL;
            break;
    }
}

/* ---- snapshots ---- */

#define COPY_ARRAY(to, from)                                                                       \
    do {                                                                                           \
        (to).items = NULL;                                                                         \
        (to).capacity = 0;                                                                         \
        (to).count = 0;                                                                            \
        RESERVE(to, (from).count + 1);                                                             \
        if ((from).count) memcpy((to).items, (from).items, (size_t)(from).count * sizeof *(from).items); \
        (to).count = (from).count;                                                                 \
        bytes += (size_t)(to).capacity * sizeof *(to).items;                                       \
    } while (0)

#define RESTORE_ARRAY(to, from)                                                                    \
    do {                                                                                           \
        (to).count = 0;                                                                            \
        RESERVE(to, (from).count + 1);                                                             \
        if ((from).count) memcpy((to).items, (from).items, (size_t)(from).count * sizeof *(from).items); \
        (to).count = (from).count;                                                                 \
    } while (0)

#define EACH_ARRAY(X, a, b)                                                                        \
    X((a).cells, (b).cells);                                                                       \
    X((a).objects, (b).objects);                                                                   \
    X((a).bounds, (b).bounds);                                                                     \
    X((a).vars, (b).vars);                                                                         \
    X((a).frames, (b).frames);                                                                     \
    X((a).stack, (b).stack);                                                                       \
    X((a).evals, (b).evals)

#define FREE_ARRAY(a, b) free((a).items)

static size_t copy_state(State *to, const State *from) {
    size_t bytes = sizeof *to;
    *to = *from;
    EACH_ARRAY(COPY_ARRAY, *to, *from);
    return bytes;
}

static void restore_state(State *to, const State *from) {
    State keep = *to;
    *to = *from;
    to->cells = keep.cells;
    to->objects = keep.objects;
    to->bounds = keep.bounds;
    to->vars = keep.vars;
    to->frames = keep.frames;
    to->stack = keep.stack;
    to->evals = keep.evals;
    EACH_ARRAY(RESTORE_ARRAY, *to, *from);
}

static void free_state(State *state) { EACH_ARRAY(FREE_ARRAY, *state, *state); }

static void take_snapshot(Vm *vm) {
    if (vm->snapshot_bytes > MAX_SNAPSHOT_BYTES && vm->snapshot_count > 4) { /* keep one in two */
        int kept = 0;
        vm->snapshot_bytes = 0;
        for (int i = 0; i < vm->snapshot_count; i++) {
            if (i % 2 == 0) {
                vm->snapshot_bytes += vm->snapshots[i].bytes;
                vm->snapshots[kept++] = vm->snapshots[i];
            } else {
                free_state(&vm->snapshots[i].state);
            }
        }
        vm->snapshot_count = kept;
        vm->interval *= 2;
    }
    vm->snapshots = grow(vm->snapshots, &vm->snapshot_capacity, vm->snapshot_count + 1, sizeof *vm->snapshots);
    Snapshot *snapshot = &vm->snapshots[vm->snapshot_count++];
    snapshot->bytes = copy_state(&snapshot->state, &vm->s);
    vm->snapshot_bytes += snapshot->bytes;
}

static void maybe_snapshot(Vm *vm) {
    long long last = vm->snapshot_count ? vm->snapshots[vm->snapshot_count - 1].state.steps : -1;
    if (last < 0 || vm->s.steps >= last + vm->interval) take_snapshot(vm);
}

static int snapshot_before(const Vm *vm, long long steps) {
    int i = vm->snapshot_count - 1;
    while (i > 0 && vm->snapshots[i].state.steps > steps) i--;
    return i;
}

/* ---- running ---- */

static bool stops(const Vm *vm, Level level) {
    return vm->event != LEVEL_NONE && vm->event <= level;
}

Level vm_run(Vm *vm, Level level, long limit) {
    vm->touched.count = 0;
    while (limit-- > 0 && vm->s.status == VM_READY) {
        maybe_snapshot(vm);
        step(vm);
        if (stops(vm, level)) {
            vm->last_stop = vm->event;
            return vm->event;
        }
    }
    return LEVEL_NONE;
}

/* Replays up to `target`; `touched` ends up with what happened since the last
 * stop of `level`. */
static void replay_to(Vm *vm, long long target, Level level) {
    int i = snapshot_before(vm, target);
    bool nearer = vm->s.steps <= target && vm->s.steps >= vm->snapshots[i].state.steps;
    if (!nearer) restore_state(&vm->s, &vm->snapshots[i].state);
    vm->touched.count = 0;
    while (vm->s.steps < target && vm->s.status == VM_READY) {
        if (stops(vm, level)) vm->touched.count = 0;
        step(vm);
    }
    vm->last_stop = vm->event;
}

void vm_seek(Vm *vm, long long target) {
    if (vm->snapshot_count == 0) return;
    if (target < vm->snapshots[0].state.steps) target = vm->snapshots[0].state.steps;
    replay_to(vm, target, LEVEL_STATEMENT);
}

bool vm_back(Vm *vm, Level level) {
    long long now = vm->s.steps;
    if (vm->snapshot_count == 0 || now <= vm->snapshots[0].state.steps) return false;
    long long until = now;
    for (int i = snapshot_before(vm, now - 1); i >= 0; i--) {
        long long found = -1;
        restore_state(&vm->s, &vm->snapshots[i].state);
        while (vm->s.steps < until - 1 && vm->s.status == VM_READY) {
            step(vm);
            if (stops(vm, level)) found = vm->s.steps;
        }
        if (found >= 0) {
            restore_state(&vm->s, &vm->snapshots[i].state);
            vm->event = LEVEL_NONE;
            vm->touched.count = 0;
            while (vm->s.steps < found) {
                if (stops(vm, level)) vm->touched.count = 0;
                step(vm);
            }
            vm->last_stop = vm->event;
            return true;
        }
        until = vm->snapshots[i].state.steps + 1;
    }
    restore_state(&vm->s, &vm->snapshots[0].state);
    vm->touched.count = 0;
    vm->last_stop = LEVEL_NONE;
    return true;
}

/* ---- starting ---- */

Vm *vm_create(const Program *program, const Config *config) {
    Vm *vm = calloc(1, sizeof *vm);
    if (!vm) abort();
    vm->program = program;
    vm->config = config;
    vm->log = calloc(LOG_SIZE, sizeof *vm->log);
    if (!vm->log) abort();
    compile(program, config, &vm->code);
    return vm;
}

static void reset(Vm *vm) {
    for (int i = 0; i < vm->snapshot_count; i++) free_state(&vm->snapshots[i].state);
    vm->snapshot_count = 0;
    vm->snapshot_bytes = 0;
    vm->interval = 2000;
    free_state(&vm->s);
    memset(&vm->s, 0, sizeof vm->s);
    free(vm->input);
    vm->input = NULL;
    vm->input_length = 0;
    vm->input_ended = false;
    vm->touched.count = 0;
    vm->event = vm->last_stop = LEVEL_NONE;
    vm->trial_base = vm->trial_count = 0;

    State *s = &vm->s;
    s->node = -1;
    PUSH(s->objects, (Object){.address = 0x7ffff000u}); /* the null pointer */
    s->serial = 1;
    for (int i = 0; i < vm->code.string_count; i++) {
        const char *text = vm->code.strings[i]->text;
        Pointer at = new_object(vm, NULL, NULL, (int)strlen(text) + 1, true);
        Object *object = &s->objects.items[at.object];
        int n = 0;
        for (const char *c = text; *c; c++) {
            int code = (signed char)*c;
            if (*c == '\\' && c[1]) {
                static const char FROM[] = "ntrabfv0", TO[] = "\n\t\r\a\b\f\v";
                const char *found = strchr(FROM, *++c);
                code = found ? TO[found - FROM] : (signed char)*c;
            }
            s->cells.items[object->base + n++].as.i = code;
        }
    }
    Frame globals = {.vars = 0, .var_count = vm->code.global_vars, .objects = s->objects.count, .node = -1};
    PUSH(s->frames, globals);
    RESERVE(s->vars, vm->code.global_vars + 1);
    memset(s->vars.items, 0, (size_t)(vm->code.global_vars + 1) * sizeof(Var));
    s->vars.count = vm->code.global_vars;
    s->pc = vm->code.start;
}

static void set_up_globals(Vm *vm) {
    while (vm->code.items[vm->s.pc].op != OP_CALL && vm->code.items[vm->s.pc].op != OP_END) step(vm);
    vm->s.steps = 0;
}

void vm_start(Vm *vm) {
    reset(vm);
    set_up_globals(vm);
    take_snapshot(vm);
}

/* The value of an array bound, given the scalar arguments of a trial. */
static bool trial_int(const Function *f, const TrialArgument *args, const Expr *e, long long *out) {
    long long a, b;
    if (const_int(e, out)) return true;
    if (e->kind == EX_NAME && e->symbol && e->symbol->kind == SYM_PARAM) {
        const TrialArgument *arg = &args[e->symbol->position];
        if (arg->count < 1 || f->symbol->params[e->symbol->position]->type->kind == TY_ARRAY) return false;
        *out = (long long)arg->values[0];
        return true;
    }
    if (e->kind == EX_UNARY && e->op == TOK_MINUS && trial_int(f, args, e->base, &a)) {
        *out = -a;
        return true;
    }
    if (e->kind != EX_BINARY || !trial_int(f, args, e->base, &a) || !trial_int(f, args, e->right, &b))
        return false;
    if (a > 1000000 || a < -1000000 || b > 1000000 || b < -1000000) return false;
    switch (e->op) {
        case TOK_PLUS: *out = a + b; return true;
        case TOK_MINUS: *out = a - b; return true;
        case TOK_STAR: *out = a * b; return true;
        case TOK_SLASH: if (!b) return false; *out = a / b; return true;
        case TOK_MOD: if (!b) return false; *out = a % b; return true;
        default: return false;
    }
}

int vm_trial_size(const Config *config, const Function *f, const TrialArgument *args, int index, bool *open) {
    const Type *type = f->symbol->params[index]->type;
    *open = false;
    if (type->kind != TY_ARRAY) return vm_slots(config, type);
    long long count = vm_slots(config, type->base);
    for (int i = 0; i < type->dims.count; i++) {
        const Dim *dim = &type->dims.items[i];
        long long lo = config->index_base, hi;
        if (!dim->hi) {
            *open = true;
            continue;
        }
        if (!trial_int(f, args, dim->hi, &hi)) return -1;
        if (dim->lo && !trial_int(f, args, dim->lo, &lo)) return -1;
        long long extent = dim->lo ? hi - lo + 1 : hi;
        if (extent <= 0 || extent > 100000 || count * extent > 100000) return -1;
        count *= extent;
    }
    return (int)count;
}

bool vm_start_trial(Vm *vm, const Function *f, const TrialArgument *args) {
    const Symbol *symbol = f->symbol;
    reset(vm);
    set_up_globals(vm);
    State *s = &vm->s;
    vm->trial_base = s->vars.count;
    vm->trial_count = symbol->param_count;

    for (int i = 0; i < symbol->param_count; i++) {
        const Symbol *param = symbol->params[i];
        bool open;
        int count = vm_trial_size(vm->config, f, args, i, &open);
        if (count <= 0) return false;
        Var var = {.symbol = param, .live = true};
        if (open) {
            int rows = args[i].count / count + (args[i].count % count != 0);
            count *= rows > 0 ? rows : 1;
        }
        var.at = new_object(vm, param, param->type, count, false);
        if (param->type->kind == TY_ARRAY) {
            Bounds bounds[MAX_RANK];
            int rank = param->type->dims.count;
            if (rank > MAX_RANK) return false;
            long rest = vm_slots(vm->config, param->type->base);
            for (int d = rank - 1; d >= 0; d--) {
                const Dim *dim = &param->type->dims.items[d];
                long long lo = vm->config->index_base, hi = 0;
                if (dim->lo) trial_int(f, args, dim->lo, &lo);
                if (dim->hi) trial_int(f, args, dim->hi, &hi);
                if (!dim->lo) hi = lo + hi - 1;
                if (!dim->hi) hi = lo + count / rest - 1;
                bounds[d] = (Bounds){(long)lo, (long)hi};
                rest *= (long)(hi - lo + 1);
            }
            var.bounds = save_bounds(vm, bounds, rank);
            var.rank = rank;
            s->objects.items[var.at.object].bounds = var.bounds;
            s->objects.items[var.at.object].rank = rank;
        }
        Value *cells = &s->cells.items[s->objects.items[var.at.object].base];
        for (int k = 0; k < count; k++) {
            if (cells[k].kind == V_POINTER) {
                cells[k].known = true; /* null */
                continue;
            }
            if (k >= args[i].count) continue;
            int byte = 0;
            Value v = {.kind = V_DOUBLE, .known = true, .as.d = args[i].values[k]};
            cells[k] = converted(vm, v, vm_slot_type(vm->config, param->type, k, &byte));
        }
        PUSH(s->vars, var);
    }
    /* the stack as a call would leave it */
    for (int i = 0; i < symbol->param_count; i++) {
        const Symbol *param = symbol->params[i];
        const Var *var = &s->vars.items[vm->trial_base + i];
        bool by_place = param->type->kind == TY_ARRAY || param->by_reference || param->type->kind == TY_RECORD;
        push(vm, by_place ? make_pointer(var->at) : s->cells.items[s->objects.items[var->at.object].base]);
    }
    int end = vm->code.count - 1;
    s->frames.items[0].stack = 0;
    enter_function(vm, f, NULL, end);
    s->steps = 0;
    vm->nodes[0].start = 0;
    take_snapshot(vm);
    return true;
}

void vm_destroy(Vm *vm) {
    if (!vm) return;
    for (int i = 0; i < vm->snapshot_count; i++) free_state(&vm->snapshots[i].state);
    free(vm->snapshots);
    free_state(&vm->s);
    code_free(&vm->code);
    free(vm->touched.items);
    free(vm->output);
    free(vm->input);
    free(vm->log);
    free(vm->nodes);
    free(vm);
}

void vm_give_input(Vm *vm, const char *text) {
    size_t length = strlen(text);
    vm->input = realloc(vm->input, vm->input_length + length + 1);
    if (!vm->input) abort();
    memcpy(vm->input + vm->input_length, text, length + 1);
    vm->input_length += length;
    if (vm->s.status == VM_NEEDS_INPUT) vm->s.status = VM_READY;
}

void vm_end_input(Vm *vm) {
    vm->input_ended = true;
    if (vm->s.status == VM_NEEDS_INPUT) vm->s.status = VM_READY;
}

/* ---- looking inside ---- */

static uint64_t mix(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 1099511628211u;
    return hash;
}

uint64_t vm_fingerprint(const Vm *vm) {
    const State *s = &vm->s;
    uint64_t h = 14695981039346656037u;
    long long numbers[] = {s->pc, s->status, s->steps, s->log_count, s->node_count, s->node,
                           s->objects.count, s->cells.count, s->frames.count, s->stack.count,
                           s->evals.count, s->bounds.count, s->vars.count, s->serial,
                           (long long)s->output_length, (long long)s->input_pos};
    h = mix(h, numbers, sizeof numbers);
    for (int i = 0; i < s->cells.count; i++) {
        const Value *v = &s->cells.items[i];
        int known[2] = {(int)v->kind, v->known};
        h = mix(h, known, sizeof known);
        if (v->kind == V_POINTER) h = mix(h, &v->as.p.object, 2 * sizeof(int32_t));
        else h = mix(h, &v->as.i, sizeof v->as.i);
    }
    for (int i = 0; i < s->frames.count; i++) {
        h = mix(h, &s->frames.items[i].stmt, sizeof(void *));
        h = mix(h, &s->frames.items[i].phase, sizeof(int));
    }
    for (int i = 0; i < s->evals.count; i++) h = mix(h, &s->evals.items[i].expr, sizeof(void *));
    return mix(h, vm->output ? vm->output : "", s->output_length);
}

VmStatus vm_status(const Vm *vm) { return vm->s.status; }
const char *vm_message(const Vm *vm) { return vm->s.message; }
int vm_exit_status(const Vm *vm) { return vm->s.exit_status; }
long long vm_steps(const Vm *vm) { return vm->s.steps; }
Level vm_last_stop(const Vm *vm) { return vm->last_stop; }
int vm_frame_count(const Vm *vm) { return vm->s.frames.count; }
const Frame *vm_frame(const Vm *vm, int index) { return &vm->s.frames.items[index]; }
int vm_frame_vars(const Vm *vm, int frame) { return vm->s.frames.items[frame].var_count; }

const Var *vm_var(const Vm *vm, int frame, int index) {
    return &vm->s.vars.items[vm->s.frames.items[frame].vars + index];
}

const Var *vm_find_var(const Vm *vm, int frame, const Symbol *symbol) {
    for (int i = 0; i < vm_frame_vars(vm, frame); i++) {
        const Var *var = vm_var(vm, frame, i);
        if (var->live && var->symbol == symbol) return var;
    }
    return NULL;
}

const Bounds *vm_bounds(const Vm *vm, const Var *var, int dim) {
    return &vm->s.bounds.items[var->bounds + dim];
}

const Value *vm_cell(const Vm *vm, Pointer at) { return cell_of(vm, at); }
const Object *vm_object(const Vm *vm, Pointer at) { return object_of(vm, at); }
int vm_object_count(const Vm *vm) { return vm->s.objects.count; }
const Object *vm_object_at(const Vm *vm, int index) { return &vm->s.objects.items[index]; }

Pointer vm_pointer_to(const Vm *vm, int object) {
    return (Pointer){object, 0, vm->s.objects.items[object].serial};
}

const Eval *vm_evals(const Vm *vm, int *count) {
    int first = vm->s.frames.count ? vm->s.frames.items[vm->s.frames.count - 1].evals : 0;
    *count = vm->s.evals.count - first;
    return vm->s.evals.items + first;
}

const Touch *vm_touched(const Vm *vm, int *count) {
    *count = vm->touched.count;
    return vm->touched.items;
}

const char *vm_output(const Vm *vm, size_t *length) {
    *length = vm->s.output_length;
    return vm->output ? vm->output : "";
}

long vm_log_count(const Vm *vm) { return vm->s.log_count; }

const LogEntry *vm_log(const Vm *vm, long number) {
    if (number < 0 || number >= vm->s.log_count) return NULL;
    const LogEntry *entry = &vm->log[number % LOG_SIZE];
    return entry->number == number ? entry : NULL;
}

int vm_node_count(const Vm *vm) { return vm->s.node_count; }
const CallNode *vm_node(const Vm *vm, int index) { return &vm->nodes[index]; }
int vm_trial_count(const Vm *vm) { return vm->trial_count; }
const Var *vm_trial_var(const Vm *vm, int index) { return &vm->s.vars.items[vm->trial_base + index]; }

/* ---- text ---- */

void vm_format(const Vm *vm, const Value *v, const Type *type, char *out, size_t size) {
    if (!v->known) {
        snprintf(out, size, "?");
        return;
    }
    if (v->kind == V_POINTER) {
        if (v->as.p.object == 0) {
            snprintf(out, size, "null");
        } else {
            char name[64];
            vm_place_name(vm, v->as.p, name, sizeof name);
            snprintf(out, size, "&%s", name);
        }
        return;
    }
    if (v->kind == V_FLOAT || v->kind == V_DOUBLE) {
        snprintf(out, size, "%.6g", as_double(*v));
        if (!strpbrk(out, ".en") && strlen(out) + 2 < size) strcat(out, ".0");
        return;
    }
    if (type && type->kind == TY_BOOL) {
        snprintf(out, size, "%s", v->as.i ? "true" : "false");
    } else if (type && type->kind == TY_CHAR) {
        if (v->as.i >= 32 && v->as.i < 127) snprintf(out, size, "'%c'", v->as.i);
        else if (v->as.i == '\n') snprintf(out, size, "'\\n'");
        else snprintf(out, size, "'\\%d'", v->as.i);
    } else {
        snprintf(out, size, "%d", v->as.i);
    }
}

static void append(char *out, size_t size, const char *text) {
    size_t used = strlen(out);
    if (used + 1 < size) snprintf(out + used, size - used, "%s", text);
}

static void place_inside(const Vm *vm, const Type *type, const Bounds *bounds, int rank, int slot,
                         bool whole, char *out, size_t size) {
    char text[32];
    if (type->kind == TY_ARRAY) {
        int per = vm_slots(vm->config, type->base);
        if (per <= 0) return;
        int element = slot / per;
        long index[MAX_RANK];
        for (int i = rank - 1; i >= 0; i--) {
            long extent = bounds[i].hi - bounds[i].lo + 1;
            if (extent <= 0) return;
            index[i] = bounds[i].lo + element % extent;
            element /= (int)extent;
        }
        append(out, size, "[");
        for (int i = 0; i < rank; i++) {
            snprintf(text, sizeof text, "%s%ld", i ? "," : "", index[i]);
            append(out, size, text);
        }
        append(out, size, "]");
        place_inside(vm, type->base, NULL, 0, slot % per, whole, out, size);
    } else if (type->kind == TY_RECORD && !(whole && slot == 0)) {
        for (const VarDecl *f = type->record->fields; f; f = f->next) {
            int count = vm_slots(vm->config, f->symbol->type);
            if (slot >= count) {
                slot -= count;
                continue;
            }
            append(out, size, ".");
            append(out, size, f->name);
            const Type *inner = f->symbol->type;
            Bounds fixed[MAX_RANK];
            int r = 0;
            if (inner->kind == TY_ARRAY) {
                for (; r < inner->dims.count && r < MAX_RANK; r++) {
                    long long lo = vm->config->index_base, hi = 0;
                    const Dim *dim = &inner->dims.items[r];
                    if (dim->lo) const_int(dim->lo, &lo);
                    if (dim->hi) const_int(dim->hi, &hi);
                    if (!dim->lo) hi = lo + hi - 1;
                    fixed[r] = (Bounds){(long)lo, (long)hi};
                }
            }
            place_inside(vm, inner, fixed, r, slot, whole, out, size);
            return;
        }
    }
}

void vm_place_name(const Vm *vm, Pointer p, char *out, size_t size) {
    const Object *object = object_of(vm, p);
    out[0] = '\0';
    if (!object) {
        snprintf(out, size, "(gone)");
        return;
    }
    snprintf(out, size, "%s", object->symbol ? object->symbol->name : object->is_temporary ? "(temporary)" : "\"...\"");
    if (object->symbol && object->type)
        place_inside(vm, object->type, vm->s.bounds.items + object->bounds, object->rank, p.offset, true, out, size);
}

static bool find_eval(const Eval *evals, int count, const Expr *expr, Value *out, bool *newest) {
    for (int i = count - 1; i >= 0; i--) {
        if (evals[i].expr == expr) {
            *out = evals[i].value;
            *newest = i == count - 1;
            return true;
        }
    }
    return false;
}

static int level_of(TokenKind op) {
    switch (op) {
        case TOK_OR: return 1;
        case TOK_AND: return 2;
        case TOK_PIPE: return 3;
        case TOK_CARET: return 4;
        case TOK_AMP: return 5;
        case TOK_EQ: case TOK_NE: return 6;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 7;
        case TOK_SHL: case TOK_SHR: return 8;
        case TOK_PLUS: case TOK_MINUS: return 9;
        default: return 10;
    }
}

static const char *spelling(TokenKind op) {
    switch (op) {
        case TOK_OR: return "or";
        case TOK_AND: return "and";
        case TOK_NOT: return "not ";
        case TOK_PIPE: return "|";
        case TOK_CARET: return "^";
        case TOK_AMP: return "&";
        case TOK_TILDE: return "~";
        case TOK_EQ: return "=";
        case TOK_NE: return "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        case TOK_MOD: return "mod";
        case TOK_AT: return "@";
        default: return "?";
    }
}

typedef struct {
    const Vm *vm;
    const Eval *evals;
    int count;
    const char *mark_on, *mark_off, *value_on, *value_off;
    StrBuf *out;
} Printer;

static void show(Printer *p, const Expr *e, int min);

static void show_args(Printer *p, const Expr *e, const char *open, const char *close) {
    strbuf_puts(p->out, open);
    for (int i = 0; i < e->args.count; i++) {
        if (i) strbuf_puts(p->out, ", ");
        show(p, e->args.items[i], 0);
    }
    strbuf_puts(p->out, close);
}

static void show(Printer *p, const Expr *e, int min) {
    Value v;
    bool newest;
    if (p->count > 0 && find_eval(p->evals, p->count, e, &v, &newest) && e->type->kind != TY_RECORD) {
        char text[64];
        vm_format(p->vm, &v, e->type, text, sizeof text);
        bool negative = text[0] == '-' && min > 9;
        strbuf_puts(p->out, newest ? p->mark_on : p->value_on);
        if (negative) strbuf_puts(p->out, "(");
        strbuf_puts(p->out, text);
        if (negative) strbuf_puts(p->out, ")");
        strbuf_puts(p->out, newest ? p->mark_off : p->value_off);
        return;
    }
    switch (e->kind) {
        case EX_STRING: strbuf_printf(p->out, "\"%s\"", e->text); break;
        case EX_CHAR: strbuf_printf(p->out, "'%s'", e->text); break;
        case EX_APPLY:
            show(p, e->base, 12);
            show_args(p, e, e->is_call ? "(" : "[", e->is_call ? ")" : "]");
            break;
        case EX_INDEX:
            show(p, e->base, 12);
            show_args(p, e, "[", "]");
            break;
        case EX_FIELD:
            show(p, e->base, 12);
            strbuf_printf(p->out, "%s%s", e->op == TOK_ARROW ? "->" : ".", e->text);
            break;
        case EX_UNARY:
            if (min > 11) strbuf_puts(p->out, "(");
            strbuf_puts(p->out, spelling(e->op));
            show(p, e->base, 11);
            if (min > 11) strbuf_puts(p->out, ")");
            break;
        case EX_BINARY: {
            int level = level_of(e->op);
            if (level < min) strbuf_puts(p->out, "(");
            show(p, e->base, level);
            strbuf_printf(p->out, " %s ", spelling(e->op));
            show(p, e->right, level + 1);
            if (level < min) strbuf_puts(p->out, ")");
            break;
        }
        default: strbuf_puts(p->out, e->text); break;
    }
}

void print_expr(const Vm *vm, const Expr *expr, const Eval *evals, int count, const char *mark_on,
                const char *mark_off, const char *value_on, const char *value_off, StrBuf *out) {
    Printer printer = {vm, evals, count, mark_on, mark_off, value_on, value_off, out};
    show(&printer, expr, 0);
}

/* "A[3]": a place as the current function sees it. */
static void place_text(const Vm *vm, const Expr *e, char *out, size_t size) {
    int count;
    const Eval *evals = vm_evals(vm, &count);
    StrBuf text = {0};
    Printer printer = {vm, evals, count, "", "", "", "", &text};
    strbuf_puts(&text, "");
    /* the place itself must not turn into its old value, only its indices */
    const Expr *chain[32];
    int n = 0;
    for (const Expr *x = e; x && n < 32; x = x->base) {
        chain[n++] = x;
        if (x->kind == EX_NAME || x->is_call) break;
    }
    for (int i = n - 1; i >= 0; i--) {
        const Expr *x = chain[i];
        if (x->kind == EX_NAME) strbuf_puts(&text, x->text);
        else if (x->is_call) show(&printer, x, 0);
        else if (x->kind == EX_INDEX || x->kind == EX_APPLY) show_args(&printer, x, "[", "]");
        else if (x->kind == EX_FIELD) strbuf_printf(&text, "%s%s", x->op == TOK_ARROW ? "->" : ".", x->text);
        else if (x->kind == EX_UNARY && x->op == TOK_STAR) {
            char *copy = malloc(text.length + 2);
            if (!copy) abort();
            snprintf(copy, text.length + 2, "*%s", text.data);
            text.length = 0;
            strbuf_puts(&text, copy);
            free(copy);
        }
    }
    snprintf(out, size, "%s", text.data);
    strbuf_free(&text);
}

static void format_call(Vm *vm, const Function *function, char *out, size_t size) {
    out[0] = '\0';
    int frame = frame_index(vm);
    for (int i = 0; i < function->symbol->param_count; i++) {
        const Symbol *param = function->symbol->params[i];
        const Var *var = vm_find_var(vm, frame, param);
        char text[64] = "";
        if (i) append(out, size, ", ");
        if (param->type->kind == TY_ARRAY || param->type->kind == TY_RECORD) {
            snprintf(text, sizeof text, "%.60s", param->name);
        } else if (var) {
            const Value *cell = cell_of(vm, var->at);
            char value[40] = "?";
            if (cell) vm_format(vm, cell, param->type, value, sizeof value);
            snprintf(text, sizeof text, "%.20s=%s", param->name, value);
        }
        append(out, size, text);
    }
}
