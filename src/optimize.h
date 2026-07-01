/*
 * optimize.h -- a peephole optimizer over a compiled Chunk.
 *
 * Produces a new function whose bytecode computes exactly the same result as
 * the input but with some obvious waste removed:
 *   - unary constant folding      (CONST k; NEG   -> CONST -k)
 *   - dead pushes                 (CONST k; POP   -> nothing)
 *   - jump threading              (JMP a; a: JMP b -> JMP b)
 *   - jump-to-next elimination    (JMP over nothing -> nothing)
 *   - unreachable-code removal    (anything after RETURN/HALT/JMP with no label)
 *
 * Jumps are tracked by target *instruction*, not byte offset, so removing or
 * folding instructions never corrupts a branch.  The result is intended to
 * still pass the verifier and is validated against it in the tests.
 *
 * Only the given function's own chunk is rewritten; nested function constants
 * are carried over untouched.
 */

#ifndef COIL_OPTIMIZE_H
#define COIL_OPTIMIZE_H

#include "object.h"

/* Return a new, optimized copy of fn (allocated in h), or NULL on failure.
 * The original is left unchanged. */
ObjFunction *optimize_function(Heap *h, ObjFunction *fn);

#endif /* COIL_OPTIMIZE_H */
