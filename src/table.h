/*
 * table.h -- open-addressing hash table keyed by interned strings
 *
 * Used for the VM's globals and for the string-intern set.  Keys are
 * ObjString* compared by identity (strings are interned, so identity == value
 * equality); table_find_string looks a key up by content for interning.
 */

#ifndef COIL_TABLE_H
#define COIL_TABLE_H

#include "value.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct ObjString ObjString;   /* full definition in object.h */

typedef struct {
    ObjString *key;     /* NULL = empty or tombstone (see value) */
    Value      value;
} Entry;

typedef struct {
    Entry  *entries;
    size_t  count;      /* live entries + tombstones */
    size_t  cap;        /* power of two, or 0 */
} Table;

void  table_init(Table *t);
void  table_free(Table *t);

/* returns true if the key was newly inserted (vs. updated) */
bool  table_set(Table *t, ObjString *key, Value value);
bool  table_get(const Table *t, ObjString *key, Value *out);
bool  table_delete(Table *t, ObjString *key);

/* interning lookup: find an existing key matching chars/hash, or NULL */
ObjString *table_find_string(const Table *t, const char *chars,
                             uint32_t length, uint32_t hash);

#endif /* COIL_TABLE_H */
