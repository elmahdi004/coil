/*
 * gen_bytecode_seeds.c -- compile sample programs and write their serialized
 * bytecode modules, to seed the bytecode fuzzer.
 *
 *   gen_bytecode_seeds <out_dir>
 */

#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *dir, const char *name, const char *src)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *fn = compile(&vm.heap, src, strlen(src));
    if (fn) {
        uint8_t *bytes = NULL;
        size_t   len   = 0;
        if (module_serialize(fn, &bytes, &len) == MODULE_OK) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(bytes, 1, len, f); fclose(f); printf("%s: %zu bytes\n", name, len); }
            free(bytes);
        }
    }
    vm_free(&vm);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    dump(dir, "seed_arith",   "let x = 1 + 2 * 3; print x; print x * x - 7;");
    dump(dir, "seed_fib",     "fn fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } print fib(8);");
    dump(dir, "seed_closure", "fn mk() { let c = 0; fn i() { c = c + 1; return c; } return i; } let f = mk(); print f(); print f();");
    dump(dir, "seed_array",   "let a = [1, 2, 3, 4]; let s = 0; for (let i = 0; i < 4; i = i + 1) { s = s + a[i]; } print s;");
    dump(dir, "seed_loop",    "let i = 0; while (i < 10) { print i; i = i + 1; }");
    dump(dir, "seed_str",     "fn g(x) { return x + \"!\"; } print g(\"hi\"); let b = true and false; print b or true;");
    return 0;
}
