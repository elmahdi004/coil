/*
 * test_natives3.c -- the extended standard library (math / array / string).
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
    if (!run(
        "assert(sign(-5) == -1); assert(sign(0) == 0); assert(sign(3.2) == 1);"
        "assert(clamp(5, 0, 10) == 5); assert(clamp(-1, 0, 10) == 0); assert(clamp(99, 0, 10) == 10);"
        "assert(gcd(12, 18) == 6); assert(gcd(-12, 8) == 4); assert(gcd(7, 0) == 7); assert(gcd(0, 0) == 0);"
        "assert(hypot(3, 4) == 5.0);")) { puts("FAIL nat3.math1"); return 1; }

    if (!run(
        "assert(abs(log2(8) - 3.0) < 0.0001); assert(abs(log10(1000) - 3.0) < 0.0001); assert(abs(cbrt(27) - 3.0) < 0.0001);"
        "assert(abs(atan(0)) < 0.0001); assert(abs(asin(0)) < 0.0001); assert(abs(acos(1)) < 0.0001);"
        "assert(abs(deg(pi()) - 180.0) < 0.0001);"
        "assert(abs(rad(180) - pi()) < 0.0001);"
        "assert(abs(pi() - 3.14159265) < 0.0001);"
        "assert(abs(e() - 2.71828182) < 0.0001);"
        "assert(is_nan(sqrt(-1))); assert(!is_nan(5)); assert(!is_nan(2.5));")) { puts("FAIL nat3.math2"); return 1; }

    if (!run(
        "assert(sum([1, 2, 3, 4]) == 10); assert(sum([1.5, 2.5]) == 4.0); assert(sum([]) == 0);"
        "assert(avg([2, 4, 6]) == 4.0);"
        "assert(max_of([3, 1, 4, 1, 5]) == 5); assert(min_of([3, 1, 4, 1, 5]) == 1);"
        "assert(index_of([10, 20, 30], 20) == 1); assert(index_of([1, 2], 9) == -1);"
        "assert(count([1, 2, 2, 3, 2], 2) == 3);")) { puts("FAIL nat3.agg"); return 1; }

    if (!run(
        "let a = [1, 2, 4];"
        "insert(a, 2, 3);"
        "assert(len(a) == 4); assert(a[2] == 3); assert(a[3] == 4);"
        "insert(a, 0, 0); assert(a[0] == 0); assert(a[1] == 1);"
        "insert(a, len(a), 9); assert(a[len(a) - 1] == 9);"
        "let b = [10, 20, 30];"
        "assert(remove_at(b, 1) == 20); assert(len(b) == 2); assert(b[0] == 10); assert(b[1] == 30);"
        "let c = concat([1, 2], [3, 4]); assert(len(c) == 4); assert(c[0] == 1); assert(c[3] == 4);"
        "let d = fill(3, 7); assert(len(d) == 3); assert(d[0] == 7); assert(d[2] == 7);"
        "assert(len(fill(0, 1)) == 0);")) { puts("FAIL nat3.mutate"); return 1; }

    if (!run(
        "assert(trim(\"  hi  \") == \"hi\"); assert(trim(\"x\") == \"x\"); assert(trim(\"   \") == \"\");"
        "assert(starts_with(\"hello\", \"he\")); assert(!starts_with(\"hello\", \"lo\"));"
        "assert(ends_with(\"hello\", \"lo\")); assert(!ends_with(\"hi\", \"hello\"));"
        "assert(pad_left(\"7\", 3, \"0\") == \"007\"); assert(pad_left(\"abcd\", 3, \"0\") == \"abcd\");"
        "assert(pad_right(\"7\", 3, \"0\") == \"700\");"
        "assert(char_at(\"hello\", 1) == \"e\"); assert(char_at(\"hello\", 0) == \"h\");"
        "assert(hex(255) == \"ff\"); assert(hex(0) == \"0\"); assert(hex(16) == \"10\");")) { puts("FAIL nat3.str"); return 1; }

    /* error paths return cleanly */
    if (!errs("print gcd(1.5, 2);"))        { puts("FAIL nat3.err.gcd"); return 1; }
    if (!errs("print char_at(\"hi\", 5);"))  { puts("FAIL nat3.err.charat"); return 1; }
    if (!errs("let a = [1]; insert(a, 9, 0);")) { puts("FAIL nat3.err.insert"); return 1; }
    if (!errs("print remove_at([], 0);"))    { puts("FAIL nat3.err.removeat"); return 1; }
    if (!errs("print max_of([]);"))          { puts("FAIL nat3.err.maxof"); return 1; }
    if (!errs("print sum([1, \"x\"]);"))     { puts("FAIL nat3.err.sum"); return 1; }
    if (!errs("print avg([]);"))             { puts("FAIL nat3.err.avg"); return 1; }

    puts("PASS test_natives3");
    return 0;
}
