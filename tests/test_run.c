/* test_run.c -- execute real programs end to end. */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    VM vm;
    vm_init(&vm);

    const char *src =
        "fn fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\n"
        "print fib(10);\n"                                  /* 55 */
        "fn make() { let c = 0; fn inc() { c = c + 1; return c; } return inc; }\n"
        "let counter = make();\n"
        "print counter(); print counter(); print counter();\n"  /* 1 2 3 */
        "let a = [1, 2, 3, 4]; let s = 0;\n"
        "for (let i = 0; i < 4; i = i + 1) { s = s + a[i]; }\n"
        "print s;\n"                                         /* 10 */
        "a[2] = 100; print a[2];\n"                          /* 100 */
        "print \"hello \" + \"world\";\n"                    /* hello world */
        "print 7 / 2; print 7.0 / 2; print 7 % 3;\n";        /* 3, 3.5, 1 */

    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    printf("[result=%d]\n", r);
    return r == INTERPRET_OK ? 0 : 1;
}
