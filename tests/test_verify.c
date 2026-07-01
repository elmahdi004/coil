/*
 * test_verify.c -- the bytecode verifier is the security boundary.
 *
 * The VM executes verified bytecode without per-instruction bounds checks, so
 * this suite pins down the verifier's contract from both sides:
 *   - well-formed modules (compiled from source, or hand-assembled) VERIFY_OK;
 *   - malformed modules are rejected (bad indices, jumps, stack effects, ...);
 *   - arbitrary single-byte and truncation mutations of a valid module never
 *     crash the loader or verifier (ASan/UBSan enforce this).
 *
 * Actually *running* adversarial-but-verified bytecode is covered by
 * fuzz/bytecode_fuzzer.c, which builds with the COIL_FUZZ step cap; here we stop
 * at load+verify so a mutated infinite loop can't hang the unit test.
 */
#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/module.h"
#include "../src/verify.h"
#include "../src/chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ---- helpers ----------------------------------------------------------- */

/* compile source and serialize it; NULL on compile error. Caller frees. */
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

/* assemble a single-function module from raw code + (int) constants. */
static uint8_t *raw_module(const uint8_t *code, size_t code_len,
                           const int64_t *ints, size_t nints,
                           int arity, int upvals, uint16_t num_slots,
                           size_t *out_len)
{
    VM vm;
    vm_init(&vm);
    vm.heap.gc_paused = true;                 /* half-built fn is unrooted */
    ObjFunction *fn = function_new(&vm.heap);
    fn->arity         = arity;
    fn->upvalue_count = upvals;
    fn->num_slots     = num_slots;
    for (size_t i = 0; i < code_len; i++)
        chunk_write(&fn->chunk, code[i], 1);
    for (size_t i = 0; i < nints; i++)
        chunk_add_const(&fn->chunk, INT_VAL(ints[i]));
    uint8_t *bytes = NULL;
    size_t   len   = 0;
    module_serialize(fn, &bytes, &len);
    vm.heap.gc_paused = false;
    vm_free(&vm);
    *out_len = len;
    return bytes;
}

/* 1 iff the bytes both load and verify (the VM would be willing to run them). */
static int accepts(const uint8_t *bytes, size_t len)
{
    if (!bytes) return 0;
    VM vm;
    vm_init(&vm);
    ObjFunction *entry = NULL;
    int ok = 0;
    if (module_load(&vm.heap, bytes, len, &entry) == MODULE_OK && entry)
        ok = (verify_module(entry) == VERIFY_OK);
    vm_free(&vm);
    return ok;
}

/* compile, serialize, load, verify, and run a terminating program. */
static int roundtrip_runs(const char *src)
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

/* ---- tests ------------------------------------------------------------- */

static int test_positive(void)
{
    const char *progs[] = {
        "print 1 + 2 * 3;",
        "fn fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } assert(fib(12) == 144);",
        "fn make() { let c = 0; fn inc() { c = c + 1; return c; } return inc; }"
        "let a = make(); assert(a() == 1); assert(a() == 2);",
        "let a = [1, 2, 3]; let s = 0;"
        "for (let i = 0; i < 3; i = i + 1) { s = s + a[i]; } assert(s == 6);",
        "let t = {\"k\": 1}; t[\"k\"] = t[\"k\"] + 41; assert(t[\"k\"] == 42);",
        "assert(((5 & 3) | (1 << 4)) == 17);",
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
        size_t len = 0;
        uint8_t *b = compile_module(progs[i], &len);
        if (!accepts(b, len)) { free(b); printf("FAIL positive #%zu\n", i); return 0; }
        free(b);
        if (!roundtrip_runs(progs[i])) { printf("FAIL positive-run #%zu\n", i); return 0; }
    }

    /* minimal hand-assembled accepters */
    { uint8_t c[] = { OP_NIL, OP_RETURN };              size_t n; uint8_t *b = raw_module(c, sizeof c, NULL, 0, 0, 0, 4, &n); int ok = accepts(b, n); free(b); if (!ok) { puts("FAIL positive nil-return"); return 0; } }
    { uint8_t c[] = { OP_CONST, 0, OP_RETURN };         int64_t k[] = { 42 }; size_t n; uint8_t *b = raw_module(c, sizeof c, k, 1, 0, 0, 4, &n); int ok = accepts(b, n); free(b); if (!ok) { puts("FAIL positive const-return"); return 0; } }
    /* the canonical if/else shape: both arms POP the condition, so control
     * reconverges at OP_RETURN with a consistent stack height. */
    { uint8_t c[] = { OP_NIL, OP_JMP_IF_FALSE, 0, 4, OP_POP, OP_JMP, 0, 1, OP_POP, OP_RETURN };
      size_t n; uint8_t *b = raw_module(c, sizeof c, NULL, 0, 0, 0, 4, &n); int ok = accepts(b, n); free(b); if (!ok) { puts("FAIL positive branch"); return 0; } }

    puts("  positive: ok");
    return 1;
}

static int test_reject(void)
{
    struct { const char *why; uint8_t code[16]; size_t n; int64_t ints[4]; size_t nints; int arity; uint16_t slots; } bad[] = {
        { "empty code",             { 0 }, 0, {0}, 0, 0, 4 },
        { "unknown opcode",         { 0xFE, OP_RETURN }, 2, {0}, 0, 0, 4 },
        { "const index OOB",        { OP_CONST, 5, OP_RETURN }, 3, {7}, 1, 0, 4 },
        { "stack underflow (add)",  { OP_ADD, OP_RETURN }, 2, {0}, 0, 0, 4 },
        { "underflow (return)",     { OP_POP, OP_RETURN }, 2, {0}, 0, 0, 4 },
        { "local slot too high",    { OP_GET_LOCAL, 200, OP_RETURN }, 3, {0}, 0, 0, 4 },
        { "upvalue index OOB",      { OP_GET_UPVALUE, 0, OP_RETURN }, 3, {0}, 0, 0, 4 },
        { "fall off the end",       { OP_NIL }, 1, {0}, 0, 0, 4 },
        { "truncated const",        { OP_CONST }, 1, {1}, 1, 0, 4 },
        { "truncated jump",         { OP_JMP, 0 }, 2, {0}, 0, 0, 4 },
        { "jump past end",          { OP_JMP, 0xFF, 0xF0, OP_RETURN }, 4, {0}, 0, 0, 4 },
        { "jump into operand",      { OP_JMP, 0, 1, OP_CONST, 0, OP_RETURN }, 6, {9}, 1, 0, 4 },
        { "loop before start",      { OP_LOOP, 0, 10, OP_RETURN }, 4, {0}, 0, 0, 4 },
        { "def-global bad const",   { OP_DEF_GLOBAL, 0, OP_RETURN }, 3, {5}, 1, 0, 4 },  /* const 0 is int, not a name */
        { "index-get underflow",    { OP_INDEX_GET, OP_RETURN }, 2, {0}, 0, 0, 4 },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        size_t n = 0;
        uint8_t *b = raw_module(bad[i].code, bad[i].n, bad[i].ints, bad[i].nints,
                                bad[i].arity, 0, bad[i].slots, &n);
        int a = accepts(b, n);
        free(b);
        if (a) { printf("FAIL reject: verifier ACCEPTED '%s'\n", bad[i].why); return 0; }
    }
    puts("  reject: ok");
    return 1;
}

/* deterministic 32-bit LCG so the sweep is reproducible without <time.h>. */
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }

static int test_mutations(void)
{
    const char *src =
        "fn fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); }"
        "let a = [1, 2, 3]; let s = 0;"
        "for (let i = 0; i < 3; i = i + 1) { s = s + a[i]; }"
        "let t = {\"x\": s}; print t[\"x\"];";
    size_t len = 0;
    uint8_t *base = compile_module(src, &len);
    if (!accepts(base, len)) { free(base); puts("FAIL mutations base"); return 0; }

    uint8_t *buf = (uint8_t *)malloc(len);
    long verified = 0, total = 0;

    /* every single-byte position xor'd with a spread of masks */
    const uint8_t masks[] = { 0x01, 0x02, 0x08, 0x20, 0x7f, 0x80, 0xff };
    for (size_t i = 0; i < len; i++)
        for (size_t m = 0; m < sizeof masks; m++) {
            memcpy(buf, base, len);
            buf[i] ^= masks[m];
            verified += accepts(buf, len);          /* ASan guards the walk */
            total++;
        }

    /* every truncation */
    for (size_t cut = 0; cut < len; cut++) {
        verified += accepts(base, cut);
        total++;
    }

    /* a few thousand multi-byte scrambles */
    uint32_t seed = 0xC01Du;
    for (int iter = 0; iter < 4000; iter++) {
        memcpy(buf, base, len);
        int flips = 1 + (int)(lcg(&seed) % 6);
        for (int f = 0; f < flips; f++)
            buf[lcg(&seed) % len] = (uint8_t)(lcg(&seed) & 0xff);
        verified += accepts(buf, len);
        total++;
    }

    free(buf);
    free(base);
    printf("  mutations: %ld/%ld accepted, no crash\n", verified, total);
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_positive();
    ok &= test_reject();
    ok &= test_mutations();
    if (!ok) return 1;
    puts("PASS test_verify");
    return 0;
}
