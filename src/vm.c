/*
 * vm.c -- the bytecode interpreter loop.
 */

#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "natives.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static void reset_stack(VM *vm)
{
    vm->sp            = vm->stack;
    vm->frame_count   = 0;
    vm->open_upvalues = NULL;
}

static void push(VM *vm, Value v) { *vm->sp++ = v; }
static Value pop(VM *vm)          { return *--vm->sp; }
static Value peek(VM *vm, int d)  { return vm->sp[-1 - d]; }

static void runtime_error(VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame   *frame = &vm->frames[i];
        ObjFunction *fn    = frame->closure->fn;
        size_t       inst  = (size_t)(frame->ip - fn->chunk.code - 1);
        fprintf(stderr, "[line %d] in %s\n",
                fn->chunk.lines[inst],
                fn->name ? fn->name->chars : "script");
    }
    reset_stack(vm);
}

/* higher-order library natives (defined below run(), which they re-enter) */
static bool n_map(Heap *h, int argc, Value *args, Value *result);
static bool n_filter(Heap *h, int argc, Value *args, Value *result);
static bool n_each(Heap *h, int argc, Value *args, Value *result);

void vm_define_native(VM *vm, const char *name, NativeFn fn, int arity)
{
    ObjString *s = string_copy(&vm->heap, name, strlen(name));
    push(vm, OBJ_VAL(s));                              /* root while allocating */
    push(vm, OBJ_VAL(native_new(&vm->heap, fn, arity)));
    table_set(&vm->globals, AS_STRING(peek(vm, 1)), peek(vm, 0));
    pop(vm);
    pop(vm);
}

void vm_init(VM *vm)
{
    heap_init(&vm->heap);
    vm->heap.vm = vm;
    vm->stack = (Value *)malloc(STACK_MAX * sizeof(Value));
    if (vm->stack == NULL) { fprintf(stderr, "coil: cannot allocate value stack\n"); exit(1); }
    reset_stack(vm);
    table_init(&vm->globals);
    register_natives(vm);
    /* higher-order helpers: run a user callback per element */
    vm_define_native(vm, "map",    n_map,    2);
    vm_define_native(vm, "filter", n_filter, 2);
    vm_define_native(vm, "each",   n_each,   2);
}

void vm_free(VM *vm)
{
    table_free(&vm->globals);
    heap_free(&vm->heap);
    free(vm->stack);
    vm->stack = NULL;
}

/* ------------------------------------------------------------------ */
static bool call(VM *vm, ObjClosure *closure, int argc)
{
    if (argc != closure->fn->arity) {
        runtime_error(vm, "expected %d arguments but got %d",
                      closure->fn->arity, argc);
        return false;
    }
    if (vm->frame_count == FRAMES_MAX) {
        runtime_error(vm, "stack overflow");
        return false;
    }
    /* reserve a full frame's worth of stack; the verifier caps frame height to
     * MAX_FRAME_STACK, so this guarantees push() can never run past the stack. */
    if ((size_t)(vm->sp - vm->stack) > STACK_MAX - MAX_FRAME_STACK) {
        runtime_error(vm, "stack overflow");
        return false;
    }
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip      = closure->fn->chunk.code;
    frame->slots   = vm->sp - argc - 1;
    return true;
}

static bool call_value(VM *vm, Value callee, int argc)
{
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
        case OBJ_CLOSURE:
            return call(vm, AS_CLOSURE(callee), argc);
        case OBJ_NATIVE: {
            ObjNative *n = AS_NATIVE(callee);
            if (n->arity >= 0 && argc != n->arity) {
                runtime_error(vm, "expected %d arguments but got %d",
                              n->arity, argc);
                return false;
            }
            Value result = NIL_VAL;
            if (!n->fn(&vm->heap, argc, vm->sp - argc, &result)) {
                runtime_error(vm, "error in native function '%s'",
                              n->fn == NULL ? "?" : "native");
                return false;
            }
            vm->sp -= argc + 1;
            push(vm, result);
            return true;
        }
        default: break;
        }
    }
    runtime_error(vm, "can only call functions");
    return false;
}

static ObjUpvalue *capture_upvalue(VM *vm, Value *local)
{
    ObjUpvalue *prev = NULL;
    ObjUpvalue *up   = vm->open_upvalues;
    while (up != NULL && up->location > local) {
        prev = up;
        up   = up->next;
    }
    if (up != NULL && up->location == local)
        return up;

    ObjUpvalue *created = upvalue_new(&vm->heap, local);
    created->next = up;
    if (prev == NULL)
        vm->open_upvalues = created;
    else
        prev->next = created;
    return created;
}

static void close_upvalues(VM *vm, Value *last)
{
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        ObjUpvalue *up = vm->open_upvalues;
        up->closed   = *up->location;
        up->location = &up->closed;
        vm->open_upvalues = up->next;
    }
}

static void concatenate(VM *vm)
{
    ObjString *b = AS_STRING(peek(vm, 0));
    ObjString *a = AS_STRING(peek(vm, 1));
    size_t len = (size_t)a->length + b->length;
    char *chars = ALLOCATE(&vm->heap, char, len + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[len] = '\0';
    ObjString *result = string_take(&vm->heap, chars, len);
    pop(vm);
    pop(vm);
    push(vm, OBJ_VAL(result));
}

/* ------------------------------------------------------------------ */
/* Execute frames until the one that was on top when we were entered returns.
 * `base_frame` is the frame_count to stop at: 0 drives a whole program to
 * completion; a positive value lets a native re-enter the VM for a single
 * callback and hand control back once that callback's frame has returned. */
static InterpretResult run(VM *vm, int base_frame)
{
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
#ifdef COIL_FUZZ
    uint64_t steps = 0;          /* bound execution so fuzzing doesn't hang */
#endif

#define READ_BYTE()     (*frame->ip++)
#define READ_SHORT()    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONST()    (frame->closure->fn->chunk.constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONST())

#define NUM_BINOP(make_int, make_flt, op)                                     \
    do {                                                                      \
        Value b = peek(vm, 0), a = peek(vm, 1);                               \
        if (IS_INT(a) && IS_INT(b)) {                                         \
            pop(vm); pop(vm);                                                 \
            push(vm, make_int(AS_INT(a) op AS_INT(b)));                       \
        } else if (IS_NUM(a) && IS_NUM(b)) {                                  \
            pop(vm); pop(vm);                                                 \
            push(vm, make_flt(value_as_double(a) op value_as_double(b)));     \
        } else {                                                             \
            runtime_error(vm, "operands must be numbers");                    \
            return INTERPRET_RUNTIME_ERROR;                                   \
        }                                                                     \
    } while (0)

    for (;;) {
#ifdef COIL_FUZZ
        /* bound both time (steps) and space (live bytes) so a fuzz input that
         * loops or grows a value quadratically terminates instead of hanging */
        if (++steps > 2000000ULL || vm->heap.bytes_allocated > (64u << 20))
            return INTERPRET_OK;
#endif
        uint8_t op = READ_BYTE();
        switch (op) {
        case OP_CONST: push(vm, READ_CONST()); break;
        case OP_NIL:   push(vm, NIL_VAL); break;
        case OP_TRUE:  push(vm, BOOL_VAL(true)); break;
        case OP_FALSE: push(vm, BOOL_VAL(false)); break;
        case OP_POP:   pop(vm); break;

        case OP_ADD: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (IS_STRING(a) && IS_STRING(b)) {
                concatenate(vm);
            } else if (IS_INT(a) && IS_INT(b)) {
                pop(vm); pop(vm);
                push(vm, INT_VAL((int64_t)((uint64_t)AS_INT(a) + (uint64_t)AS_INT(b))));
            } else if (IS_NUM(a) && IS_NUM(b)) {
                pop(vm); pop(vm);
                push(vm, FLOAT_VAL(value_as_double(a) + value_as_double(b)));
            } else {
                runtime_error(vm, "operands must be two numbers or two strings");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUB: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (IS_INT(a) && IS_INT(b)) {
                pop(vm); pop(vm);
                push(vm, INT_VAL((int64_t)((uint64_t)AS_INT(a) - (uint64_t)AS_INT(b))));
            } else if (IS_NUM(a) && IS_NUM(b)) {
                pop(vm); pop(vm); push(vm, FLOAT_VAL(value_as_double(a) - value_as_double(b)));
            } else { runtime_error(vm, "operands must be numbers"); return INTERPRET_RUNTIME_ERROR; }
            break;
        }
        case OP_MUL: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (IS_INT(a) && IS_INT(b)) {
                pop(vm); pop(vm);
                push(vm, INT_VAL((int64_t)((uint64_t)AS_INT(a) * (uint64_t)AS_INT(b))));
            } else if (IS_NUM(a) && IS_NUM(b)) {
                pop(vm); pop(vm); push(vm, FLOAT_VAL(value_as_double(a) * value_as_double(b)));
            } else { runtime_error(vm, "operands must be numbers"); return INTERPRET_RUNTIME_ERROR; }
            break;
        }
        case OP_DIV: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (IS_INT(a) && IS_INT(b)) {
                if (AS_INT(b) == 0) { runtime_error(vm, "division by zero"); return INTERPRET_RUNTIME_ERROR; }
                int64_t da = AS_INT(a), db = AS_INT(b);
                pop(vm); pop(vm);
                /* db == -1 also covers the INT64_MIN/-1 overflow case */
                push(vm, INT_VAL(db == -1 ? (int64_t)(-(uint64_t)da) : da / db));
            } else if (IS_NUM(a) && IS_NUM(b)) {
                pop(vm); pop(vm); push(vm, FLOAT_VAL(value_as_double(a) / value_as_double(b)));
            } else {
                runtime_error(vm, "operands must be numbers");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_MOD: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (IS_INT(a) && IS_INT(b)) {
                if (AS_INT(b) == 0) { runtime_error(vm, "modulo by zero"); return INTERPRET_RUNTIME_ERROR; }
                int64_t ma = AS_INT(a), mb = AS_INT(b);
                pop(vm); pop(vm);
                push(vm, INT_VAL(mb == -1 ? 0 : ma % mb));   /* mb==-1 avoids overflow */
            } else {
                runtime_error(vm, "operands must be integers");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_NEG: {
            Value a = peek(vm, 0);
            if (IS_INT(a))        { pop(vm); push(vm, INT_VAL((int64_t)(-(uint64_t)AS_INT(a)))); }
            else if (IS_FLOAT(a)) { pop(vm); push(vm, FLOAT_VAL(-AS_FLOAT(a))); }
            else { runtime_error(vm, "operand must be a number"); return INTERPRET_RUNTIME_ERROR; }
            break;
        }
        case OP_NOT: push(vm, BOOL_VAL(!value_truthy(pop(vm)))); break;

        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
            Value b = peek(vm, 0), a = peek(vm, 1);
            if (!IS_INT(a) || !IS_INT(b)) {
                runtime_error(vm, "bitwise operands must be integers");
                return INTERPRET_RUNTIME_ERROR;
            }
            pop(vm); pop(vm);
            int64_t x = AS_INT(a), y = AS_INT(b), r = 0;
            switch (op) {
            case OP_BAND: r = x & y; break;
            case OP_BOR:  r = x | y; break;
            case OP_BXOR: r = x ^ y; break;
            case OP_SHL:  r = (int64_t)((uint64_t)x << (y & 63)); break;  /* defined wrap */
            case OP_SHR:  r = (int64_t)((uint64_t)x >> (y & 63)); break;  /* logical */
            }
            push(vm, INT_VAL(r));
            break;
        }
        case OP_BNOT: {
            Value a = peek(vm, 0);
            if (!IS_INT(a)) { runtime_error(vm, "~ operand must be an integer"); return INTERPRET_RUNTIME_ERROR; }
            pop(vm);
            push(vm, INT_VAL(~AS_INT(a)));
            break;
        }

        case OP_EQ: { Value b = pop(vm), a = pop(vm); push(vm, BOOL_VAL(value_equal(a, b))); break; }
        case OP_NE: { Value b = pop(vm), a = pop(vm); push(vm, BOOL_VAL(!value_equal(a, b))); break; }
        case OP_LT: NUM_BINOP(BOOL_VAL, BOOL_VAL, <); break;
        case OP_LE: NUM_BINOP(BOOL_VAL, BOOL_VAL, <=); break;
        case OP_GT: NUM_BINOP(BOOL_VAL, BOOL_VAL, >); break;
        case OP_GE: NUM_BINOP(BOOL_VAL, BOOL_VAL, >=); break;

        case OP_JMP:          { uint16_t off = READ_SHORT(); frame->ip += off; break; }
        case OP_JMP_IF_FALSE: { uint16_t off = READ_SHORT(); if (!value_truthy(peek(vm, 0))) frame->ip += off; break; }
        case OP_LOOP:         { uint16_t off = READ_SHORT(); frame->ip -= off; break; }

        case OP_GET_LOCAL: { uint8_t s = READ_BYTE(); push(vm, frame->slots[s]); break; }
        case OP_SET_LOCAL: { uint8_t s = READ_BYTE(); frame->slots[s] = peek(vm, 0); break; }

        case OP_GET_GLOBAL: {
            ObjString *name = READ_STRING();
            Value v;
            if (!table_get(&vm->globals, name, &v)) {
                runtime_error(vm, "undefined variable '%s'", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, v);
            break;
        }
        case OP_DEF_GLOBAL: {
            ObjString *name = READ_STRING();
            table_set(&vm->globals, name, peek(vm, 0));
            pop(vm);
            break;
        }
        case OP_SET_GLOBAL: {
            ObjString *name = READ_STRING();
            if (table_set(&vm->globals, name, peek(vm, 0))) {
                table_delete(&vm->globals, name);
                runtime_error(vm, "undefined variable '%s'", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_GET_UPVALUE: { uint8_t s = READ_BYTE(); push(vm, *frame->closure->upvalues[s]->location); break; }
        case OP_SET_UPVALUE: { uint8_t s = READ_BYTE(); *frame->closure->upvalues[s]->location = peek(vm, 0); break; }

        case OP_CALL: {
            int argc = READ_BYTE();
            if (!call_value(vm, peek(vm, argc), argc))
                return INTERPRET_RUNTIME_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        case OP_CLOSURE: {
            ObjFunction *fn = AS_FUNCTION(READ_CONST());
            ObjClosure  *cl = closure_new(&vm->heap, fn);
            push(vm, OBJ_VAL(cl));
            for (int i = 0; i < cl->upvalue_count; i++) {
                uint8_t is_local = READ_BYTE();
                uint8_t index    = READ_BYTE();
                if (is_local)
                    cl->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                else
                    cl->upvalues[i] = frame->closure->upvalues[index];
            }
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->sp - 1);
            pop(vm);
            break;

        case OP_RETURN: {
            Value result = pop(vm);
            close_upvalues(vm, frame->slots);
            vm->frame_count--;
            if (vm->frame_count == 0) {
                pop(vm);              /* the top-level script slot */
                return INTERPRET_OK;
            }
            vm->sp = frame->slots;
            push(vm, result);
            if (vm->frame_count == base_frame)
                return INTERPRET_OK;  /* handed back to a re-entrant caller */
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        case OP_NEW_ARRAY: {
            int n = READ_BYTE();
            ObjArray *arr = array_new(&vm->heap);
            for (int i = 0; i < n; i++)
                valuearray_push(&arr->items, peek(vm, n - 1 - i));
            vm->sp -= n;
            push(vm, OBJ_VAL(arr));
            break;
        }
        case OP_NEW_TABLE: {
            int n = READ_BYTE();
            ObjTable *t = table_obj_new(&vm->heap);
            Value *base = vm->sp - 2 * n;          /* n key/value pairs */
            for (int i = 0; i < n; i++) {
                Value key = base[2 * i];
                Value val = base[2 * i + 1];
                if (!value_hashable(key)) {
                    runtime_error(vm, "table key must be nil, bool, number, or string");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vtable_set(t, key, val) != 0) {
                    runtime_error(vm, "out of memory");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
            vm->sp -= 2 * n;
            push(vm, OBJ_VAL(t));
            break;
        }
        case OP_INDEX_GET: {
            Value index = pop(vm);
            Value cont  = pop(vm);
            if (IS_ARRAY(cont)) {
                if (!IS_INT(index)) { runtime_error(vm, "array index must be an integer"); return INTERPRET_RUNTIME_ERROR; }
                ObjArray *a = AS_ARRAY(cont);
                int64_t  i  = AS_INT(index);
                if (i < 0 || (size_t)i >= a->items.count) { runtime_error(vm, "array index out of bounds"); return INTERPRET_RUNTIME_ERROR; }
                push(vm, a->items.values[i]);
            } else if (IS_TABLE(cont)) {
                if (!value_hashable(index)) { runtime_error(vm, "invalid table key"); return INTERPRET_RUNTIME_ERROR; }
                Value out;
                push(vm, vtable_get(AS_TABLE(cont), index, &out) ? out : NIL_VAL);
            } else if (IS_SLICE(cont)) {
                if (!IS_INT(index)) { runtime_error(vm, "slice index must be an integer"); return INTERPRET_RUNTIME_ERROR; }
                ObjSlice *sl = AS_SLICE(cont);
                int64_t   i  = AS_INT(index);
                if (i < 0 || (size_t)i >= sl->len) { runtime_error(vm, "slice index out of bounds"); return INTERPRET_RUNTIME_ERROR; }
                push(vm, sl->parent->items.values[sl->start + (size_t)i]);
            } else {
                runtime_error(vm, "can only index arrays and tables");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_INDEX_SET: {
            Value value = peek(vm, 0);
            Value index = peek(vm, 1);
            Value cont  = peek(vm, 2);
            if (IS_ARRAY(cont)) {
                if (!IS_INT(index)) { runtime_error(vm, "array index must be an integer"); return INTERPRET_RUNTIME_ERROR; }
                ObjArray *a = AS_ARRAY(cont);
                int64_t  i  = AS_INT(index);
                if (i < 0 || (size_t)i >= a->items.count) { runtime_error(vm, "array index out of bounds"); return INTERPRET_RUNTIME_ERROR; }
                a->items.values[i] = value;
            } else if (IS_TABLE(cont)) {
                if (!value_hashable(index)) { runtime_error(vm, "invalid table key"); return INTERPRET_RUNTIME_ERROR; }
                if (vtable_set(AS_TABLE(cont), index, value) != 0) { runtime_error(vm, "out of memory"); return INTERPRET_RUNTIME_ERROR; }
            } else {
                runtime_error(vm, "can only index arrays and tables");
                return INTERPRET_RUNTIME_ERROR;
            }
            pop(vm); pop(vm); pop(vm);
            push(vm, value);
            break;
        }
        case OP_LEN: {
            Value v = pop(vm);
            if (IS_ARRAY(v))       push(vm, INT_VAL((int64_t)AS_ARRAY(v)->items.count));
            else if (IS_STRING(v)) push(vm, INT_VAL((int64_t)AS_STRING(v)->length));
            else if (IS_TABLE(v))  push(vm, INT_VAL((int64_t)AS_TABLE(v)->live));
            else if (IS_SLICE(v))  push(vm, INT_VAL((int64_t)AS_SLICE(v)->len));
            else { runtime_error(vm, "len expects an array, string, or table"); return INTERPRET_RUNTIME_ERROR; }
            break;
        }

        case OP_PRINT: value_print(pop(vm)); printf("\n"); break;
        case OP_HALT:  return INTERPRET_OK;
        default:
            runtime_error(vm, "unknown opcode %d", op);
            return INTERPRET_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONST
#undef READ_STRING
#undef NUM_BINOP
}

/* ------------------------------------------------------------------ */
/* Higher-order natives.
 *
 * These call a Coil callback once per element by re-entering the interpreter.
 * The callback is arbitrary user code: it can allocate freely, so a garbage
 * collection may run at any point during it. */

/* Invoke `fn` with a single argument and return its result in *out. */
static bool vm_call1(VM *vm, Value fn, Value arg, Value *out)
{
    int base = vm->frame_count;
    push(vm, fn);                                   /* callee below its args */
    push(vm, arg);
    if (!call_value(vm, fn, 1))
        return false;
    if (vm->frame_count == base) {                  /* a native ran inline */
        *out = pop(vm);
        return true;
    }
    if (run(vm, base) != INTERPRET_OK)              /* drive the closure body */
        return false;
    *out = pop(vm);
    return true;
}

/* Apply `fn` to each element of `src`, collecting into `out`: in map mode the
 * result of each call, in filter mode the elements whose call was truthy. */
static bool hof_collect(VM *vm, Value fn, ObjArray *src, bool filtering, ObjArray *out)
{
    /* Root the in-progress result on the operand stack: a callback can run a
     * collection, and `out` is otherwise reachable only from this C frame. */
    push(vm, OBJ_VAL(out));
    for (size_t i = 0; i < src->items.count; i++) {
        Value elem = src->items.values[i];
        Value r;
        if (!vm_call1(vm, fn, elem, &r)) {
            pop(vm);
            return false;
        }
        if (filtering) {
            if (value_truthy(r))
                valuearray_push(&out->items, elem);
        } else {
            valuearray_push(&out->items, r);
        }
    }
    pop(vm);
    return true;
}

static bool n_map(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_CLOSURE(args[1])) return false;
    VM *vm = h->vm;
    ObjArray *out = array_new(h);
    if (!hof_collect(vm, args[1], AS_ARRAY(args[0]), false, out))
        return false;
    *result = OBJ_VAL(out);
    return true;
}

static bool n_filter(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_CLOSURE(args[1])) return false;
    VM *vm = h->vm;
    ObjArray *out = array_new(h);
    if (!hof_collect(vm, args[1], AS_ARRAY(args[0]), true, out))
        return false;
    *result = OBJ_VAL(out);
    return true;
}

static bool n_each(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_CLOSURE(args[1])) return false;
    VM *vm = h->vm;
    ObjArray *src = AS_ARRAY(args[0]);
    for (size_t i = 0; i < src->items.count; i++) {
        Value r;
        if (!vm_call1(vm, args[1], src->items.values[i], &r))
            return false;
    }
    *result = NIL_VAL;
    return true;
}

InterpretResult vm_run_function(VM *vm, ObjFunction *fn)
{
    push(vm, OBJ_VAL(fn));
    ObjClosure *closure = closure_new(&vm->heap, fn);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    if (!call(vm, closure, 0))
        return INTERPRET_RUNTIME_ERROR;
    return run(vm, 0);
}

InterpretResult vm_interpret(VM *vm, const char *source, size_t len)
{
    ObjFunction *fn = compile(&vm->heap, source, len);
    if (fn == NULL)
        return INTERPRET_COMPILE_ERROR;
    return vm_run_function(vm, fn);
}
