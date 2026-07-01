/*
 * test_lexer2.c -- lexer edge cases exercised directly against the token API.
 */
#include "../src/lexer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void expect_types(const char *src, const TokType *want, size_t n)
{
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    for (size_t i = 0; i < n; i++) {
        Token t = lexer_next(&lx);
        if (t.type != want[i]) {
            printf("token %zu: got %d want %d in <%s>\n", i, t.type, want[i], src);
            assert(0);
        }
    }
}

static void test_operators(void)
{
    /* every operator, including the bitwise/shift additions */
    const char *src = "+ - * / % & | ^ ~ << >> ! != = == < > <= >=";
    TokType want[] = {
        TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
        TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_SHL, TOK_SHR,
        TOK_BANG, TOK_NE, TOK_ASSIGN, TOK_EQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
        TOK_EOF
    };
    expect_types(src, want, sizeof(want) / sizeof(want[0]));
    puts("  operators: ok");
}

static void test_numbers(void)
{
    Lexer lx;
    const char *src = "0 42 3.14 1e3 2.5e-2 100";
    lexer_init(&lx, src, strlen(src));

    Token t = lexer_next(&lx); assert(t.type == TOK_INT   && t.ival == 0);
    t = lexer_next(&lx);       assert(t.type == TOK_INT   && t.ival == 42);
    t = lexer_next(&lx);       assert(t.type == TOK_FLOAT && t.fval == 3.14);
    t = lexer_next(&lx);       assert(t.type == TOK_FLOAT && t.fval == 1000.0);
    t = lexer_next(&lx);       assert(t.type == TOK_FLOAT && t.fval == 0.025);
    t = lexer_next(&lx);       assert(t.type == TOK_INT   && t.ival == 100);
    t = lexer_next(&lx);       assert(t.type == TOK_EOF);
    puts("  numbers: ok");
}

static void test_strings_and_comments(void)
{
    /* a comment runs to end of line; strings keep their raw span */
    const char *src = "1 # this is ignored\n\"hello\" 2";
    TokType want[] = { TOK_INT, TOK_STRING, TOK_INT, TOK_EOF };
    expect_types(src, want, 4);

    /* keywords vs identifiers */
    const char *kw = "let x fn if else while for and or print true false nil return";
    TokType kwant[] = {
        TOK_LET, TOK_IDENT, TOK_FN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR,
        TOK_AND, TOK_OR, TOK_PRINT, TOK_TRUE, TOK_FALSE, TOK_NIL, TOK_RETURN, TOK_EOF
    };
    expect_types(kw, kwant, sizeof(kwant) / sizeof(kwant[0]));
    puts("  comments/keywords: ok");
}

static void test_errors(void)
{
    Lexer lx;

    /* unterminated string yields an error token */
    const char *u = "\"no closing quote";
    lexer_init(&lx, u, strlen(u));
    assert(lexer_next(&lx).type == TOK_ERROR);

    /* a stray character yields an error token, then EOF */
    const char *bad = "@";
    lexer_init(&lx, bad, strlen(bad));
    assert(lexer_next(&lx).type == TOK_ERROR);

    /* EOF is reported repeatedly and never runs off the buffer */
    const char *empty = "";
    lexer_init(&lx, empty, 0);
    assert(lexer_next(&lx).type == TOK_EOF);
    assert(lexer_next(&lx).type == TOK_EOF);
    puts("  errors: ok");
}

int main(void)
{
    test_operators();
    test_numbers();
    test_strings_and_comments();
    test_errors();
    puts("PASS test_lexer2");
    return 0;
}
