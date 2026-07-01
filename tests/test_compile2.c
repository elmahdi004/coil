/*
 * test_compile2.c -- compiler corner cases: scoping, shadowing, precedence,
 * assignment-as-expression, and string escapes.
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
    /* block scoping and shadowing */
    if (!run(
        "let x = 1;"
        "{ let x = 2; assert(x == 2); { let x = 3; assert(x == 3); } assert(x == 2); }"
        "assert(x == 1);")) { puts("FAIL c2.shadow"); return 1; }

    /* inner scope can see and mutate an outer local */
    if (!run(
        "fn f() { let a = 10; { a = a + 5; let b = a * 2; assert(b == 30); } return a; }"
        "assert(f() == 15);")) { puts("FAIL c2.outerlocal"); return 1; }

    /* operator precedence and left associativity */
    if (!run(
        "assert(2 + 3 * 4 - 1 == 13);"
        "assert(10 - 2 - 3 == 5);"          /* left assoc */
        "assert(2 * 3 % 4 == 2);"
        "assert(-2 * 3 == -6);"
        "assert(2 + 3 < 4 + 4);"
        "assert((1 < 2) == (3 > 2));"
        "assert(1 + 2 == 3 and 4 * 5 == 20);")) { puts("FAIL c2.prec"); return 1; }

    /* assignment is an expression yielding the assigned value (chained) */
    if (!run(
        "let a = 0; let b = 0;"
        "a = b = 7;"
        "assert(a == 7); assert(b == 7);"
        "let c = (a = 3);"
        "assert(a == 3); assert(c == 3);")) { puts("FAIL c2.assign"); return 1; }

    /* string escapes decode correctly */
    if (!run(
        "assert(len(\"a\\nb\") == 3);"
        "assert(\"a\\tb\" == \"a\" + chr(9) + \"b\");"
        "assert(\"quote: \\\"x\\\"\" == \"quote: \" + chr(34) + \"x\" + chr(34));"
        "assert(\"back\\\\slash\" == \"back\" + chr(92) + \"slash\");")) { puts("FAIL c2.escapes"); return 1; }

    /* functions: empty body, bare return, and many locals */
    if (!run(
        "fn empty() {}"
        "assert(empty() == nil);"
        "fn early(x) { if (x > 0) { return; } return -1; }"
        "assert(early(5) == nil); assert(early(-1) == -1);"
        "fn many() {"
        "  let a=1; let b=2; let c=3; let d=4; let e=5; let f=6; let g=7; let h=8;"
        "  return a+b+c+d+e+f+g+h;"
        "}"
        "assert(many() == 36);")) { puts("FAIL c2.functions"); return 1; }

    /* for-loop clause variants */
    if (!run(
        "let s = 0; let i = 0;"
        "for (; i < 5; ) { s = s + i; i = i + 1; }"       /* no init/incr */
        "assert(s == 10);"
        "let t = 0;"
        "for (let j = 0; j < 3; j = j + 1) { t = t + 1; }"
        "assert(t == 3);")) { puts("FAIL c2.for"); return 1; }

    /* deeply nested grouping */
    if (!run(
        "assert(((((1 + 2) * 3) - 4) / 1) == 5);"
        "assert(-(-(-5)) == -5);"
        "assert(!(!(!(false))) == true);")) { puts("FAIL c2.nesting"); return 1; }

    puts("PASS test_compile2");
    return 0;
}
