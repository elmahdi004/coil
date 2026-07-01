/*
 * test_errors.c -- every error path returns cleanly (no crash, no leak).
 *
 * Runtime errors must return INTERPRET_RUNTIME_ERROR and leave the VM in a
 * state that vm_free can tear down; compile errors must return
 * INTERPRET_COMPILE_ERROR without ever entering the interpreter loop.
 */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

static int expect(const char *src, InterpretResult want)
{
    VM vm;
    vm_init(&vm);
    InterpretResult got = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return got == want;
}

#define RUNTIME(s)  do { if (!expect((s), INTERPRET_RUNTIME_ERROR)) { printf("FAIL runtime: %s\n", (s)); return 1; } } while (0)
#define COMPILE(s)  do { if (!expect((s), INTERPRET_COMPILE_ERROR)) { printf("FAIL compile: %s\n", (s)); return 1; } } while (0)
#define OK(s)       do { if (!expect((s), INTERPRET_OK))            { printf("FAIL ok: %s\n", (s));      return 1; } } while (0)

int main(void)
{
    /* arithmetic domain errors */
    RUNTIME("print 1 / 0;");
    RUNTIME("print 1 % 0;");
    RUNTIME("print -true;");
    RUNTIME("print 1 + \"x\";");
    RUNTIME("print \"a\" - \"b\";");
    RUNTIME("print 1 & 2.5;");
    RUNTIME("print ~1.5;");

    /* name resolution */
    RUNTIME("print undefined_variable;");
    RUNTIME("undefined_variable = 3;");
    RUNTIME("print missing();");

    /* type / call errors */
    RUNTIME("let x = 5; x();");
    RUNTIME("print (3)(4);");
    RUNTIME("fn f(a) { return a; } f(1, 2);");     /* arity mismatch */
    RUNTIME("fn f(a, b) { return a; } f(1);");

    /* indexing */
    RUNTIME("let a = [1, 2]; print a[5];");
    RUNTIME("let a = [1, 2]; print a[-1];");
    RUNTIME("let a = [1]; a[3] = 9;");
    RUNTIME("print (5)[0];");                        /* indexing a non-container */
    RUNTIME("let t = {}; t[[1]] = 2;");             /* unhashable key (array)   */

    /* assert is the workhorse the other suites depend on */
    RUNTIME("assert(false);");
    RUNTIME("assert(nil);");
    OK("assert(true); assert(1); assert(\"x\");");

    /* native argument errors surface as runtime errors, not crashes */
    RUNTIME("print sqrt(\"x\");");
    RUNTIME("print ord(\"\");");
    RUNTIME("print push(5, 1);");

    /* syntax errors never reach the VM */
    COMPILE("let = 5;");
    COMPILE("print ;");
    COMPILE("1 +;");
    COMPILE("fn (");
    COMPILE("let a = [1, 2;");
    COMPILE("if (x { }");
    COMPILE("}{");

    /* well-formed programs still succeed after all that */
    OK("print 1 + 1;");
    OK("let a = [1, 2, 3]; assert(a[0] == 1);");

    puts("PASS test_errors");
    return 0;
}
