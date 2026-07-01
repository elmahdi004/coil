/*
 * test_strings.c -- string concatenation and the string standard library.
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
    /* concatenation and interned equality */
    if (!run(
        "assert(\"foo\" + \"bar\" == \"foobar\");"
        "assert(\"\" + \"x\" == \"x\");"
        "let a = \"ab\"; let b = \"ab\";"
        "assert(a == b);"
        "assert(len(\"hello\") == 5);"
        "assert(len(\"\") == 0);")) { puts("FAIL str.concat"); return 1; }

    /* case folding */
    if (!run(
        "assert(upper(\"abcXYZ\") == \"ABCXYZ\");"
        "assert(lower(\"abcXYZ\") == \"abcxyz\");"
        "assert(upper(\"\") == \"\");")) { puts("FAIL str.case"); return 1; }

    /* substr clamps start and length instead of reading out of bounds */
    if (!run(
        "assert(substr(\"hello\", 1, 3) == \"ell\");"
        "assert(substr(\"hello\", 0, 5) == \"hello\");"
        "assert(substr(\"hello\", 3, 100) == \"lo\");"   /* length clamped */
        "assert(substr(\"hello\", 10, 3) == \"\");"      /* start past end   */
        "assert(substr(\"hello\", 0, 0) == \"\");")) { puts("FAIL str.substr"); return 1; }

    /* find / contains */
    if (!run(
        "assert(find(\"hello\", \"ll\") == 2);"
        "assert(find(\"hello\", \"h\") == 0);"
        "assert(find(\"hello\", \"z\") == -1);"
        "assert(find(\"hello\", \"\") == 0);"
        "assert(contains(\"hello world\", \"o w\"));"
        "assert(!contains(\"hello\", \"xyz\"));")) { puts("FAIL str.find"); return 1; }

    /* repeat and replace */
    if (!run(
        "assert(repeat(\"ab\", 3) == \"ababab\");"
        "assert(repeat(\"x\", 0) == \"\");"
        "assert(replace(\"aaa\", \"a\", \"bb\") == \"bbbbbb\");"
        "assert(replace(\"hello world\", \"o\", \"0\") == \"hell0 w0rld\");"
        "assert(replace(\"abc\", \"z\", \"!\") == \"abc\");")) { puts("FAIL str.replace"); return 1; }

    /* ord / chr round-trip */
    if (!run(
        "assert(ord(\"A\") == 65);"
        "assert(chr(66) == \"B\");"
        "assert(chr(ord(\"z\")) == \"z\");")) { puts("FAIL str.ordchr"); return 1; }

    /* split / join round-trip */
    if (!run(
        "let parts = split(\"a,b,c\", \",\");"
        "assert(len(parts) == 3);"
        "assert(parts[0] == \"a\"); assert(parts[2] == \"c\");"
        "assert(join(parts, \"-\") == \"a-b-c\");"
        "assert(join([1, 2, 3], \"\") == \"123\");"
        "let chars = split(\"abc\", \"\");"
        "assert(len(chars) == 3); assert(chars[1] == \"b\");")) { puts("FAIL str.split"); return 1; }

    /* conversions */
    if (!run(
        "assert(str(42) == \"42\");"
        "assert(str(true) == \"true\");"
        "assert(str(nil) == \"nil\");"
        "assert(int(\"123\") == 123);"
        "assert(int(3.9) == 3);"
        "assert(num(\"2.5\") == 2.5);")) { puts("FAIL str.convert"); return 1; }

    /* a small word-count style pipeline stresses split + tables together */
    if (!run(
        "let words = split(\"the cat the dog the bird\", \" \");"
        "let counts = {};"
        "for (let i = 0; i < len(words); i = i + 1) {"
        "  let w = words[i];"
        "  if (has(counts, w)) { counts[w] = counts[w] + 1; }"
        "  else { counts[w] = 1; }"
        "}"
        "assert(counts[\"the\"] == 3);"
        "assert(counts[\"cat\"] == 1);"
        "assert(len(counts) == 4);")) { puts("FAIL str.wordcount"); return 1; }

    puts("PASS test_strings");
    return 0;
}
