/* test_natives.c -- the native standard library. */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    VM vm;
    vm_init(&vm);

    const char *src =
        "print type(1); print type(\"x\"); print type([1]); print type({});\n"   /* int string array table */
        "print str(42); print str(true);\n"                                      /* 42 true */
        "print int(3.9); print num(\"2.5\");\n"                                   /* 3 2.5 */
        "print floor(3.7); print ceil(3.2); print sqrt(16.0); print abs(-5); print pow(2, 10);\n"  /* 3 4 4 5 1024 */
        "print min(3, 7); print max(3, 7);\n"                                     /* 3 7 */
        "let a = [1, 2, 3]; push(a, 4); print len(a); print pop(a); print len(a);\n"  /* 4 4 3 */
        "let t = { \"x\": 1 }; t[\"y\"] = 2; print len(t); print has(t, \"x\");\n"     /* 2 true */
        "del(t, \"x\"); print has(t, \"x\");\n"                                   /* false */
        "print ord(\"A\"); print chr(66); print upper(\"abc\"); print lower(\"XYZ\");\n"  /* 65 B ABC xyz */
        "print substr(\"hello\", 1, 3); print find(\"hello\", \"ll\"); print repeat(\"ab\", 3);\n"  /* ell 2 ababab */
        "let r = range(5); print len(r); print r[4];\n"                           /* 5 4 */
        "print len(keys({ \"a\": 1, \"b\": 2 }));\n";                             /* 2 */

    InterpretResult res = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    printf("[result=%d]\n", res);
    return res == INTERPRET_OK ? 0 : 1;
}
