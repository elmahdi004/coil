/*
 * memory.c -- the heap allocator
 */

#include "memory.h"
#include "object.h"
#include "gc.h"
#include <stdlib.h>

void heap_init(Heap *h)
{
    h->objects         = NULL;
    h->bytes_allocated = 0;
    h->next_gc         = 1024 * 1024;
    h->gray            = NULL;
    h->gray_count      = 0;
    h->gray_cap        = 0;
    h->vm              = NULL;
    h->gc_paused       = false;
    table_init(&h->strings);
}

void *reallocate(Heap *h, void *ptr, size_t old_size, size_t new_size)
{
    h->bytes_allocated += new_size - old_size;   /* wraps harmlessly on free */

    if (new_size > old_size) {
#ifdef COIL_GC_STRESS
        gc_collect(h);
#else
        if (h->bytes_allocated > h->next_gc)
            gc_collect(h);
#endif
    }

    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);               /* NULL on failure */
}

void heap_free(Heap *h)
{
    Obj *o = h->objects;
    while (o) {
        Obj *next = o->next;
        obj_free(h, o);
        o = next;
    }
    h->objects = NULL;
    table_free(&h->strings);
    free(h->gray);
    h->gray = NULL;
}
