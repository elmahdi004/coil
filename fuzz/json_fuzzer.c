/*
 * json_fuzzer.c -- fuzz the JSON parser + serializer.
 *
 * Parses arbitrary bytes as JSON and, on success, serializes the result back.
 * Exercises the recursive descent parser (string escapes, numbers, nesting),
 * the table/array builders, and the serializer.
 */

#include <stdint.h>
#include <stddef.h>

#include "../src/vm.h"
#include "../src/json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    VM vm;
    vm_init(&vm);

    bool ok = false;
    Value v = json_parse(&vm.heap, (const char *)data, size, &ok);
    if (ok) {
        ObjString *s = json_stringify(&vm.heap, v);
        (void)s;
    }

    vm_free(&vm);
    return 0;
}
