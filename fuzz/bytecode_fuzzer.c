/*
 * bytecode_fuzzer.c -- fuzz the bytecode loader, verifier, and VM together.
 *
 * Treats the input as a serialized bytecode module: load it (bounds-checked),
 * verify it, and run it only if verification succeeds.  This drives the whole
 * untrusted-module path -- decoding, verification, and interpretation -- over
 * arbitrary input bytes.
 */

#include <stdint.h>
#include <stddef.h>

#include "../src/vm.h"
#include "../src/module.h"
#include "../src/verify.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    VM vm;
    vm_init(&vm);

    ObjFunction *entry = NULL;
    if (module_load(&vm.heap, data, size, &entry) == MODULE_OK && entry) {
        if (verify_module(entry) == VERIFY_OK)
            vm_run_function(&vm, entry);
    }

    vm_free(&vm);
    return 0;
}
