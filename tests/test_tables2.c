/*
 * test_tables2.c -- value-keyed tables (ObjTable): mixed key types, collisions,
 * growth, deletion and the keys()/values() reflection natives.
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
    /* every hashable value type can be a key, and they don't collide */
    if (!run(
        "let t = {};"
        "t[1] = \"int\"; t[1.5] = \"float\"; t[\"k\"] = \"str\";"
        "t[true] = \"bool\"; t[nil] = \"nil\";"
        "assert(t[1] == \"int\"); assert(t[1.5] == \"float\");"
        "assert(t[\"k\"] == \"str\"); assert(t[true] == \"bool\"); assert(t[nil] == \"nil\");"
        "assert(len(t) == 5);"
        "assert(t[2] == nil);")) { puts("FAIL tab2.keytypes"); return 1; }

    /* growth: insert many keys and read them all back */
    if (!run(
        "let t = {};"
        "for (let i = 0; i < 400; i = i + 1) { t[i] = i * 3; }"
        "assert(len(t) == 400);"
        "for (let i = 0; i < 400; i = i + 1) { assert(t[i] == i * 3); }"
        "assert(t[123] == 369);")) { puts("FAIL tab2.growth"); return 1; }

    /* overwrite keeps the table small; delete shrinks live count */
    if (!run(
        "let t = {};"
        "for (let i = 0; i < 50; i = i + 1) { t[\"k\"] = i; }"
        "assert(len(t) == 1); assert(t[\"k\"] == 49);"
        "let m = {};"
        "for (let i = 0; i < 100; i = i + 1) { m[i] = i; }"
        "for (let i = 0; i < 50; i = i + 1) { del(m, i); }"
        "assert(len(m) == 50); assert(!has(m, 0)); assert(has(m, 50));"
        "assert(m[75] == 75);")) { puts("FAIL tab2.delete"); return 1; }

    /* re-inserting after deletion reuses tombstones without losing entries */
    if (!run(
        "let t = {};"
        "for (let i = 0; i < 30; i = i + 1) { t[i] = i; }"
        "for (let i = 0; i < 30; i = i + 1) { del(t, i); }"
        "assert(len(t) == 0);"
        "for (let i = 0; i < 30; i = i + 1) { t[i] = i * 2; }"
        "assert(len(t) == 30); assert(t[15] == 30);")) { puts("FAIL tab2.tombstone"); return 1; }

    /* keys()/values() enumerate exactly the live entries */
    if (!run(
        "let t = {\"a\": 1, \"b\": 2, \"c\": 3};"
        "let ks = keys(t); let vs = values(t);"
        "assert(len(ks) == 3); assert(len(vs) == 3);"
        "let total = 0;"
        "for (let i = 0; i < len(vs); i = i + 1) { total = total + vs[i]; }"
        "assert(total == 6);"
        "assert(has(t, \"a\")); assert(has(t, \"b\")); assert(has(t, \"c\"));")) { puts("FAIL tab2.reflect"); return 1; }

    /* negative, zero and large integer keys all address distinct slots */
    if (!run(
        "let t = {};"
        "t[0] = \"zero\"; t[-1] = \"neg\"; t[1000000] = \"big\"; t[-999999] = \"bigneg\";"
        "assert(t[0] == \"zero\"); assert(t[-1] == \"neg\");"
        "assert(t[1000000] == \"big\"); assert(t[-999999] == \"bigneg\");"
        "assert(len(t) == 4);"
        "t[0] = \"z2\"; assert(t[0] == \"z2\"); assert(len(t) == 4);")) { puts("FAIL tab2.intkeys"); return 1; }

    puts("PASS test_tables2");
    return 0;
}
