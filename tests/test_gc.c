/*
 * test_gc.c -- the mark-sweep collector under allocation pressure.
 *
 * These programs allocate far more than they keep, so the collector runs many
 * times mid-execution.  Correct results (checked with assert) plus a clean
 * ASan/leak report mean live objects survived and dead ones were reclaimed.
 * The suite is also run with -DCOIL_GC_STRESS (collect on every allocation),
 * which turns any missed root into an immediate use-after-free.
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
    /* keep one big array alive while churning many temporary ones */
    if (!run(
        "fn build(n) { let a = []; for (let i = 0; i < n; i = i + 1) { push(a, \"item\" + str(i)); } return a; }"
        "let keep = build(500);"
        "for (let r = 0; r < 40; r = r + 1) { build(500); }"        /* garbage */
        "assert(len(keep) == 500);"
        "assert(keep[0] == \"item0\");"
        "assert(keep[250] == \"item250\");"
        "assert(keep[499] == \"item499\");")) { puts("FAIL gc.arrays"); return 1; }

    /* tables of squares, most discarded */
    if (!run(
        "fn tbl(n) { let t = {}; for (let i = 0; i < n; i = i + 1) { t[i] = i * i; } return t; }"
        "let kt = tbl(300);"
        "for (let r = 0; r < 40; r = r + 1) { tbl(300); }"
        "assert(len(kt) == 300);"
        "assert(kt[17] == 289);"
        "assert(kt[299] == 299 * 299);")) { puts("FAIL gc.tables"); return 1; }

    /* closures keep their captured upvalues alive across collections */
    if (!run(
        "fn make(base) { fn get() { return base; } return get; }"
        "let fns = [];"
        "for (let i = 0; i < 200; i = i + 1) { push(fns, make(i * 10)); }"
        "for (let r = 0; r < 20; r = r + 1) { make(r); }"          /* churn */
        "let total = 0;"
        "for (let i = 0; i < 200; i = i + 1) { total = total + fns[i](); }"
        "assert(total == 199000);"                                  /* 10*(0+..+199) */
        "assert(fns[42]() == 420);")) { puts("FAIL gc.closures"); return 1; }

    /* deeply nested structure survives collection intact */
    if (!run(
        "let root = [];"
        "let cur = root;"
        "for (let i = 0; i < 100; i = i + 1) { let child = [i]; push(cur, child); cur = child; }"
        "for (let r = 0; r < 30; r = r + 1) { let junk = []; for (let k = 0; k < 100; k = k + 1) { push(junk, k); } }"
        "let node = root[0];"   /* root is just a holder; the chain head is root[0] */
        "for (let i = 0; i < 100; i = i + 1) { assert(node[0] == i); if (len(node) > 1) { node = node[1]; } }")) { puts("FAIL gc.nested"); return 1; }

    /* string interning + concatenation storm */
    if (!run(
        "let acc = \"\";"
        "for (let i = 0; i < 300; i = i + 1) { acc = acc + \"x\"; }"
        "assert(len(acc) == 300);"
        "let words = [];"
        "for (let i = 0; i < 200; i = i + 1) { push(words, upper(\"tag\" + str(i))); }"
        "assert(words[100] == \"TAG100\");")) { puts("FAIL gc.strings"); return 1; }

    /* tabulate builds an array of freshly allocated sub-objects; run it under
     * the collector to check the aggregate survives collection intact. */
    if (!run(
        "let t = tabulate([\"x\", \"y\", \"z\"]);"
        "assert(len(t) == 3);"
        "assert(t[0][0] == 0); assert(t[0][1] == \"x\");"
        "assert(t[2][1] == \"z\");")) { puts("FAIL gc.tabulate"); return 1; }

    /* map/filter run a callback that allocates; the result they accumulate must
     * survive a collection triggered inside those callbacks. Under
     * -DCOIL_GC_STRESS the allocation in `tag` collects on every call. */
    if (!run(
        "fn tag(x) { return str(x) + \"=\" + str(x * x); }"
        "let m = map([1, 2, 3, 4, 5], tag);"
        "assert(len(m) == 5);"
        "assert(m[0] == \"1=1\"); assert(m[4] == \"5=25\");"
        "fn big(x) { return len(str(x)) > 0; }"
        "let f = filter([10, 20, 30], big);"
        "assert(len(f) == 3); assert(f[2] == 30);")) { puts("FAIL gc.hof"); return 1; }

    /* a view's parent array must survive collection as long as the view is
     * reachable, even when nothing else references the parent.  Here the parent
     * (range(12)) is reachable only through the view, so a collection between
     * creating the view and reading it must not reclaim it. */
    if (!run(
        "let w = view(range(12), 2, 5);"   /* parent reachable only via w */
        "let s = str(99);"                 /* allocations that may collect */
        "let t = [1, 2, 3];"
        "assert(len(w) == 5);"
        "assert(w[0] == 2); assert(w[4] == 6);")) { puts("FAIL gc.view"); return 1; }

    puts("PASS test_gc");
    return 0;
}
