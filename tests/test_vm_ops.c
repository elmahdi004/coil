/*
 * test_vm_ops.c -- arithmetic, comparisons, arrays and tables end to end.
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
    /* integer arithmetic uses defined wrapping and truncating division */
    if (!run(
        "assert(2 + 3 == 5);"
        "assert(10 - 4 == 6);"
        "assert(6 * 7 == 42);"
        "assert(7 / 2 == 3);"              /* integer division truncates */
        "assert(7 % 3 == 1);"
        "assert(2 * 3 + 4 == 10);"
        "assert(2 + 3 * 4 == 14);"
        "assert(-5 == 0 - 5);"
        "assert(-(3 + 2) == -5);")) { puts("FAIL ops.int"); return 1; }

    /* float arithmetic and int/float mixing promote to float */
    if (!run(
        "assert(7.0 / 2 == 3.5);"
        "assert(1.5 + 1.5 == 3.0);"
        "assert(1 + 2.0 == 3.0);"
        "assert(10.0 - 4 == 6.0);"
        "assert(0.1 + 0.2 > 0.29);")) { puts("FAIL ops.float"); return 1; }

    /* comparisons and equality across numeric types */
    if (!run(
        "assert(1 < 2); assert(2 <= 2); assert(3 > 2); assert(3 >= 3);"
        "assert(1 == 1); assert(1 != 2);"
        "assert(1 == 1.0);"                /* cross-type numeric equality */
        "assert(!(1 == 2));"
        "assert((\"ab\" == \"ab\"));"      /* strings interned -> identity */
        "assert(\"ab\" != \"ac\");"
        "assert(nil == nil);"
        "assert(true != false);")) { puts("FAIL ops.cmp"); return 1; }

    /* and / or return operands and short-circuit */
    if (!run(
        "assert((true and 5) == 5);"
        "assert((false and 5) == false);"
        "assert((false or 7) == 7);"
        "assert((nil or 3) == 3);"
        "assert((0 or 9) == 0);")) { puts("FAIL ops.logic"); return 1; }

    /* arrays: literal, index get/set, len, push/pop */
    if (!run(
        "let a = [1, 2, 3];"
        "assert(a[0] == 1); assert(a[2] == 3); assert(len(a) == 3);"
        "a[1] = 9; assert(a[1] == 9);"
        "push(a, 4); assert(len(a) == 4); assert(a[3] == 4);"
        "assert(pop(a) == 4); assert(len(a) == 3);"
        "let m = [[1, 2], [3, 4]];"
        "assert(m[0][1] == 2); assert(m[1][0] == 3);")) { puts("FAIL ops.array"); return 1; }

    /* tables: string and int keys, has/del, len tracks live entries */
    if (!run(
        "let t = {\"x\": 1, \"y\": 2};"
        "assert(t[\"x\"] == 1); assert(t[\"y\"] == 2);"
        "t[\"z\"] = 3; assert(t[\"z\"] == 3); assert(len(t) == 3);"
        "assert(has(t, \"x\")); assert(!has(t, \"nope\"));"
        "del(t, \"x\"); assert(!has(t, \"x\")); assert(len(t) == 2);"
        "let n = {1: \"a\", 2: \"b\"};"
        "assert(n[1] == \"a\"); assert(n[2] == \"b\");"
        "assert(n[99] == nil);")) { puts("FAIL ops.table"); return 1; }

    /* control flow: if/else, while, for accumulate */
    if (!run(
        "let s = 0;"
        "for (let i = 0; i < 10; i = i + 1) { s = s + i; }"
        "assert(s == 45);"
        "let n = 10; let f = 1;"
        "while (n > 1) { f = f * n; n = n - 1; }"
        "assert(f == 3628800);"
        "let x = 7;"
        "if (x > 5) { x = x * 2; } else { x = 0; }"
        "assert(x == 14);")) { puts("FAIL ops.control"); return 1; }

    puts("PASS test_vm_ops");
    return 0;
}
