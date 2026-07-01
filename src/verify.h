/*
 * verify.h -- bytecode verifier.
 *
 * Proves a loaded module is safe for the VM to execute without per-instruction
 * bounds checks: every constant/local/upvalue index is in range, every jump
 * lands on an instruction boundary, the operand stream never runs past the
 * code, control never falls off the end, and the value-stack height is
 * consistent at every merge point and never exceeds MAX_FRAME_STACK.
 *
 * Returns VERIFY_OK only if every reachable function passes.
 */

#ifndef COIL_VERIFY_H
#define COIL_VERIFY_H

#include "object.h"

enum { VERIFY_OK = 0, VERIFY_FAIL = -1 };

int verify_module(ObjFunction *entry);

#endif /* COIL_VERIFY_H */
