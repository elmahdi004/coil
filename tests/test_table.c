/* test_table.c -- first-class table (hash map) behaviour. */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    VM vm;
    vm_init(&vm);

    const char *src =
        "let t = { \"a\": 1, \"b\": 2, 3: \"three\", true: 100 };\n"
        "print t[\"a\"]; print t[\"b\"]; print t[3]; print t[true];\n"   /* 1 2 three 100 */
        "t[\"c\"] = 99; print t[\"c\"];\n"                                /* 99 */
        "print t[\"missing\"];\n"                                         /* nil */
        "let big = {};\n"
        "for (let i = 0; i < 200; i = i + 1) { big[i] = i * i; }\n"       /* force rehash */
        "let s = 0;\n"
        "for (let i = 0; i < 200; i = i + 1) { s = s + big[i]; }\n"
        "print s;\n";                                                     /* 2646700 */

    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    printf("[result=%d]\n", r);
    return r == INTERPRET_OK ? 0 : 1;
}
