/*
 * vm.h -- the bytecode virtual machine.
 */

#ifndef COIL_VM_H
#define COIL_VM_H

#include "object.h"
#include "table.h"
#include "memory.h"

#define FRAMES_MAX      64
#define MAX_FRAME_STACK 512                       /* verifier bounds per-frame height */
#define STACK_MAX       (FRAMES_MAX * MAX_FRAME_STACK)

typedef struct {
    ObjClosure *closure;
    uint8_t    *ip;          /* into closure->fn->chunk.code */
    Value      *slots;       /* this frame's window into the value stack */
} CallFrame;

struct VM {
    Heap        heap;        /* the VM owns the heap */
    Value      *stack;       /* operand stack, STACK_MAX slots (heap-allocated) */
    Value      *sp;
    CallFrame   frames[FRAMES_MAX];
    int         frame_count;
    Table       globals;
    ObjUpvalue *open_upvalues;
};
typedef struct VM VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void vm_init(VM *vm);
void vm_free(VM *vm);
InterpretResult vm_interpret(VM *vm, const char *source, size_t len);

/* Run an already-built (e.g. loaded) top-level function. */
InterpretResult vm_run_function(VM *vm, ObjFunction *fn);

/* Register a native function as a global (used by the standard library). */
void vm_define_native(VM *vm, const char *name, NativeFn fn, int arity);

#endif /* COIL_VM_H */
