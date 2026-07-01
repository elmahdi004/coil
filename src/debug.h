/*
 * debug.h -- bytecode disassembler (development aid)
 */

#ifndef COIL_DEBUG_H
#define COIL_DEBUG_H

#include "chunk.h"

void disassemble_chunk(const Chunk *c, const char *name);
int  disassemble_instruction(const Chunk *c, int offset);

#endif /* COIL_DEBUG_H */
