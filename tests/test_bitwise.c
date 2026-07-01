/*
 * test_bitwise.c -- the bitwise / shift / logical-not operators.
 *
 * Each program asserts its own results with the `assert` native, so a wrong
 * answer surfaces as INTERPRET_RUNTIME_ERROR rather than a silent miscompare.
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
    /* and / or / xor / not on small integers */
    if (!run(
        "assert((12 & 10) == 8);"
        "assert((12 | 10) == 14);"
        "assert((12 ^ 10) == 6);"
        "assert(~0 == -1);"
        "assert(~5 == -6);"
        "assert((0 & 255) == 0);"
        "assert((255 | 0) == 255);")) { puts("FAIL bitwise.basic"); return 1; }

    /* shifts, including the &63 masking of the shift amount */
    if (!run(
        "assert((1 << 4) == 16);"
        "assert((1 << 0) == 1);"
        "assert((256 >> 2) == 64);"
        "assert((1024 >> 10) == 1);"
        "assert((1 << 64) == 1);"          /* 64 & 63 == 0 -> identity */
        "assert((1 << 65) == 2);"          /* 65 & 63 == 1 */
        "assert((-1 >> 0) == -1);")) { puts("FAIL bitwise.shift"); return 1; }

    /* precedence: & tighter than ^ tighter than |; shift looser than + */
    if (!run(
        "assert((1 | 2 & 3) == 3);"        /* 2&3=2, 1|2=3            */
        "assert((1 ^ 3 & 1) == 0);"        /* 3&1=1, 1^1=0            */
        "assert((1 + 1 << 4) == 32);"      /* (1+1)<<4               */
        "assert((1 << 2 | 1) == 5);")) { puts("FAIL bitwise.prec"); return 1; }

    /* the classic xor-swap proves & temporaries round-trip exactly */
    if (!run(
        "let x = 5; let y = 9;"
        "x = x ^ y; y = x ^ y; x = x ^ y;"
        "assert(x == 9); assert(y == 5);")) { puts("FAIL bitwise.xorswap"); return 1; }

    /* logical not over truthiness (only nil and false are falsy) */
    if (!run(
        "assert((!false) == true);"
        "assert((!true) == false);"
        "assert(!nil);"
        "assert((!0) == false);"           /* 0 is truthy */
        "assert((!\"\") == false);"        /* the empty string is truthy */
        "assert((!!5) == true);")) { puts("FAIL bitwise.not"); return 1; }

    /* a small bit-twiddle: popcount of 0xFF via repeated &1 and >>1 */
    if (!run(
        "fn popcount(n) {"
        "  let c = 0;"
        "  while (n != 0) { c = c + (n & 1); n = n >> 1; }"
        "  return c;"
        "}"
        "assert(popcount(255) == 8);"
        "assert(popcount(0) == 0);"
        "assert(popcount(1024) == 1);"
        "assert(popcount(7) == 3);")) { puts("FAIL bitwise.popcount"); return 1; }

    /* bitwise on non-integers must be rejected at runtime, not crash */
    {
        VM vm; vm_init(&vm);
        const char *bad = "print 1 & 2.5;";
        InterpretResult r = vm_interpret(&vm, bad, strlen(bad));
        vm_free(&vm);
        if (r != INTERPRET_RUNTIME_ERROR) { puts("FAIL bitwise.typecheck"); return 1; }
    }

    puts("PASS test_bitwise");
    return 0;
}
