/*
 * test_optimize.c -- the peephole optimizer preserves behaviour and verifiability.
 *
 * The key property is differential: for every program, the optimized function
 * must still pass the verifier and run to the same result (each program
 * self-checks with assert).  Separately we confirm the optimizer actually
 * removes the waste it claims to.
 */
#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/optimize.h"
#include "../src/verify.h"
#include "../src/asm.h"
#include <stdio.h>
#include <string.h>

/* compile, optimize, verify the optimized form, and run it. */
static int optimized_runs(const char *src)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *fn = compile(&vm.heap, src, strlen(src));
    int ok = 0;
    if (fn) {
        ObjFunction *opt = optimize_function(&vm.heap, fn);
        if (opt && verify_module(opt) == VERIFY_OK)
            ok = (vm_run_function(&vm, opt) == INTERPRET_OK);
    }
    vm_free(&vm);
    return ok;
}

static int test_behaviour(void)
{
    const char *progs[] = {
        "assert(-5 + 10 == 5);",
        "assert(!(1 == 2));",
        "let x = -(3 * 3); assert(x == -9);",
        "fn fib(n){ if(n<2){return n;} return fib(n-1)+fib(n-2);} assert(fib(12)==144);",
        "fn make(){ let c=0; fn inc(){ c=c+1; return c; } return inc; }"
        "let a=make(); assert(a()==1); assert(a()==2); assert(a()==3);",
        "let s=0; for(let i=0;i<10;i=i+1){ s=s+i; } assert(s==45);",
        "let a=[1,2,3]; let t={\"k\":a[1]}; assert(t[\"k\"]==2);",
        "assert(((~0) & 255) == 255);",
        "let n=6; let f=1; while(n>1){ f=f*n; n=n-1; } assert(f==720);",
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++)
        if (!optimized_runs(progs[i])) { printf("FAIL optimize.behaviour #%zu\n", i); return 0; }
    puts("  behaviour: ok");
    return 1;
}

static size_t code_size(const char *src, int optimize)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *fn = compile(&vm.heap, src, strlen(src));
    size_t sz = 0;
    if (fn) {
        if (optimize) { ObjFunction *o = optimize_function(&vm.heap, fn); sz = o ? o->chunk.count : 0; }
        else sz = fn->chunk.count;
    }
    vm_free(&vm);
    return sz;
}

static int test_shrinks(void)
{
    /* unary constant folding removes a NEG */
    if (!(code_size("print -5;", 1) < code_size("print -5;", 0))) { puts("FAIL shrink.fold"); return 0; }
    /* !(false) folds to a boolean constant */
    if (!(code_size("print !false;", 1) < code_size("print !false;", 0))) { puts("FAIL shrink.not"); return 0; }
    puts("  shrinks: ok");
    return 1;
}

static int test_dead_code(void)
{
    /* code after RETURN is unreachable and must be dropped by the optimizer,
     * while the reachable part still verifies and runs. */
    const char *text =
        ".arity 0\n.slots 4\n"
        "  CONST 42\n"
        "  RETURN\n"
        "  CONST 1\n"      /* dead */
        "  CONST 2\n"      /* dead */
        "  ADD\n"          /* dead */
        "  PRINT\n";       /* dead */
    VM vm;
    vm_init(&vm);
    char err[128] = {0};
    ObjFunction *fn = coil_assemble_fn(&vm.heap, text, strlen(text), err, sizeof err);
    int ok = 0;
    if (fn) {
        size_t before = fn->chunk.count;
        ObjFunction *opt = optimize_function(&vm.heap, fn);
        if (opt && opt->chunk.count < before &&
            verify_module(opt) == VERIFY_OK &&
            vm_run_function(&vm, opt) == INTERPRET_OK)
            ok = 1;
    }
    vm_free(&vm);
    if (!ok) { puts("FAIL optimize.dead-code"); return 0; }
    puts("  dead-code: ok");
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_behaviour();
    ok &= test_shrinks();
    ok &= test_dead_code();
    if (!ok) return 1;
    puts("PASS test_optimize");
    return 0;
}
