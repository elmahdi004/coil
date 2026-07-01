/*
 * test_core.c -- smoke tests for the lexer, heap, interning and table.
 */

#include "../src/lexer.h"
#include "../src/memory.h"
#include "../src/object.h"
#include "../src/table.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_lexer(void)
{
    const char *src = "let x = 12 + 3.5; # comment\nprint \"hi\";";
    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    TokType expect[] = {
        TOK_LET, TOK_IDENT, TOK_ASSIGN, TOK_INT, TOK_PLUS, TOK_FLOAT, TOK_SEMI,
        TOK_PRINT, TOK_STRING, TOK_SEMI, TOK_EOF
    };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        Token t = lexer_next(&lx);
        assert(t.type == expect[i]);
        if (t.type == TOK_INT)   assert(t.ival == 12);
        if (t.type == TOK_FLOAT) assert(t.fval == 3.5);
    }
    puts("PASS test_lexer");
}

static void test_intern(void)
{
    Heap h;
    heap_init(&h);

    ObjString *a = string_copy(&h, "hello", 5);
    ObjString *b = string_copy(&h, "hello", 5);
    ObjString *c = string_copy(&h, "world", 5);
    assert(a == b);            /* interned: identical content -> same object */
    assert(a != c);
    assert(a->length == 5);
    assert(strcmp(a->chars, "hello") == 0);

    heap_free(&h);
    puts("PASS test_intern");
}

static void test_table(void)
{
    Heap h;
    heap_init(&h);

    Table t;
    table_init(&t);

    ObjString *k1 = string_copy(&h, "port", 4);
    ObjString *k2 = string_copy(&h, "host", 4);
    assert(table_set(&t, k1, INT_VAL(8080)));   /* new */
    assert(table_set(&t, k2, INT_VAL(1)));
    assert(!table_set(&t, k1, INT_VAL(9090)));  /* update */

    Value v;
    assert(table_get(&t, k1, &v) && IS_INT(v) && AS_INT(v) == 9090);
    assert(table_get(&t, k2, &v) && AS_INT(v) == 1);
    assert(table_delete(&t, k1));
    assert(!table_get(&t, k1, &v));

    /* stress: many keys to force growth/rehash */
    for (int i = 0; i < 500; i++) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "k%d", i);
        ObjString *k = string_copy(&h, buf, (size_t)n);
        assert(table_set(&t, k, INT_VAL(i)));
    }
    for (int i = 0; i < 500; i++) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "k%d", i);
        ObjString *k = string_copy(&h, buf, (size_t)n);
        Value got;
        assert(table_get(&t, k, &got) && AS_INT(got) == i);
    }

    table_free(&t);
    heap_free(&h);
    puts("PASS test_table");
}

static void test_objects(void)
{
    Heap h;
    heap_init(&h);

    ObjArray *arr = array_new(&h);
    for (int i = 0; i < 10; i++)
        valuearray_push(&arr->items, INT_VAL(i * i));
    assert(arr->items.count == 10);
    assert(AS_INT(arr->items.values[3]) == 9);

    ObjFunction *fn = function_new(&h);
    fn->arity = 2;
    fn->name  = string_copy(&h, "add", 3);
    chunk_write(&fn->chunk, OP_RETURN, 1);
    assert(fn->chunk.count == 1);

    ObjClosure *cl = closure_new(&h, fn);
    assert(cl->fn == fn);

    heap_free(&h);             /* frees everything; ASan/leak-clean */
    puts("PASS test_objects");
}

int main(void)
{
    test_lexer();
    test_intern();
    test_table();
    test_objects();
    puts("All core tests passed.");
    return 0;
}
