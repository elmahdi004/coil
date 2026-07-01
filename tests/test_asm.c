/*
 * test_asm.c -- the textual assembler and its disassembler inverse.
 */
#include "../src/vm.h"
#include "../src/compiler.h"
#include "../src/asm.h"
#include "../src/verify.h"
#include "../src/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* assemble text in a fresh VM, verify, and run it. */
static InterpretResult assemble_run(const char *text)
{
    VM vm;
    vm_init(&vm);
    char err[128] = {0};
    ObjFunction *fn = coil_assemble_fn(&vm.heap, text, strlen(text), err, sizeof err);
    InterpretResult r = INTERPRET_COMPILE_ERROR;
    if (fn) {
        if (verify_module(fn) != VERIFY_OK) r = INTERPRET_COMPILE_ERROR;
        else r = vm_run_function(&vm, fn);
    } else {
        fprintf(stderr, "assemble error: %s\n", err);
    }
    vm_free(&vm);
    return r;
}

static int assembles(const char *text)
{
    VM vm;
    vm_init(&vm);
    char err[128] = {0};
    ObjFunction *fn = coil_assemble_fn(&vm.heap, text, strlen(text), err, sizeof err);
    int ok = fn && verify_module(fn) == VERIFY_OK;
    vm_free(&vm);
    return ok;
}

/* text should FAIL to assemble (syntax / label / range error). */
static int rejects(const char *text)
{
    VM vm;
    vm_init(&vm);
    char err[128] = {0};
    ObjFunction *fn = coil_assemble_fn(&vm.heap, text, strlen(text), err, sizeof err);
    vm_free(&vm);
    return fn == NULL;
}

static int test_basic(void)
{
    /* arithmetic, a branch and a backward loop assemble + verify + run */
    const char *prog =
        ".arity 0\n.slots 4\n"
        "  CONST 0\n"          /* sum = 0 on stack (slot 1) */
        "  CONST 5\n"          /* i = 5   (slot 2)          */
        "loop:\n"
        "  GET_LOCAL 2\n"      /* i */
        "  CONST 0\n"
        "  GT\n"               /* i > 0 ? */
        "  JMP_IF_FALSE done\n"
        "  POP\n"             /* drop the comparison result */
        "  GET_LOCAL 1\n"
        "  GET_LOCAL 2\n"
        "  ADD\n"
        "  SET_LOCAL 1\n"      /* sum += i */
        "  POP\n"
        "  GET_LOCAL 2\n"
        "  CONST 1\n"
        "  SUB\n"
        "  SET_LOCAL 2\n"      /* i -= 1 */
        "  POP\n"
        "  LOOP loop\n"        /* backward branch uses LOOP, not JMP */
        "done:\n"
        "  POP\n"             /* drop comparison result */
        "  GET_LOCAL 1\n"
        "  PRINT\n"           /* 15 */
        "  NIL\n"
        "  RETURN\n";
    if (assemble_run(prog) != INTERPRET_OK) { puts("FAIL asm.basic"); return 0; }

    /* CALL through a global native */
    if (assemble_run("  GET_GLOBAL assert\n  TRUE\n  CALL 1\n  POP\n  NIL\n  RETURN\n") != INTERPRET_OK)
        { puts("FAIL asm.call-ok"); return 0; }
    if (assemble_run("  GET_GLOBAL assert\n  FALSE\n  CALL 1\n  POP\n  NIL\n  RETURN\n") != INTERPRET_RUNTIME_ERROR)
        { puts("FAIL asm.call-fail"); return 0; }

    /* string and float constants, bitwise ops */
    if (!assembles("  CONST \"hi\"\n  CONST 3.5\n  POP\n  POP\n  CONST 12\n  CONST 10\n  BAND\n  PRINT\n  NIL\n  RETURN\n"))
        { puts("FAIL asm.consts"); return 0; }

    puts("  basic: ok");
    return 1;
}

static int test_roundtrip(void)
{
    /* Compile closure-free programs, disassemble to text, re-assemble, and run.
     * Each program self-checks with assert, so INTERPRET_OK proves the text
     * round-trip preserved behaviour exactly. */
    const char *progs[] = {
        "let x = 2 + 3 * 4; assert(x == 14);",
        "let s = 0; for (let i = 0; i < 5; i = i + 1) { s = s + i; } assert(s == 10);",
        "let a = [1, 2, 3]; assert(a[0] + a[1] + a[2] == 6);",
        "let n = 10; let f = 1; while (n > 1) { f = f * n; n = n - 1; } assert(f == 3628800);",
        "assert(((5 & 3) | (1 << 4)) == 17);",
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
        VM vm;
        vm_init(&vm);
        ObjFunction *fn = compile(&vm.heap, progs[i], strlen(progs[i]));
        if (!fn) { vm_free(&vm); printf("FAIL roundtrip compile #%zu\n", i); return 0; }
        ObjString *text = coil_disasm_text(&vm.heap, fn);
        /* re-assemble from the disassembly and run it */
        char err[128] = {0};
        ObjFunction *fn2 = coil_assemble_fn(&vm.heap, text->chars, text->length, err, sizeof err);
        int ok = fn2 && verify_module(fn2) == VERIFY_OK &&
                 vm_run_function(&vm, fn2) == INTERPRET_OK;
        if (!ok) fprintf(stderr, "roundtrip #%zu err=%s\ntext:\n%s\n", i, err, text->chars);
        vm_free(&vm);
        if (!ok) { printf("FAIL roundtrip #%zu\n", i); return 0; }
    }
    puts("  roundtrip: ok");
    return 1;
}

static int test_errors(void)
{
    if (!rejects("  BOGUS_OP\n"))                 { puts("FAIL err.mnemonic"); return 0; }
    if (!rejects("  JMP nowhere\n  RETURN\n"))    { puts("FAIL err.undef-label"); return 0; }
    if (!rejects("dup:\n  NIL\ndup:\n  RETURN\n")){ puts("FAIL err.dup-label"); return 0; }
    if (!rejects("  GET_LOCAL 999\n  RETURN\n"))  { puts("FAIL err.byte-range"); return 0; }
    if (!rejects("  CONST notanumber\n  RETURN\n")){ puts("FAIL err.bad-literal"); return 0; }
    if (!rejects("here:\n  RETURN\n  JMP here\n")){ puts("FAIL err.jmp-backward"); return 0; }
    if (!rejects("  CONST\n"))                    { puts("FAIL err.missing-operand"); return 0; }
    puts("  errors: ok");
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_basic();
    ok &= test_roundtrip();
    ok &= test_errors();
    if (!ok) return 1;
    puts("PASS test_asm");
    return 0;
}
