/*
 * objtable.c -- value-keyed hash map (the script-level table type).
 *
 * Open addressing with linear probing and tombstones.  Keys may be any
 * hashable value (nil, bool, int, float, string); strings are interned so they
 * compare by identity.  The backing array is a plain allocation owned by the
 * ObjTable; the GC marks the live keys and values, not the array.
 */

#include "object.h"
#include <stdlib.h>
#include <string.h>

#define TABLE_MAX_LOAD 0.7

bool value_hashable(Value v)
{
    switch (v.type) {
    case VAL_NIL: case VAL_BOOL: case VAL_INT: case VAL_FLOAT:
        return true;
    case VAL_OBJ:
        return v.as.obj->type == OBJ_STRING;
    default:
        return false;
    }
}

static uint32_t hash_value(Value v)
{
    switch (v.type) {
    case VAL_NIL:   return 0u;
    case VAL_BOOL:  return v.as.boolean ? 1u : 2u;
    case VAL_INT: { uint64_t x = (uint64_t)v.as.i64; return hash_string((const char *)&x, 8); }
    case VAL_FLOAT: {
        double d = v.as.f64;
        if (d == 0.0) d = 0.0;                 /* normalise -0.0 */
        uint64_t x; memcpy(&x, &d, 8);
        return hash_string((const char *)&x, 8);
    }
    case VAL_OBJ:
        return ((ObjString *)v.as.obj)->hash;  /* only strings are hashable objs */
    default:
        return 0u;
    }
}

static VEntry *find_entry(VEntry *entries, size_t cap, Value key, uint32_t h)
{
    size_t  idx = h & (cap - 1);
    VEntry *tomb = NULL;
    for (;;) {
        VEntry *e = &entries[idx];
        if (e->state == 0)
            return tomb ? tomb : e;            /* empty */
        if (e->state == 2) {
            if (!tomb) tomb = e;               /* tombstone */
        } else if (value_equal(e->key, key)) {
            return e;                          /* live match */
        }
        idx = (idx + 1) & (cap - 1);
    }
}

static int grow(ObjTable *t, size_t cap)
{
    VEntry *ne = (VEntry *)malloc(cap * sizeof(VEntry));
    if (!ne) return -1;
    for (size_t i = 0; i < cap; i++) ne[i].state = 0;

    size_t live = 0;
    for (size_t i = 0; i < t->cap; i++) {
        VEntry *e = &t->entries[i];
        if (e->state != 1) continue;
        VEntry *d = find_entry(ne, cap, e->key, hash_value(e->key));
        d->key   = e->key;
        d->value = e->value;
        d->state = 1;
        live++;
    }
    free(t->entries);
    t->entries = ne;
    t->cap     = cap;
    t->count   = live;
    t->live    = live;
    return 0;
}

bool vtable_get(ObjTable *t, Value key, Value *out)
{
    if (t->live == 0) return false;
    VEntry *e = find_entry(t->entries, t->cap, key, hash_value(key));
    if (e->state != 1) return false;
    *out = e->value;
    return true;
}

int vtable_set(ObjTable *t, Value key, Value value)
{
    if ((double)(t->count + 1) > (double)t->cap * TABLE_MAX_LOAD) {
        size_t cap = t->cap ? t->cap * 2 : 8;
        if (grow(t, cap) != 0) return -1;
    }
    VEntry *e = find_entry(t->entries, t->cap, key, hash_value(key));
    if (e->state == 0) t->count++;             /* using a fresh (non-tombstone) slot */
    if (e->state != 1) t->live++;              /* new live entry */
    e->key   = key;
    e->value = value;
    e->state = 1;
    return 0;
}

bool vtable_delete(ObjTable *t, Value key)
{
    if (t->live == 0) return false;
    VEntry *e = find_entry(t->entries, t->cap, key, hash_value(key));
    if (e->state != 1) return false;
    e->state = 2;                              /* tombstone */
    t->live--;
    return true;
}

void vtable_free(ObjTable *t)
{
    free(t->entries);
    t->entries = NULL;
    t->count = t->live = t->cap = 0;
}
