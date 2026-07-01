/*
 * source_fuzzer.c -- fuzz the whole front-to-back pipeline.
 *
 * Treats the input as Coil source text and runs it: lex -> parse/compile ->
 * execute (with the GC live).  Exercises every subsystem at once.  A fresh VM
 * per input keeps state isolated across iterations.
 */

#include <stdint.h>
#include <stddef.h>

#include "../src/vm.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    VM vm;
    vm_init(&vm);
    vm_interpret(&vm, (const char *)data, size);
    vm_free(&vm);
    return 0;
}
