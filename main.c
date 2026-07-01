/*
 * main.c -- the Coil command-line driver.
 *
 * A small front-end over the whole toolchain: the compiler, the bytecode
 * (de)serializer, the verifier, the peephole optimizer and the disassembler.
 *
 *   coil <script.coil>          compile and run a source file
 *   coil run <script.coil>      (explicit form of the above)
 *   coil repl                   start an interactive REPL
 *   coil disasm <script.coil>   compile and print the bytecode listing
 *   coil opt <script.coil>      compile, optimize, and print the listing
 *   coil build <src> <out.cbc>  compile a source file to a bytecode module
 *   coil exec <module.cbc>      load, verify and run a bytecode module
 *   coil check <module.cbc>     load and verify a module, reporting the result
 */

#include "src/vm.h"
#include "src/compiler.h"
#include "src/module.h"
#include "src/verify.h"
#include "src/optimize.h"
#include "src/asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "coil: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }

    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); fprintf(stderr, "coil: out of memory\n"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out_len = got;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "coil: cannot write '%s'\n", path); return -1; }
    size_t put = fwrite(data, 1, len, f);
    fclose(f);
    if (put != len) { fprintf(stderr, "coil: short write to '%s'\n", path); return -1; }
    return 0;
}

/* ------------------------------------------------------------------ */
static int run_file(VM *vm, const char *path)
{
    size_t len;
    char  *src = read_file(path, &len);
    if (!src) return 74;
    InterpretResult r = vm_interpret(vm, src, len);
    free(src);
    switch (r) {
    case INTERPRET_OK:            return 0;
    case INTERPRET_COMPILE_ERROR: return 65;
    case INTERPRET_RUNTIME_ERROR: return 70;
    default:                      return 1;
    }
}

static void repl(VM *vm)
{
    char line[4096];
    printf("Coil REPL -- Ctrl-D to exit\n");
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }
        vm_interpret(vm, line, strlen(line));
    }
}

/* compile a source file to an ObjFunction in vm's heap, or NULL on error. */
static ObjFunction *compile_file(VM *vm, const char *path)
{
    size_t len;
    char  *src = read_file(path, &len);
    if (!src) return NULL;
    ObjFunction *fn = compile(&vm->heap, src, len);
    free(src);
    if (!fn) fprintf(stderr, "coil: compile error in '%s'\n", path);
    return fn;
}

static int cmd_disasm(VM *vm, const char *path, int optimize)
{
    ObjFunction *fn = compile_file(vm, path);
    if (!fn) return 65;
    if (optimize) {
        ObjFunction *opt = optimize_function(&vm->heap, fn);
        if (!opt) { fprintf(stderr, "coil: optimization failed\n"); return 1; }
        fn = opt;
    }
    ObjString *text = coil_disasm_text(&vm->heap, fn);
    fputs(text->chars, stdout);
    return 0;
}

static int cmd_build(VM *vm, const char *src_path, const char *out_path)
{
    ObjFunction *fn = compile_file(vm, src_path);
    if (!fn) return 65;
    uint8_t *bytes = NULL;
    size_t   len   = 0;
    if (module_serialize(fn, &bytes, &len) != MODULE_OK) {
        fprintf(stderr, "coil: serialization failed\n");
        return 1;
    }
    int rc = write_file(out_path, bytes, len);
    free(bytes);
    if (rc == 0) fprintf(stderr, "coil: wrote %zu bytes to '%s'\n", len, out_path);
    return rc == 0 ? 0 : 1;
}

/* load a bytecode module file into vm's heap. */
static ObjFunction *load_module_file(VM *vm, const char *path)
{
    size_t len;
    char  *raw = read_file(path, &len);
    if (!raw) return NULL;
    ObjFunction *entry = NULL;
    int rc = module_load(&vm->heap, (const uint8_t *)raw, len, &entry);
    free(raw);
    if (rc != MODULE_OK) { fprintf(stderr, "coil: malformed module '%s'\n", path); return NULL; }
    return entry;
}

static int cmd_exec(VM *vm, const char *path)
{
    ObjFunction *entry = load_module_file(vm, path);
    if (!entry) return 74;
    if (verify_module(entry) != VERIFY_OK) {
        fprintf(stderr, "coil: module failed verification\n");
        return 65;
    }
    return vm_run_function(vm, entry) == INTERPRET_OK ? 0 : 70;
}

static int cmd_check(VM *vm, const char *path)
{
    ObjFunction *entry = load_module_file(vm, path);
    if (!entry) return 74;
    int ok = verify_module(entry) == VERIFY_OK;
    printf("%s: %s\n", path, ok ? "OK" : "REJECTED");
    return ok ? 0 : 1;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  coil <script.coil>          run a source file\n"
        "  coil run <script.coil>      run a source file\n"
        "  coil repl                   start a REPL\n"
        "  coil disasm <script.coil>   print the bytecode listing\n"
        "  coil opt <script.coil>      print the optimized listing\n"
        "  coil build <src> <out.cbc>  compile to a bytecode module\n"
        "  coil exec <module.cbc>      verify and run a bytecode module\n"
        "  coil check <module.cbc>     verify a bytecode module\n");
}

int main(int argc, char **argv)
{
    VM vm;
    vm_init(&vm);
    int code = 0;

    if (argc == 1) {
        repl(&vm);
    } else if (strcmp(argv[1], "repl") == 0) {
        repl(&vm);
    } else if (strcmp(argv[1], "run") == 0 && argc == 3) {
        code = run_file(&vm, argv[2]);
    } else if (strcmp(argv[1], "disasm") == 0 && argc == 3) {
        code = cmd_disasm(&vm, argv[2], 0);
    } else if (strcmp(argv[1], "opt") == 0 && argc == 3) {
        code = cmd_disasm(&vm, argv[2], 1);
    } else if (strcmp(argv[1], "build") == 0 && argc == 4) {
        code = cmd_build(&vm, argv[2], argv[3]);
    } else if (strcmp(argv[1], "exec") == 0 && argc == 3) {
        code = cmd_exec(&vm, argv[2]);
    } else if (strcmp(argv[1], "check") == 0 && argc == 3) {
        code = cmd_check(&vm, argv[2]);
    } else if (argc == 2 && argv[1][0] != '-') {
        code = run_file(&vm, argv[1]);       /* bare script path */
    } else {
        usage();
        code = 64;
    }

    vm_free(&vm);
    return code;
}
