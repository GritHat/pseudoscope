#ifndef PLIKE_VM_H
#define PLIKE_VM_H

#include "plike/sema.h"

#include <stdint.h>

/* A virtual machine that runs a program one small step at a time, can go back,
 * and knows at every moment which piece of source it is executing. */

/* ---- values and memory ---- */

typedef struct {
    int32_t object;      /* 0 is the null pointer */
    int32_t offset;      /* in slots */
    uint32_t serial;     /* tells a dead object from a newer one in its place */
} Pointer;

typedef enum { V_INT, V_FLOAT, V_DOUBLE, V_POINTER, V_TEXT } ValueKind;

typedef struct {
    ValueKind kind;
    bool known;          /* false until something is stored */
    union {
        int32_t i;           /* V_TEXT: which text */
        float f;
        double d;
        Pointer p;
    } as;
} Value;

typedef struct {
    long lo, hi;
} Bounds;

/* Memory is made of slots, one per number or pointer. An object is a run of
 * slots: a variable, a string literal, a temporary. */
typedef struct {
    int base, count;     /* slots */
    uint32_t serial;
    const Symbol *symbol;/* NULL for temporaries and literals */
    Type *type;
    int bounds, rank;    /* of an array */
    int frame;
    bool is_temporary;
    uint32_t address;    /* a plausible machine address, to show */
} Object;

/* A variable of a frame. For arrays and by-reference parameters `at` is where
 * the caller's data is. */
typedef struct {
    const Symbol *symbol;
    Pointer at;
    int bounds, rank;
    bool live;
} Var;

typedef struct {
    const Function *function; /* NULL for the globals */
    const Expr *call;         /* in the caller */
    int vars, var_count;
    int objects, slots, bounds; /* marks to go back to on return */
    int stack, evals;
    int return_pc;
    int node;                 /* in the call tree */
    const Stmt *stmt;
    int phase;
    uint32_t address;
    int life;                 /* who is acting: see Life */
    int message;              /* the one that got us here, or -1 */
    Pointer sender;
} Frame;

/* What an expression of the current statement turned out to be. */
typedef struct {
    const Expr *expr;
    Value value;
} Eval;

/* ---- what happened ---- */

typedef enum {
    LOG_ASSIGN, LOG_CONDITION, LOG_LOOP, LOG_CALL, LOG_RETURN, LOG_PRINT, LOG_READ,
    LOG_WARNING, LOG_ERROR, LOG_END,
    LOG_SEND, LOG_DELIVER, LOG_LOST, LOG_IGNORED, LOG_CHECK, LOG_STEP, LOG_TODO, LOG_STATE
} LogKind;

typedef struct {
    LogKind kind;
    long number;
    const Function *function;
    const Stmt *stmt;
    const Expr *expr;      /* assigned variable, condition */
    char target[48];       /* "A[3]" */
    char value[48];
    char old[48];
    bool truth;
    int depth;
    const char *text;      /* static; warnings and errors */
} LogEntry;

typedef struct {
    int parent;
    const Function *function;
    char args[64];
    char result[32];
    long long start, end;  /* steps; end is LLONG_MAX while active */
    int depth;
} CallNode;

typedef struct {
    Pointer at;
    bool write;
} Touch;

/* ---- systems ---- */

/* Somebody who takes part: a component, an object, a subprogram, or (always
 * the first) the program itself. A column of a sequence diagram. */
typedef struct {
    char name[40];
    Pointer at;               /* components and objects */
    const Class *klass;
    const Function *function;
} Life;

typedef enum {
    MSG_CALL, MSG_RETURN, MSG_DELIVER, MSG_LOST, MSG_IGNORED, MSG_STATE, MSG_STEP, MSG_CHECK
} MessageKind;

/* A row of a sequence diagram. */
typedef struct {
    MessageKind kind;
    int from, to;             /* lives */
    const char *name;
    char args[56];
    long sent, time;
    long long step;
    int end;                  /* row at which the activation ends; INT_MAX while active */
    bool ok;                  /* checks */
    const Stmt *stmt;
} SequenceRow;

/* A message on its way. */
typedef struct {
    long time, sent, seq;
    Pointer from, to;
    int from_life, to_life;
    const char *name;
    const Function *handler;  /* tells the types of the arguments */
    Value args[8];
    int arg_count;
    bool lost;                /* will vanish on the way */
    const Stmt *stmt;
} Event;

typedef struct {
    Pointer from, to;
    int from_count, to_count; /* slots */
    bool both_ways;
    long delay, delay_max, loss;
} LinkRule;

typedef struct {
    const Stmt *stmt;
    bool ok;
    char text[80];
    int step_row;             /* of the MSG_STEP before, or -1 */
} Check;

typedef enum { VM_READY, VM_NEEDS_INPUT, VM_FINISHED, VM_FAILED } VmStatus;

/* Detail levels. A stop at some level is also one at the deeper ones. */
typedef enum { LEVEL_NONE, LEVEL_SYSTEM, LEVEL_CALL, LEVEL_STATEMENT, LEVEL_EXPRESSION, LEVEL_MEMORY } Level;

enum { PHASE_WHOLE, PHASE_CONDITION, PHASE_STEP };

typedef struct Vm Vm;

/* Input of a trial call: a flat list of values per parameter. */
typedef struct {
    double *values;
    int count;
} TrialArgument;

/* Runs main, or a single function on the given arguments (as many as its
 * parameters; numbers fill arrays and records in order). */
Vm *vm_create(const Program *program, const Config *config);
void vm_start(Vm *vm);
bool vm_start_trial(Vm *vm, const Function *function, const TrialArgument *args);
/* How many values a parameter of a trial takes, or -1 if its bounds cannot be
 * computed. When `open`, any multiple will do. */
int vm_trial_size(const Config *config, const Function *function, const TrialArgument *args,
                  int index, bool *open);
/* Runs a test: "test ... end test". */
void vm_start_test(Vm *vm, const Function *test);
void vm_destroy(Vm *vm);

/* Executes up to `limit` instructions; stops early at a stop of this level.
 * Returns the level of the stop, LEVEL_NONE if the limit was reached first. */
Level vm_run(Vm *vm, Level level, long limit);
/* Back to the previous stop of this level. */
bool vm_back(Vm *vm, Level level);
void vm_seek(Vm *vm, long long step);

void vm_give_input(Vm *vm, const char *text);
void vm_end_input(Vm *vm);

/* ---- looking inside ---- */

/* Equal states have equal fingerprints. */
uint64_t vm_fingerprint(const Vm *vm);

VmStatus vm_status(const Vm *vm);
const char *vm_message(const Vm *vm);  /* why it failed */
int vm_exit_status(const Vm *vm);
long long vm_steps(const Vm *vm);
Level vm_last_stop(const Vm *vm);

int vm_frame_count(const Vm *vm);
const Frame *vm_frame(const Vm *vm, int index);     /* 0 is the globals */
int vm_frame_vars(const Vm *vm, int frame);
const Var *vm_var(const Vm *vm, int frame, int index);
const Var *vm_find_var(const Vm *vm, int frame, const Symbol *symbol);
const Bounds *vm_bounds(const Vm *vm, const Var *var, int dim);
const Value *vm_cell(const Vm *vm, Pointer at);     /* NULL if not valid */
const Object *vm_object(const Vm *vm, Pointer at);  /* NULL if not valid */
int vm_object_count(const Vm *vm);
const Object *vm_object_at(const Vm *vm, int index);
Pointer vm_pointer_to(const Vm *vm, int object);
const Eval *vm_evals(const Vm *vm, int *count);     /* of the current frame */
const Touch *vm_touched(const Vm *vm, int *count);  /* since the last stop */
const char *vm_output(const Vm *vm, size_t *length);
long vm_log_count(const Vm *vm);
const LogEntry *vm_log(const Vm *vm, long number);  /* NULL when forgotten */
int vm_node_count(const Vm *vm);
const CallNode *vm_node(const Vm *vm, int index);
long vm_time(const Vm *vm);
int vm_life_count(const Vm *vm);
const Life *vm_life(const Vm *vm, int index);
int vm_message_count(const Vm *vm);
const SequenceRow *vm_message_at(const Vm *vm, int index);
int vm_queue_count(const Vm *vm);
const Event *vm_queued(const Vm *vm, int index);
int vm_check_count(const Vm *vm);
const Check *vm_check(const Vm *vm, int index);
int vm_link_count(const Vm *vm);
const LinkRule *vm_link(const Vm *vm, int index);
int vm_current_life(const Vm *vm);
/* The state a component is in; NULL if it has none. */
const char *vm_state_name(const Vm *vm, const Life *life);
/* The components of the program, in order. */
int vm_instance_count(const Vm *vm);
Pointer vm_instance(const Vm *vm, int index, const Class **klass);
int vm_trial_count(const Vm *vm);
const Var *vm_trial_var(const Vm *vm, int index);

/* Slots a type takes; arrays must have constant bounds. */
int vm_slots(const Config *config, const Type *type);
/* Size in bytes and alignment in C. */
int vm_sizeof(const Config *config, const Type *type, int *align);
/* Byte offset and type of a slot inside a type. */
const Type *vm_slot_type(const Config *config, const Type *type, int slot, int *byte);

/* ---- text ---- */

/* A value as the source would write it. */
void vm_format(const Vm *vm, const Value *value, const Type *type, char *out, size_t size);
/* "A[3]", "p.x": the name of a place in memory. */
void vm_place_name(const Vm *vm, Pointer at, char *out, size_t size);
/* The same, but always down to the single slot: "p.x" also for the first field. */
void vm_slot_name(const Vm *vm, Pointer at, char *out, size_t size);

/* Prints an expression as pseudocode. When `evals` is given, the first `count`
 * evaluated parts are replaced by their values, and the newest one is put
 * between the two marks. */
void print_expr(const Vm *vm, const Expr *expr, const Eval *evals, int count,
                const char *mark_on, const char *mark_off, const char *value_on,
                const char *value_off, StrBuf *out);

/* ---- compiled code ---- */

typedef enum {
    OP_CONST, OP_STRING, OP_ADDR, OP_LOAD, OP_STORE, OP_COPY, OP_REBIND,
    OP_INDEX, OP_FIELD, OP_CONVERT, OP_UNARY, OP_BINARY, OP_TRUTH, OP_BUILTIN,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE, OP_AND, OP_OR, OP_POP,
    OP_CALL, OP_RETURN, OP_ENTER, OP_PARAM_BOUNDS, OP_ALLOC,
    OP_BLOCK, OP_BLOCK_END, OP_STMT, OP_EVAL, OP_CONDITION, OP_PRINT, OP_READ,
    OP_EXTERNAL, OP_UNSUPPORTED, OP_END,
    OP_NOW, OP_SENDER, OP_LINK, OP_SEED, OP_READY, OP_LOOP, OP_RUN, OP_SEND, OP_STOP, OP_CANCEL,
    OP_CHECK, OP_EXPECT, OP_STEP, OP_TODO
} Op;

typedef struct {
    Op op;
    int a, b;
    const Expr *expr;
    const Stmt *stmt;
    const Symbol *symbol;
    Type *type;
    const Function *function;
    Value value;
} Instr;

typedef struct {
    Instr *items;
    int count, capacity;
    int global_vars;
    int start;                 /* global set-up, then main */
    int ready;                 /* where the set-up ends */
    struct {
        const Function *function;
        int entry, vars;
    } *functions;
    int function_count;
    const Expr **strings;
    int string_count, string_capacity;
} Code;

void compile(const Program *program, const Config *config, Code *code);
void code_free(Code *code);
int code_entry(const Code *code, const Function *function, int *vars);

#endif
