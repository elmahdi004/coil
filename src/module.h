/*
 * module.h -- (de)serialize a compiled program as a bytecode module.
 *
 * Format (all integers little-endian):
 *   [4]  magic "COIL"
 *   [2]  version
 *   [2]  function_count
 *   function_count x function:
 *     [1]  arity
 *     [1]  upvalue_count
 *     [2]  num_slots
 *     [1]  has_name; if 1: [4] name_len [name_len bytes]
 *     [4]  code_len   [code_len bytes]
 *     [2]  const_count
 *     const_count x constant:
 *       [1] tag  (0 nil, 1 bool[1], 2 int[8], 3 float[8],
 *                 4 string[4 len + bytes], 5 funcref[2 index])
 *   [2]  entry_index
 *
 * module_load reads UNTRUSTED bytes and is fully bounds-checked; it produces a
 * structurally-decoded function graph but does NOT prove the bytecode safe to
 * run -- that is the verifier's job (verify.h).
 */

#ifndef COIL_MODULE_H
#define COIL_MODULE_H

#include "object.h"

enum {
    MODULE_OK         =  0,
    MODULE_ERR_FORMAT = -1,
    MODULE_ERR_NOMEM  = -2
};

/* Serialize entry and every function reachable through its constants.
 * On success *out is a malloc'd buffer of *out_len bytes (caller frees). */
int module_serialize(ObjFunction *entry, uint8_t **out, size_t *out_len);

/* Load a module from data[0,size). On success *entry is the entry function. */
int module_load(Heap *h, const uint8_t *data, size_t size, ObjFunction **entry);

#endif /* COIL_MODULE_H */
