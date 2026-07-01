/*
 * asm.h -- a textual assembler / disassembler for Coil bytecode.
 *
 * The compiler emits bytecode from source; this module is the layer below it,
 * turning a readable mnemonic listing into the exact same binary a Chunk holds.
 * It is the inverse of debug.c's disassembler and is primarily a testing and
 * introspection tool: it lets verifier and VM tests be written at the opcode
 * level in a form a human can actually read and diff.
 *
 * Grammar (one item per line; '#' begins a comment):
 *     .arity N            top-level function arity   (default 0)
 *     .slots N            declared frame slot count  (default 8)
 *     label:              a jump target
 *     MNEMONIC [operand]  one instruction
 *
 * Operands by mnemonic:
 *     none                NIL TRUE FALSE POP ADD ... RETURN INDEX_GET PRINT HALT
 *     u8 immediate        GET_LOCAL SET_LOCAL GET_UPVALUE SET_UPVALUE
 *                         CALL NEW_ARRAY NEW_TABLE
 *     literal -> const    CONST  (int, float, "string", true, false, nil)
 *     name  -> const      GET_GLOBAL SET_GLOBAL DEF_GLOBAL  ("name" or bareword)
 *     label -> u16 offset JMP JMP_IF_FALSE LOOP
 */

#ifndef COIL_ASM_H
#define COIL_ASM_H

#include "object.h"

/* Assemble text into a single top-level function allocated in h.
 * On error returns NULL and writes a message into err (if err != NULL). */
ObjFunction *coil_assemble_fn(Heap *h, const char *text, size_t len,
                              char *err, size_t errsz);

/* Convenience: assemble, then serialize to a fresh malloc'd module buffer.
 * Returns 0 on success (caller frees *out), -1 on error (message in err). */
int coil_assemble(const char *text, size_t len,
                  uint8_t **out, size_t *out_len, char *err, size_t errsz);

/* Render fn's chunk back into the textual format (jump targets become
 * synthetic labels).  Returned string is heap-allocated in h. */
ObjString *coil_disasm_text(Heap *h, ObjFunction *fn);

#endif /* COIL_ASM_H */
