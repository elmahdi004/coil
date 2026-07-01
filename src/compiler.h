/*
 * compiler.h -- compile Coil source to a top-level function.
 */

#ifndef COIL_COMPILER_H
#define COIL_COMPILER_H

#include "object.h"

/* Compile source[0,len) into a top-level ObjFunction, or NULL on error. */
ObjFunction *compile(Heap *h, const char *source, size_t len);

/* GC hook: mark the functions of any in-progress compilers. */
void mark_compiler_roots(Heap *h);

#endif /* COIL_COMPILER_H */
