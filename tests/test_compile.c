/* test_compile.c -- compile a program and disassemble it (smoke test) */
#include "../src/compiler.h"
#include "../src/debug.h"
#include "../src/memory.h"
#include "../src/object.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    Heap h;
    heap_init(&h);

    const char *src =
        "let x = 1 + 2 * 3;\n"
        "fn add(a, b) { return a + b; }\n"
        "fn make_counter() { let c = 0; fn inc() { c = c + 1; return c; } return inc; }\n"
        "print add(x, 4);\n"
        "if (x > 5) { print \"big\"; } else { print \"small\"; }\n"
        "let arr = [10, 20, 30];\n"
        "arr[1] = 99;\n"
        "for (let i = 0; i < 3; i = i + 1) { print arr[i]; }\n";

    ObjFunction *fn = compile(&h, src, strlen(src));
    assert(fn != NULL);
    disassemble_chunk(&fn->chunk, "script");

    heap_free(&h);
    puts("PASS test_compile");
    return 0;
}
