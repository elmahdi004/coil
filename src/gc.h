/*
 * gc.h -- tracing mark-sweep garbage collector.
 *
 * Collection is driven from reallocate() when the live byte count crosses a
 * threshold (or on every allocation when built with -DCOIL_GC_STRESS).  Roots
 * are the VM value stack, call frames, globals, open upvalues, and any
 * in-progress compiler functions.  Interned strings are weak: unmarked ones are
 * pruned from the intern set before the sweep.
 */

#ifndef COIL_GC_H
#define COIL_GC_H

#include "memory.h"
#include "value.h"

void gc_collect(Heap *h);
void gc_mark_object(Heap *h, Obj *obj);
void gc_mark_value(Heap *h, Value v);

#endif /* COIL_GC_H */
