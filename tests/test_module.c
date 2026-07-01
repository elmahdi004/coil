/* test_module.c -- bytecode serialize/load round-trip. */
#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    const char *src =
        "fn fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\n"
        "print fib(12);\n"                       /* 144 */
        "let a = [1, 2, 3]; print a[0] + a[1] + a[2];\n"  /* 6 */
        "print \"coil \" + \"vm\";\n";           /* coil vm */

    /* compile + serialize in one VM */
    VM vm1;
    vm_init(&vm1);
    ObjFunction *fn = compile(&vm1.heap, src, strlen(src));
    assert(fn != NULL);

    uint8_t *bytes = NULL;
    size_t   len   = 0;
    assert(module_serialize(fn, &bytes, &len) == MODULE_OK);
    assert(bytes && len > 0);
    printf("serialized to %zu bytes\n", len);
    vm_free(&vm1);                                /* fn is gone; bytes survive */

    /* load + run in a fresh VM */
    VM vm2;
    vm_init(&vm2);
    ObjFunction *entry = NULL;
    assert(module_load(&vm2.heap, bytes, len, &entry) == MODULE_OK);
    assert(entry != NULL);
    puts("--- running loaded module ---");
    assert(vm_run_function(&vm2, entry) == INTERPRET_OK);
    vm_free(&vm2);
    free(bytes);

    puts("PASS test_module");
    return 0;
}
