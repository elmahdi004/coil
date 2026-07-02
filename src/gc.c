/*
 * gc.c -- mark-sweep collector.
 */

#include "gc.h"
#include "object.h"
#include "table.h"
#include "vm.h"
#include "compiler.h"
#include <stdlib.h>

#define GC_HEAP_MIN (1024 * 1024)

void gc_mark_object(Heap *h, Obj *obj)
{
    if (obj == NULL || obj->marked)
        return;
    obj->marked = true;

    if (h->gray_cap < h->gray_count + 1) {
        h->gray_cap = h->gray_cap ? h->gray_cap * 2 : 8;
        h->gray = (Obj **)realloc(h->gray, h->gray_cap * sizeof(Obj *));
        if (h->gray == NULL)
            exit(1);              /* unreachable under a normal rss limit */
    }
    h->gray[h->gray_count++] = obj;
}

void gc_mark_value(Heap *h, Value v)
{
    if (IS_OBJ(v))
        gc_mark_object(h, AS_OBJ(v));
}

static void mark_array(Heap *h, ValueArray *a)
{
    for (size_t i = 0; i < a->count; i++)
        gc_mark_value(h, a->values[i]);
}

static void mark_table(Heap *h, Table *t)
{
    for (size_t i = 0; i < t->cap; i++) {
        Entry *e = &t->entries[i];
        if (e->key != NULL) {
            gc_mark_object(h, (Obj *)e->key);
            gc_mark_value(h, e->value);
        }
    }
}

static void blacken(Heap *h, Obj *obj)
{
    switch (obj->type) {
    case OBJ_STRING:
    case OBJ_NATIVE:
        break;
    case OBJ_UPVALUE:
        gc_mark_value(h, ((ObjUpvalue *)obj)->closed);
        break;
    case OBJ_FUNCTION: {
        ObjFunction *f = (ObjFunction *)obj;
        gc_mark_object(h, (Obj *)f->name);
        mark_array(h, &f->chunk.constants);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *c = (ObjClosure *)obj;
        gc_mark_object(h, (Obj *)c->fn);
        for (int i = 0; i < c->upvalue_count; i++)
            gc_mark_object(h, (Obj *)c->upvalues[i]);
        break;
    }
    case OBJ_ARRAY:
        mark_array(h, &((ObjArray *)obj)->items);
        break;
    case OBJ_TABLE: {
        ObjTable *t = (ObjTable *)obj;
        for (size_t i = 0; i < t->cap; i++) {
            if (t->entries[i].state == 1) {
                gc_mark_value(h, t->entries[i].key);
                gc_mark_value(h, t->entries[i].value);
            }
        }
        break;
    }
    case OBJ_SLICE:
        /* a slice is a lightweight view: it copies no elements and owns no
         * storage of its own, so it contributes nothing to trace here. */
        break;
    }
}

static void mark_roots(Heap *h)
{
    VM *vm = h->vm;
    if (vm != NULL) {
        for (Value *slot = vm->stack; slot < vm->sp; slot++)
            gc_mark_value(h, *slot);
        for (int i = 0; i < vm->frame_count; i++)
            gc_mark_object(h, (Obj *)vm->frames[i].closure);
        for (ObjUpvalue *up = vm->open_upvalues; up != NULL; up = up->next)
            gc_mark_object(h, (Obj *)up);
        mark_table(h, &vm->globals);
    }
    mark_compiler_roots(h);     /* in-progress functions during compilation */
}

static void trace_references(Heap *h)
{
    while (h->gray_count > 0) {
        Obj *obj = h->gray[--h->gray_count];
        blacken(h, obj);
    }
}

/* interned strings are weak references: drop those about to be swept */
static void table_remove_white(Table *t)
{
    for (size_t i = 0; i < t->cap; i++) {
        Entry *e = &t->entries[i];
        if (e->key != NULL && !e->key->obj.marked)
            table_delete(t, e->key);
    }
}

static void sweep(Heap *h)
{
    Obj *prev = NULL;
    Obj *obj  = h->objects;
    while (obj != NULL) {
        if (obj->marked) {
            obj->marked = false;          /* reset for next cycle */
            prev = obj;
            obj  = obj->next;
        } else {
            Obj *unreached = obj;
            obj = obj->next;
            if (prev != NULL)
                prev->next = obj;
            else
                h->objects = obj;
            obj_free(h, unreached);
        }
    }
}

void gc_collect(Heap *h)
{
    if (h->gc_paused)
        return;

    mark_roots(h);
    trace_references(h);
    table_remove_white(&h->strings);
    sweep(h);

    h->next_gc = h->bytes_allocated * 2;
    if (h->next_gc < GC_HEAP_MIN)
        h->next_gc = GC_HEAP_MIN;
}
