/* test_natives2.c -- array/string/math natives added in the second batch. */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    VM vm;
    vm_init(&vm);

    const char *src =
        "print join([\"a\", \"b\", \"c\"], \"-\");\n"          /* a-b-c */
        "let a = [3, 1, 2]; sort(a); print a[0]; print a[1]; print a[2];\n"  /* 1 2 3 */
        "reverse(a); print a[0];\n"                            /* 3 */
        "let sl = slice([10, 20, 30, 40], 1, 3); print sl[0]; print sl[1]; print len(sl);\n"  /* 20 30 2 */
        "let parts = split(\"a,b,c\", \",\"); print len(parts); print parts[1];\n"  /* 3 b */
        "print contains(\"hello\", \"ell\");\n"                /* true */
        "print replace(\"aXbXc\", \"X\", \"-\");\n"            /* a-b-c */
        "print join([1, 2, 3], \"+\");\n"                      /* 1+2+3 */
        "print floor(cos(0.0));\n"                             /* 1 */
        "print round(2.6);\n";                                 /* 3 */

    InterpretResult res = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    printf("[result=%d]\n", res);
    return res == INTERPRET_OK ? 0 : 1;
}
