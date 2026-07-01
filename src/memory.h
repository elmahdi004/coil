/*
 * memory.h -- the managed heap and the one allocation entry point
 *
 * Every GC object is allocated through reallocate(), which keeps a running
 * byte count and (from M7) drives collection.  The heap owns the intrusive list
 * of all live objects and the string-intern set.
 */

#ifndef COIL_MEMORY_H
#define COIL_MEMORY_H

#include "value.h"
#include "table.h"
#include <stddef.h>

typedef struct {
    Obj    *objects;          /* intrusive list of all live objects */
    size_t  bytes_allocated;
    size_t  next_gc;          /* threshold (M7) */
    Obj   **gray;             /* GC worklist (M7) */
    size_t  gray_count;
    size_t  gray_cap;
    Table   strings;          /* string-intern set */
    struct VM *vm;            /* root source for collection (M7); may be NULL */
    bool    gc_paused;        /* suppress collection during bulk loads */
} Heap;

void  heap_init(Heap *h);
void  heap_free(Heap *h);     /* free every object and the intern set */

/*
 * Resize an allocation: ptr==NULL grows from nothing; new_size==0 frees.
 * Returns the (possibly moved) block, or NULL on failure / free.
 */
void *reallocate(Heap *h, void *ptr, size_t old_size, size_t new_size);

#define ALLOCATE(h, type, count) \
    ((type *)reallocate((h), NULL, 0, sizeof(type) * (size_t)(count)))

#define FREE(h, type, ptr) \
    reallocate((h), (ptr), sizeof(type), 0)

#define GROW_ARRAY(h, type, ptr, old_count, new_count) \
    ((type *)reallocate((h), (ptr), sizeof(type) * (size_t)(old_count), \
                        sizeof(type) * (size_t)(new_count)))

#define FREE_ARRAY(h, type, ptr, count) \
    reallocate((h), (ptr), sizeof(type) * (size_t)(count), 0)

#endif /* COIL_MEMORY_H */
