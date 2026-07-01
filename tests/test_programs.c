/*
 * test_programs.c -- end-to-end algorithms that lean on many features at once.
 *
 * Each is a self-contained Coil program that checks its own output with assert,
 * so it doubles as an integration test for the compiler, VM, GC and stdlib.
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
    /* sieve of Eratosthenes */
    if (!run(
        "fn sieve(n) {"
        "  let is_p = fill(n, true);"
        "  let count = 0;"
        "  for (let i = 2; i < n; i = i + 1) {"
        "    if (is_p[i]) {"
        "      count = count + 1;"
        "      let j = i * i;"
        "      while (j < n) { is_p[j] = false; j = j + i; }"
        "    }"
        "  }"
        "  return count;"
        "}"
        "assert(sieve(30) == 10);"          /* 2,3,5,7,11,13,17,19,23,29 */
        "assert(sieve(100) == 25);")) { puts("FAIL prog.sieve"); return 1; }

    /* bubble sort, in place */
    if (!run(
        "fn bubble(a) {"
        "  let n = len(a);"
        "  for (let i = 0; i < n; i = i + 1) {"
        "    for (let j = 0; j < n - 1 - i; j = j + 1) {"
        "      if (a[j] > a[j + 1]) { let t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }"
        "    }"
        "  }"
        "  return a;"
        "}"
        "let s = bubble([5, 2, 8, 1, 9, 3, 7]);"
        "assert(s[0] == 1); assert(s[6] == 9);"
        "for (let i = 0; i < len(s) - 1; i = i + 1) { assert(s[i] <= s[i + 1]); }")) { puts("FAIL prog.bubble"); return 1; }

    /* recursive quicksort building fresh arrays (uses concat) */
    if (!run(
        "fn qsort(a) {"
        "  if (len(a) <= 1) { return a; }"
        "  let pivot = a[0];"
        "  let less = []; let more = [];"
        "  for (let i = 1; i < len(a); i = i + 1) {"
        "    if (a[i] < pivot) { push(less, a[i]); } else { push(more, a[i]); }"
        "  }"
        "  return concat(concat(qsort(less), [pivot]), qsort(more));"
        "}"
        "let r = qsort([3, 6, 1, 8, 2, 9, 4, 0, 5, 7]);"
        "for (let i = 0; i < len(r) - 1; i = i + 1) { assert(r[i] <= r[i + 1]); }"
        "assert(r[0] == 0); assert(r[9] == 9);")) { puts("FAIL prog.qsort"); return 1; }

    /* memoized fibonacci via a table */
    if (!run(
        "let memo = {};"
        "fn fib(n) {"
        "  if (n < 2) { return n; }"
        "  if (has(memo, n)) { return memo[n]; }"
        "  let r = fib(n - 1) + fib(n - 2);"
        "  memo[n] = r;"
        "  return r;"
        "}"
        "assert(fib(30) == 832040);"
        "assert(fib(40) == 102334155);")) { puts("FAIL prog.fib"); return 1; }

    /* string algorithms: palindrome and reverse */
    if (!run(
        "fn rev(s) { let out = \"\"; for (let i = len(s) - 1; i >= 0; i = i - 1) { out = out + char_at(s, i); } return out; }"
        "fn is_pal(s) {"
        "  let n = len(s);"
        "  for (let i = 0; i < n / 2; i = i + 1) { if (char_at(s, i) != char_at(s, n - 1 - i)) { return false; } }"
        "  return true;"
        "}"
        "assert(rev(\"abcde\") == \"edcba\"); assert(rev(\"\") == \"\");"
        "assert(is_pal(\"racecar\")); assert(is_pal(\"\")); assert(!is_pal(\"hello\"));")) { puts("FAIL prog.strings"); return 1; }

    /* Collatz step counts */
    if (!run(
        "fn collatz(n) {"
        "  let steps = 0;"
        "  while (n != 1) { if (n % 2 == 0) { n = n / 2; } else { n = 3 * n + 1; } steps = steps + 1; }"
        "  return steps;"
        "}"
        "assert(collatz(1) == 0); assert(collatz(6) == 8); assert(collatz(27) == 111);")) { puts("FAIL prog.collatz"); return 1; }

    /* a tiny stack machine interpreter, written in Coil (RPN evaluator) */
    if (!run(
        "fn rpn(tokens) {"
        "  let stack = [];"
        "  for (let i = 0; i < len(tokens); i = i + 1) {"
        "    let tk = tokens[i];"
        "    if (tk == \"+\") { let b = pop(stack); let a = pop(stack); push(stack, a + b); }"
        "    else { if (tk == \"*\") { let b = pop(stack); let a = pop(stack); push(stack, a * b); }"
        "    else { push(stack, tk); } }"
        "  }"
        "  return pop(stack);"
        "}"
        "assert(rpn([2, 3, \"+\", 4, \"*\"]) == 20);"     /* (2+3)*4 */
        "assert(rpn([5, 1, 2, \"+\", \"*\"]) == 15);")) { puts("FAIL prog.rpn"); return 1; }

    puts("PASS test_programs");
    return 0;
}
