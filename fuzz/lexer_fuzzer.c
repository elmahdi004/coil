/*
 * lexer_fuzzer.c -- drive the tokenizer over arbitrary bytes.
 *
 * Builds once you implement src/lexer.c.  Tokenizes the input to completion so
 * the whole scanner is exercised.  More fuzz targets (parser, compiler, vm,
 * bytecode) get added as those milestones land.
 */

#include <stdint.h>
#include <stddef.h>

#include "../src/lexer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    Lexer lx;
    lexer_init(&lx, (const char *)data, size);

    for (;;) {
        Token t = lexer_next(&lx);
        if (t.type == TOK_EOF || t.type == TOK_ERROR)
            break;
    }
    return 0;
}
