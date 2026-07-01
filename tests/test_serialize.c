/*
 * test_serialize.c -- module (de)serialization is a canonical, lossless codec.
 *
 *   - serialize is deterministic: load(serialize(x)) re-serializes byte-identically;
 *   - a loaded module executes exactly like the source it came from;
 *   - the loader rejects truncated / corrupt headers without crashing.
 */
#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/module.h"
#include "../src/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static uint8_t *compile_module(const char *src, size_t *out_len)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *fn = compile(&vm.heap, src, strlen(src));
    uint8_t *bytes = NULL;
    size_t   len   = 0;
    if (fn)
        module_serialize(fn, &bytes, &len);
    vm_free(&vm);
    *out_len = len;
    return bytes;
}

/* load the bytes, then re-serialize the loaded entry into *out2. */
static int reserialize(const uint8_t *in, size_t in_len, uint8_t **out2, size_t *out2_len)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *entry = NULL;
    int ok = 0;
    if (module_load(&vm.heap, in, in_len, &entry) == MODULE_OK && entry)
        ok = (module_serialize(entry, out2, out2_len) == MODULE_OK);
    vm_free(&vm);
    return ok;
}

static int test_canonical(void)
{
    const char *progs[] = {
        "print 1 + 2;",
        "fn fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } print fib(10);",
        "fn make() { let c = 0; fn inc() { c = c + 1; return c; } return inc; } print make()();",
        "let a = [1, 2, [3, 4]]; let t = {\"a\": 1, \"b\": [5, 6]}; print a[2][1] + t[\"b\"][0];",
        "print \"hello\" + \" \" + \"world\"; print 3.14159; print -42; print true;",
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
        size_t l1 = 0, l2 = 0;
        uint8_t *b1 = compile_module(progs[i], &l1);
        if (!b1) { printf("FAIL canonical compile #%zu\n", i); return 0; }
        uint8_t *b2 = NULL;
        if (!reserialize(b1, l1, &b2, &l2)) { free(b1); printf("FAIL canonical reload #%zu\n", i); return 0; }
        int same = (l1 == l2) && memcmp(b1, b2, l1) == 0;
        free(b1); free(b2);
        if (!same) { printf("FAIL canonical mismatch #%zu (%zu vs %zu)\n", i, l1, l2); return 0; }
    }
    puts("  canonical: ok");
    return 1;
}

static int loaded_runs_ok(const char *src)
{
    size_t len = 0;
    uint8_t *bytes = compile_module(src, &len);
    if (!bytes) return 0;
    VM vm;
    vm_init(&vm);
    ObjFunction *entry = NULL;
    int ok = 0;
    if (module_load(&vm.heap, bytes, len, &entry) == MODULE_OK && entry &&
        verify_module(entry) == VERIFY_OK)
        ok = (vm_run_function(&vm, entry) == INTERPRET_OK);
    vm_free(&vm);
    free(bytes);
    return ok;
}

static int test_exec_equivalence(void)
{
    /* each program self-checks with assert, so INTERPRET_OK means correct. */
    const char *progs[] = {
        "assert(1 + 2 * 3 == 7);",
        "fn fact(n){ if(n<=1){return 1;} return n*fact(n-1);} assert(fact(6) == 720);",
        "let a=[5,3,1,4,2]; sort(a); assert(a[0]==1); assert(a[4]==5);",
        "let t={}; for(let i=0;i<10;i=i+1){ t[i]=i*i; } assert(t[9]==81); assert(len(t)==10);",
        "assert(((255 >> 4) & 15) == 15);",
        "fn adder(x){ fn a(y){return x+y;} return a;} let f=adder(100); assert(f(23)==123);",
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++)
        if (!loaded_runs_ok(progs[i])) { printf("FAIL exec-equiv #%zu\n", i); return 0; }
    puts("  exec-equivalence: ok");
    return 1;
}

/* module_load must return an error (never MODULE_OK) and never crash. */
static int rejects(const uint8_t *bytes, size_t len)
{
    VM vm;
    vm_init(&vm);
    ObjFunction *entry = NULL;
    int r = module_load(&vm.heap, bytes, len, &entry);
    vm_free(&vm);
    return r != MODULE_OK;
}

static int test_reject_headers(void)
{
    /* a known-good module we can corrupt */
    size_t len = 0;
    uint8_t *good = compile_module("print 1 + 2;", &len);
    if (!good || len < 12) { free(good); puts("FAIL reject setup"); return 0; }

    /* every prefix shorter than the whole module must fail to load */
    for (size_t cut = 0; cut < len; cut++)
        if (!rejects(good, cut)) { printf("FAIL accepted truncation at %zu\n", cut); free(good); return 0; }

    uint8_t *bad = (uint8_t *)malloc(len);

    memcpy(bad, good, len); bad[0] = 'X';                 /* wrong magic       */
    if (!rejects(bad, len)) { puts("FAIL bad magic accepted"); goto fail; }

    memcpy(bad, good, len); bad[4] = 0xFF; bad[5] = 0xFF; /* wrong version     */
    if (!rejects(bad, len)) { puts("FAIL bad version accepted"); goto fail; }

    memcpy(bad, good, len); bad[6] = 0; bad[7] = 0;       /* function_count 0  */
    if (!rejects(bad, len)) { puts("FAIL zero fn-count accepted"); goto fail; }

    memcpy(bad, good, len); bad[6] = 0xFF; bad[7] = 0xFF; /* absurd fn-count   */
    if (!rejects(bad, len)) { puts("FAIL huge fn-count accepted"); goto fail; }

    /* empty and tiny inputs */
    if (!rejects((const uint8_t *)"", 0))      { puts("FAIL empty accepted"); goto fail; }
    if (!rejects((const uint8_t *)"CO", 2))    { puts("FAIL 2-byte accepted"); goto fail; }
    if (!rejects((const uint8_t *)"COIL", 4))  { puts("FAIL 4-byte accepted"); goto fail; }

    free(bad); free(good);
    puts("  reject-headers: ok");
    return 1;
fail:
    free(bad); free(good);
    return 0;
}

int main(void)
{
    int ok = 1;
    ok &= test_canonical();
    ok &= test_exec_equivalence();
    ok &= test_reject_headers();
    if (!ok) return 1;
    puts("PASS test_serialize");
    return 0;
}
