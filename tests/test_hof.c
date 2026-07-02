/*
 * test_hof.c -- higher-order library natives (map / filter / each).
 *
 * These natives re-enter the interpreter to run a Coil callback per element.
 * The callbacks here are small and allocate little, so a normal build never
 * collects mid-call; under -DCOIL_GC_STRESS (collect on every allocation) the
 * allocating-callback cases exercise object survival across the re-entry.
 */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

static int run(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_OK;
}

static int errs(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_RUNTIME_ERROR;
}

int main(void)
{
    /* map: transform each element */
    if (!run(
        "fn dbl(x) { return x * 2; }"
        "let m = map([1, 2, 3, 4], dbl);"
        "assert(len(m) == 4);"
        "assert(m[0] == 2); assert(m[1] == 4); assert(m[3] == 8);"
        "assert(len(map([], dbl)) == 0);")) { puts("FAIL hof.map"); return 1; }

    /* filter: keep elements whose callback is truthy */
    if (!run(
        "fn even(x) { return x % 2 == 0; }"
        "let f = filter([1, 2, 3, 4, 5, 6], even);"
        "assert(len(f) == 3);"
        "assert(f[0] == 2); assert(f[1] == 4); assert(f[2] == 6);"
        "fn no(x) { return false; }"
        "assert(len(filter([1, 2, 3], no)) == 0);")) { puts("FAIL hof.filter"); return 1; }

    /* each: side effects only, returns nil */
    if (!run(
        "let acc = [];"
        "fn collect(x) { push(acc, x + 100); }"
        "each([5, 6, 7], collect);"
        "assert(len(acc) == 3);"
        "assert(acc[0] == 105); assert(acc[2] == 107);")) { puts("FAIL hof.each"); return 1; }

    /* map with an allocating callback (each call builds a string) */
    if (!run(
        "fn tag(x) { return str(x) + \"!\"; }"
        "let t = map([1, 2, 3], tag);"
        "assert(len(t) == 3);"
        "assert(t[0] == \"1!\"); assert(t[2] == \"3!\");")) { puts("FAIL hof.map.alloc"); return 1; }

    /* nested: map whose callback itself calls a higher-order native */
    if (!run(
        "fn sq(x) { return x * x; }"
        "fn rowsum(r) { return sum(map([1, 2, 3], sq)) + r; }"
        "let n = map([0, 10], rowsum);"
        "assert(n[0] == 14); assert(n[1] == 24);")) { puts("FAIL hof.nested"); return 1; }

    /* error paths return cleanly */
    if (!errs("print map(5, 5);"))                       { puts("FAIL hof.err.map"); return 1; }
    if (!errs("fn f(x){return x;} print map([1], 7);"))  { puts("FAIL hof.err.fn"); return 1; }
    if (!errs("print filter([1, 2], 3);"))               { puts("FAIL hof.err.filter"); return 1; }

    puts("PASS test_hof");
    return 0;
}
