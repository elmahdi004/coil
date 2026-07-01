/*
 * chunk.h -- a unit of compiled bytecode (code + constant pool + line info)
 */

#ifndef COIL_CHUNK_H
#define COIL_CHUNK_H

#include "value.h"

typedef enum {
    OP_CONST,          /* u8 const-index -> push constant            */
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_NOT,
    OP_BAND, OP_BOR, OP_BXOR, OP_BNOT, OP_SHL, OP_SHR,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JMP,            /* u16 offset (forward)                       */
    OP_JMP_IF_FALSE,   /* u16 offset (forward)                       */
    OP_LOOP,           /* u16 offset (backward)                      */
    OP_GET_LOCAL, OP_SET_LOCAL,        /* u8 slot                    */
    OP_GET_GLOBAL, OP_SET_GLOBAL, OP_DEF_GLOBAL,  /* u8 name-const   */
    OP_GET_UPVALUE, OP_SET_UPVALUE,    /* u8 upvalue-index           */
    OP_CALL,           /* u8 argc                                    */
    OP_RETURN,
    OP_CLOSURE,        /* u8 fn-const, then (u8 is_local, u8 index)* */
    OP_CLOSE_UPVALUE,
    OP_NEW_ARRAY,      /* u8 element-count                           */
    OP_NEW_TABLE,      /* u8 pair-count                              */
    OP_INDEX_GET, OP_INDEX_SET,
    OP_LEN,
    OP_PRINT,
    OP_HALT
} OpCode;

typedef struct {
    uint8_t    *code;
    int        *lines;
    size_t      count;
    size_t      cap;
    ValueArray  constants;
} Chunk;

void chunk_init(Chunk *c);
void chunk_free(Chunk *c);

/* append one byte; returns its offset, or -1 on allocation failure */
int  chunk_write(Chunk *c, uint8_t byte, int line);

/* intern a constant; returns its index, or -1 on allocation failure */
int  chunk_add_const(Chunk *c, Value v);

#endif /* COIL_CHUNK_H */
