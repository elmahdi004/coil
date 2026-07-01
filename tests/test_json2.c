/*
 * test_json2.c -- JSON parsing and stringifying through the native bridge.
 *
 * Round-trips go value -> json_str -> json_parse so they don't depend on the
 * exact spacing json_str emits; parse errors must surface as runtime errors,
 * never crashes.
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

static int errs(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_RUNTIME_ERROR;
}

int main(void)
{
    /* scalars */
    if (!run(
        "assert(json_parse(\"42\") == 42);"
        "assert(json_parse(\"-7\") == -7);"
        "assert(json_parse(\"3.5\") == 3.5);"
        "assert(json_parse(\"true\") == true);"
        "assert(json_parse(\"false\") == false);"
        "assert(json_parse(\"null\") == nil);"
        "assert(json_parse(\"\\\"hi\\\"\") == \"hi\");")) { puts("FAIL json2.scalars"); return 1; }

    /* arrays and objects, including nesting */
    if (!run(
        "let a = json_parse(\"[1, 2, 3]\");"
        "assert(len(a) == 3); assert(a[0] == 1); assert(a[2] == 3);"
        "let o = json_parse(\"{\\\"a\\\": 1, \\\"b\\\": [2, 3]}\");"
        "assert(o[\"a\"] == 1); assert(o[\"b\"][1] == 3);"
        "let deep = json_parse(\"{\\\"x\\\": {\\\"y\\\": {\\\"z\\\": 99}}}\");"
        "assert(deep[\"x\"][\"y\"][\"z\"] == 99);"
        "let empty = json_parse(\"[]\"); assert(len(empty) == 0);")) { puts("FAIL json2.compound"); return 1; }

    /* value -> string -> value round-trips (format-agnostic) */
    if (!run(
        "assert(json_parse(json_str(42)) == 42);"
        "assert(json_parse(json_str(\"hello\")) == \"hello\");"
        "assert(json_parse(json_str([1, 2, 3]))[2] == 3);"
        "let t = {\"k\": 5, \"list\": [7, 8]};"
        "let back = json_parse(json_str(t));"
        "assert(back[\"k\"] == 5); assert(back[\"list\"][0] == 7);"
        "let nested = [[1, 2], [3, [4, 5]]];"
        "let r = json_parse(json_str(nested));"
        "assert(r[1][1][0] == 4);")) { puts("FAIL json2.roundtrip"); return 1; }

    /* whitespace and escapes */
    if (!run(
        "assert(json_parse(\"  [ 1 , 2 ]  \")[1] == 2);"
        "assert(json_parse(\"\\\"a\\\\nb\\\"\") == \"a\\nb\");"
        "assert(len(json_parse(\"\\\"\\\\t\\\\t\\\"\")) == 2);")) { puts("FAIL json2.whitespace"); return 1; }

    /* malformed input is a clean runtime error */
    if (!errs("print json_parse(\"[1, 2\");"))          { puts("FAIL json2.err.unclosed"); return 1; }
    if (!errs("print json_parse(\"{bad}\");"))          { puts("FAIL json2.err.badobj"); return 1; }
    if (!errs("print json_parse(\"\");"))               { puts("FAIL json2.err.empty"); return 1; }
    if (!errs("print json_parse(\"tru\");"))            { puts("FAIL json2.err.truncated"); return 1; }
    if (!errs("print json_parse(\"[1,,2]\");"))         { puts("FAIL json2.err.doublecomma"); return 1; }

    puts("PASS test_json2");
    return 0;
}
