/*
 * test_stdlib2.c -- remaining standard-library coverage: type introspection,
 * ranges/slices, sorting, rounding and conversions.
 */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

static int run(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_OK;
}

int main(void)
{
    /* type() names every runtime kind */
    if (!run(
        "assert(type(5) == \"int\");"
        "assert(type(5.0) == \"float\");"
        "assert(type(\"x\") == \"string\");"
        "assert(type(true) == \"bool\");"
        "assert(type(nil) == \"nil\");"
        "assert(type([1]) == \"array\");"
        "assert(type({}) == \"table\");"
        "assert(type(len) == \"function\");")) { puts("FAIL std2.type"); return 1; }

    /* range and slice */
    if (!run(
        "let r = range(5);"
        "assert(len(r) == 5); assert(r[0] == 0); assert(r[4] == 4);"
        "assert(len(range(0)) == 0);"
        "let sl = slice([10, 20, 30, 40, 50], 1, 4);"
        "assert(len(sl) == 3); assert(sl[0] == 20); assert(sl[2] == 40);"
        "assert(len(slice([1, 2, 3], 2, 100)) == 1);"     /* clamped */
        "assert(len(slice([1, 2, 3], 5, 6)) == 0);")) { puts("FAIL std2.range"); return 1; }

    /* reverse and sort (numbers and strings) */
    if (!run(
        "let a = [1, 2, 3, 4]; reverse(a);"
        "assert(a[0] == 4); assert(a[3] == 1);"
        "let n = [3, 1, 4, 1, 5, 9, 2, 6]; sort(n);"
        "assert(n[0] == 1); assert(n[7] == 9);"
        "let s = [\"banana\", \"apple\", \"cherry\"]; sort(s);"
        "assert(s[0] == \"apple\"); assert(s[2] == \"cherry\");")) { puts("FAIL std2.sort"); return 1; }

    /* rounding family, including ties and negatives */
    if (!run(
        "assert(floor(3.7) == 3); assert(floor(-3.2) == -4);"
        "assert(ceil(3.2) == 4); assert(ceil(-3.7) == -3);"
        "assert(round(2.5) == 3); assert(round(2.4) == 2); assert(round(-2.5) == -3);"
        "assert(abs(-5) == 5); assert(abs(-5.5) == 5.5); assert(abs(5) == 5);"
        "assert(sqrt(16.0) == 4.0); assert(pow(2, 10) == 1024.0);"
        "assert(min(3, 5) == 3); assert(max(3, 5) == 5);"
        "assert(min(2.5, 2) == 2);")) { puts("FAIL std2.round"); return 1; }

    /* conversions between the scalar types */
    if (!run(
        "assert(str(3.5) == \"3.5\"); assert(str(-42) == \"-42\");"
        "assert(int(true) == 1); assert(int(false) == 0);"
        "assert(int(\"  99\") == 99); assert(int(3.99) == 3);"
        "assert(num(5) == 5.0); assert(num(\"2.5\") == 2.5);"
        "assert(ord(chr(65)) == 65);")) { puts("FAIL std2.convert"); return 1; }

    /* keys/values round-trip and stay consistent with has/len */
    if (!run(
        "let t = {};"
        "for (let i = 0; i < 20; i = i + 1) { t[i] = i * i; }"
        "let ks = keys(t); let vs = values(t);"
        "assert(len(ks) == 20); assert(len(vs) == 20);"
        "let check = 0;"
        "for (let i = 0; i < len(ks); i = i + 1) { if (t[ks[i]] == ks[i] * ks[i]) { check = check + 1; } }"
        "assert(check == 20);")) { puts("FAIL std2.keysvalues"); return 1; }

    puts("PASS test_stdlib2");
    return 0;
}
