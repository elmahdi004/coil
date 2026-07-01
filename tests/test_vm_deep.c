/*
 * test_vm_deep.c -- recursion depth, large structures and integer edge cases.
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
    /* recursion within the frame limit */
    if (!run(
        "fn sumto(n) { if (n == 0) { return 0; } return n + sumto(n - 1); }"
        "assert(sumto(50) == 1275);")) { puts("FAIL deep.recursion"); return 1; }

    /* runaway recursion overflows the frame stack as a clean runtime error */
    if (!errs("fn boom(n) { return boom(n + 1); } boom(0);")) { puts("FAIL deep.overflow"); return 1; }

    /* large arrays built and summed */
    if (!run(
        "let a = [];"
        "for (let i = 0; i < 10000; i = i + 1) { push(a, i); }"
        "assert(len(a) == 10000);"
        "let total = 0;"
        "for (let i = 0; i < 10000; i = i + 1) { total = total + a[i]; }"
        "assert(total == 49995000);")) { puts("FAIL deep.array"); return 1; }

    /* integer arithmetic is defined under overflow (wraps, no UB) */
    if (!run(
        "let big = 1000000000 * 1000000000 * 1000000000;"
        "assert(type(big) == \"int\");"                 /* wrapped, still an int */
        "let x = 9223372036854775807;"                  /* INT64_MAX */
        "assert(x + 1 < x);")) { puts("FAIL deep.wrap"); return 1; }

    /* C-style truncating division and modulo signs */
    if (!run(
        "assert(7 / 2 == 3); assert(-7 / 2 == -3); assert(-8 / 2 == -4);"
        "assert(7 % 3 == 1); assert(-7 % 3 == -1); assert(7 % -3 == 1);"
        "assert(100 / -1 == -100);")) { puts("FAIL deep.divmod"); return 1; }

    /* eight-argument function call */
    if (!run(
        "fn f(a, b, c, d, e, g, h, i) { return a + b + c + d + e + g + h + i; }"
        "assert(f(1, 2, 3, 4, 5, 6, 7, 8) == 36);")) { puts("FAIL deep.args"); return 1; }

    /* nested heterogeneous structures */
    if (!run(
        "let data = [];"
        "for (let i = 0; i < 100; i = i + 1) { push(data, {\"id\": i, \"sq\": i * i}); }"
        "let s = 0;"
        "for (let i = 0; i < 100; i = i + 1) { s = s + data[i][\"sq\"]; }"
        "assert(s == 328350);"                          /* sum of i^2, 0..99 */
        "assert(data[10][\"id\"] == 10);")) { puts("FAIL deep.nested"); return 1; }

    puts("PASS test_vm_deep");
    return 0;
}
