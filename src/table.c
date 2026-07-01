/*
 * table.c -- open-addressing hash table (linear probing, tombstones)
 *
 * Empty slot:     key == NULL, value == NIL
 * Tombstone:      key == NULL, value == BOOL(true)
 * The table grows (power-of-two capacity) at 0.75 load.
 */

#include "table.h"
#include "object.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LOAD 0.75

void table_init(Table *t)
{
    t->entries = NULL;
    t->count   = 0;
    t->cap     = 0;
}

void table_free(Table *t)
{
    free(t->entries);
    table_init(t);
}

static Entry *find_entry(Entry *entries, size_t cap, ObjString *key)
{
    size_t idx = key->hash & (cap - 1);
    Entry *tombstone = NULL;

    for (;;) {
        Entry *e = &entries[idx];
        if (e->key == NULL) {
            if (IS_NIL(e->value))
                return tombstone ? tombstone : e;   /* truly empty */
            if (tombstone == NULL)
                tombstone = e;                       /* remember first tombstone */
        } else if (e->key == key) {
            return e;
        }
        idx = (idx + 1) & (cap - 1);
    }
}

static void adjust_cap(Table *t, size_t cap)
{
    Entry *entries = (Entry *)malloc(cap * sizeof(Entry));
    for (size_t i = 0; i < cap; i++) {
        entries[i].key   = NULL;
        entries[i].value = NIL_VAL;
    }

    t->count = 0;
    for (size_t i = 0; i < t->cap; i++) {
        Entry *e = &t->entries[i];
        if (e->key == NULL)
            continue;
        Entry *dest = find_entry(entries, cap, e->key);
        dest->key   = e->key;
        dest->value = e->value;
        t->count++;
    }

    free(t->entries);
    t->entries = entries;
    t->cap     = cap;
}

bool table_set(Table *t, ObjString *key, Value value)
{
    if ((double)(t->count + 1) > (double)t->cap * MAX_LOAD) {
        size_t cap = t->cap ? t->cap * 2 : 8;
        adjust_cap(t, cap);
    }

    Entry *e = find_entry(t->entries, t->cap, key);
    bool is_new = (e->key == NULL);
    if (is_new && IS_NIL(e->value))
        t->count++;                /* filling an empty (not a tombstone) slot */

    e->key   = key;
    e->value = value;
    return is_new;
}

bool table_get(const Table *t, ObjString *key, Value *out)
{
    if (t->cap == 0)
        return false;
    Entry *e = find_entry(t->entries, t->cap, key);
    if (e->key == NULL)
        return false;
    *out = e->value;
    return true;
}

bool table_delete(Table *t, ObjString *key)
{
    if (t->cap == 0)
        return false;
    Entry *e = find_entry(t->entries, t->cap, key);
    if (e->key == NULL)
        return false;
    e->key   = NULL;
    e->value = BOOL_VAL(true);     /* tombstone */
    return true;
}

ObjString *table_find_string(const Table *t, const char *chars,
                             uint32_t length, uint32_t hash)
{
    if (t->cap == 0)
        return NULL;

    size_t idx = hash & (t->cap - 1);
    for (;;) {
        Entry *e = &t->entries[idx];
        if (e->key == NULL) {
            if (IS_NIL(e->value))
                return NULL;       /* empty slot: not present */
        } else if (e->key->length == length &&
                   e->key->hash == hash &&
                   memcmp(e->key->chars, chars, length) == 0) {
            return e->key;
        }
        idx = (idx + 1) & (t->cap - 1);
    }
}
