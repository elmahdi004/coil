/*
 * debug.c -- bytecode disassembler
 */

#include "debug.h"
#include "object.h"
#include <stdio.h>

static int simple(const char *name, int offset)
{
    printf("%s\n", name);
    return offset + 1;
}

static int byte_op(const char *name, const Chunk *c, int offset)
{
    uint8_t slot = c->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int const_op(const char *name, const Chunk *c, int offset)
{
    uint8_t idx = c->code[offset + 1];
    printf("%-16s %4d '", name, idx);
    if (idx < c->constants.count)
        value_print(c->constants.values[idx]);
    printf("'\n");
    return offset + 2;
}

static int jump_op(const char *name, int sign, const Chunk *c, int offset)
{
    uint16_t jump = (uint16_t)(c->code[offset + 1] << 8);
    jump |= c->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int disassemble_instruction(const Chunk *c, int offset)
{
    printf("%04d ", offset);
    if (offset > 0 && c->lines[offset] == c->lines[offset - 1])
        printf("   | ");
    else
        printf("%4d ", c->lines[offset]);

    uint8_t op = c->code[offset];
    switch (op) {
    case OP_CONST:        return const_op("OP_CONST", c, offset);
    case OP_NIL:          return simple("OP_NIL", offset);
    case OP_TRUE:         return simple("OP_TRUE", offset);
    case OP_FALSE:        return simple("OP_FALSE", offset);
    case OP_POP:          return simple("OP_POP", offset);
    case OP_ADD:          return simple("OP_ADD", offset);
    case OP_SUB:          return simple("OP_SUB", offset);
    case OP_MUL:          return simple("OP_MUL", offset);
    case OP_DIV:          return simple("OP_DIV", offset);
    case OP_MOD:          return simple("OP_MOD", offset);
    case OP_NEG:          return simple("OP_NEG", offset);
    case OP_NOT:          return simple("OP_NOT", offset);
    case OP_BAND:         return simple("OP_BAND", offset);
    case OP_BOR:          return simple("OP_BOR", offset);
    case OP_BXOR:         return simple("OP_BXOR", offset);
    case OP_BNOT:         return simple("OP_BNOT", offset);
    case OP_SHL:          return simple("OP_SHL", offset);
    case OP_SHR:          return simple("OP_SHR", offset);
    case OP_EQ:           return simple("OP_EQ", offset);
    case OP_NE:           return simple("OP_NE", offset);
    case OP_LT:           return simple("OP_LT", offset);
    case OP_LE:           return simple("OP_LE", offset);
    case OP_GT:           return simple("OP_GT", offset);
    case OP_GE:           return simple("OP_GE", offset);
    case OP_JMP:          return jump_op("OP_JMP", 1, c, offset);
    case OP_JMP_IF_FALSE: return jump_op("OP_JMP_IF_FALSE", 1, c, offset);
    case OP_LOOP:         return jump_op("OP_LOOP", -1, c, offset);
    case OP_GET_LOCAL:    return byte_op("OP_GET_LOCAL", c, offset);
    case OP_SET_LOCAL:    return byte_op("OP_SET_LOCAL", c, offset);
    case OP_GET_GLOBAL:   return const_op("OP_GET_GLOBAL", c, offset);
    case OP_SET_GLOBAL:   return const_op("OP_SET_GLOBAL", c, offset);
    case OP_DEF_GLOBAL:   return const_op("OP_DEF_GLOBAL", c, offset);
    case OP_GET_UPVALUE:  return byte_op("OP_GET_UPVALUE", c, offset);
    case OP_SET_UPVALUE:  return byte_op("OP_SET_UPVALUE", c, offset);
    case OP_CALL:         return byte_op("OP_CALL", c, offset);
    case OP_RETURN:       return simple("OP_RETURN", offset);
    case OP_CLOSURE: {
        uint8_t idx = c->code[offset + 1];
        printf("%-16s %4d '", "OP_CLOSURE", idx);
        if (idx < c->constants.count)
            value_print(c->constants.values[idx]);
        printf("'\n");
        offset += 2;
        if (idx < c->constants.count && IS_FUNCTION(c->constants.values[idx])) {
            ObjFunction *f = AS_FUNCTION(c->constants.values[idx]);
            for (int j = 0; j < f->upvalue_count; j++) {
                int is_local = c->code[offset++];
                int index    = c->code[offset++];
                printf("%04d    |   %s %d\n", offset - 2,
                       is_local ? "local" : "upvalue", index);
            }
        }
        return offset;
    }
    case OP_CLOSE_UPVALUE: return simple("OP_CLOSE_UPVALUE", offset);
    case OP_NEW_ARRAY:     return byte_op("OP_NEW_ARRAY", c, offset);
    case OP_NEW_TABLE:     return byte_op("OP_NEW_TABLE", c, offset);
    case OP_INDEX_GET:     return simple("OP_INDEX_GET", offset);
    case OP_INDEX_SET:     return simple("OP_INDEX_SET", offset);
    case OP_LEN:           return simple("OP_LEN", offset);
    case OP_PRINT:         return simple("OP_PRINT", offset);
    case OP_HALT:          return simple("OP_HALT", offset);
    default:
        printf("unknown opcode %d\n", op);
        return offset + 1;
    }
}

void disassemble_chunk(const Chunk *c, const char *name)
{
    printf("== %s ==\n", name);
    for (int offset = 0; offset < (int)c->count;)
        offset = disassemble_instruction(c, offset);
}
