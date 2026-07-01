/*
 * test_closures.c -- closures, upvalues, recursion and their interaction.
 *
 * This exercises capture_upvalue / close_upvalues on the value stack, which is
 * the machinery most sensitive to any change in how frames are laid out.
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

int main(void)
{
    /* a counter closes over a mutable local and mutates it across calls */
    if (!run(
        "fn make() { let c = 0; fn inc() { c = c + 1; return c; } return inc; }"
        "let a = make();"
        "assert(a() == 1); assert(a() == 2); assert(a() == 3);")) { puts("FAIL clo.counter"); return 1; }

    /* two counters must be independent (distinct upvalues) */
    if (!run(
        "fn make() { let c = 0; fn inc() { c = c + 1; return c; } return inc; }"
        "let a = make(); let b = make();"
        "assert(a() == 1); assert(a() == 2);"
        "assert(b() == 1);"                /* b unaffected by a */
        "assert(a() == 3); assert(b() == 2);")) { puts("FAIL clo.independent"); return 1; }

    /* a partially-applied adder captures its argument */
    if (!run(
        "fn adder(x) { fn add(y) { return x + y; } return add; }"
        "let add5 = adder(5); let add10 = adder(10);"
        "assert(add5(3) == 8); assert(add10(3) == 13);"
        "assert(add5(100) == 105);")) { puts("FAIL clo.adder"); return 1; }

    /* upvalue chained two levels deep (inner captures a middle-scope local) */
    if (!run(
        "fn outer(a) {"
        "  fn middle(b) {"
        "    fn inner(c) { return a + b + c; }"
        "    return inner;"
        "  }"
        "  return middle;"
        "}"
        "let f = outer(1)(2);"
        "assert(f(3) == 6); assert(f(10) == 13);")) { puts("FAIL clo.chain"); return 1; }

    /* recursion: fibonacci and factorial */
    if (!run(
        "fn fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); }"
        "assert(fib(10) == 55); assert(fib(15) == 610);"
        "fn fact(n) { if (n <= 1) { return 1; } return n * fact(n-1); }"
        "assert(fact(5) == 120); assert(fact(10) == 3628800);")) { puts("FAIL clo.recursion"); return 1; }

    /* mutual recursion through globals */
    if (!run(
        "fn is_even(n) { if (n == 0) { return true; } return is_odd(n - 1); }"
        "fn is_odd(n) { if (n == 0) { return false; } return is_even(n - 1); }"
        "assert(is_even(10)); assert(is_odd(7)); assert(!is_even(3));")) { puts("FAIL clo.mutual"); return 1; }

    /* a closure returned from a loop body captures the final loop value */
    if (!run(
        "fn build() {"
        "  let acc = 0;"
        "  for (let i = 1; i <= 5; i = i + 1) { acc = acc + i; }"
        "  fn get() { return acc; }"
        "  return get;"
        "}"
        "assert(build()() == 15);")) { puts("FAIL clo.loop"); return 1; }

    puts("PASS test_closures");
    return 0;
}
