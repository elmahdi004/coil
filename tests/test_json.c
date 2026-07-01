/* test_json.c -- JSON parse/stringify via the native bridge. */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    VM vm;
    vm_init(&vm);

    const char *src =
        "let v = [1, 2, { \"x\": 3.5, \"y\": \"hi\", \"z\": [true, nil, false] }];\n"
        "let s = json_str(v);\n"
        "print s;\n"
        "let p = json_parse(s);\n"
        "print p[0];\n"                 /* 1 */
        "print p[2][\"x\"];\n"          /* 3.5 */
        "print p[2][\"y\"];\n"          /* hi */
        "print p[2][\"z\"][0];\n"       /* true */
        "print len(p);\n"              /* 3 */
        "let n = json_parse(\"[10, 20, 30]\");\n"
        "print n[0] + n[1] + n[2];\n";  /* 60 */

    InterpretResult res = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    printf("[result=%d]\n", res);
    return res == INTERPRET_OK ? 0 : 1;
}
