/*
 * chunk.c -- bytecode chunk storage
 *
 * A chunk's code/line arrays are plain heap buffers owned by the chunk (and
 * thus by the enclosing function object); they are not GC-managed.  The
 * constant pool is a ValueArray; any heap Values it holds are GC objects kept
 * alive because the chunk is reachable from its function.
 */

#include "chunk.h"
#include <stdlib.h>

void chunk_init(Chunk *c)
{
    c->code  = NULL;
    c->lines = NULL;
    c->count = 0;
    c->cap   = 0;
    valuearray_init(&c->constants);
}

void chunk_free(Chunk *c)
{
    free(c->code);
    free(c->lines);
    valuearray_free(&c->constants);
    chunk_init(c);
}

int chunk_write(Chunk *c, uint8_t byte, int line)
{
    if (c->count == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 16;
        uint8_t *nc = (uint8_t *)realloc(c->code, ncap * sizeof(uint8_t));
        if (!nc)
            return -1;
        c->code = nc;                       /* commit before the next realloc */
        int *nl = (int *)realloc(c->lines, ncap * sizeof(int));
        if (!nl)
            return -1;                      /* code grew, lines didn't; state
                                             * stays consistent (cap unchanged) */
        c->lines = nl;
        c->cap   = ncap;
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    return (int)c->count++;
}

int chunk_add_const(Chunk *c, Value v)
{
    return valuearray_push(&c->constants, v);
}
