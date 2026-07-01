/*
 * object.h -- heap objects and their constructors
 */

#ifndef COIL_OBJECT_H
#define COIL_OBJECT_H

#include "value.h"
#include "chunk.h"
#include "memory.h"
#include <stdbool.h>

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_TABLE,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE
} ObjType;

struct Obj {
    ObjType      type;
    bool         marked;
    struct Obj  *next;        /* intrusive heap list */
};

struct ObjString {
    Obj       obj;
    uint32_t  length;
    uint32_t  hash;
    char     *chars;          /* NUL-terminated, length bytes + 1 */
};

typedef struct {
    Obj         obj;
    ValueArray  items;
} ObjArray;

/* a value-keyed hash entry; state 0 empty, 1 live, 2 tombstone */
typedef struct {
    Value   key;
    Value   value;
    uint8_t state;
} VEntry;

typedef struct {
    Obj      obj;
    VEntry  *entries;
    size_t   count;       /* live + tombstones */
    size_t   live;        /* live entries only */
    size_t   cap;
} ObjTable;

typedef struct {
    Obj        obj;
    int        arity;
    int        upvalue_count;
    uint16_t   num_slots;     /* max local slots (frame window size) */
    Chunk      chunk;
    ObjString *name;          /* NULL for the top-level script */
} ObjFunction;

typedef struct ObjUpvalue {
    Obj                obj;
    Value             *location;   /* into the value stack, or &closed */
    Value              closed;     /* the value once closed over */
    struct ObjUpvalue *next;       /* list of open upvalues (VM) */
} ObjUpvalue;

typedef struct {
    Obj          obj;
    ObjFunction *fn;
    ObjUpvalue **upvalues;
    int          upvalue_count;
} ObjClosure;

/* A native: reads argc args, writes *result, returns false on error. */
typedef bool (*NativeFn)(Heap *h, int argc, Value *args, Value *result);

typedef struct {
    Obj      obj;
    NativeFn fn;
    int      arity;
} ObjNative;

#define OBJ_TYPE(v)     (AS_OBJ(v)->type)
#define AS_STRING(v)    ((ObjString *)AS_OBJ(v))
#define AS_CSTRING(v)   (((ObjString *)AS_OBJ(v))->chars)
#define AS_ARRAY(v)     ((ObjArray *)AS_OBJ(v))
#define AS_TABLE(v)     ((ObjTable *)AS_OBJ(v))
#define AS_FUNCTION(v)  ((ObjFunction *)AS_OBJ(v))
#define AS_CLOSURE(v)   ((ObjClosure *)AS_OBJ(v))
#define AS_NATIVE(v)    ((ObjNative *)AS_OBJ(v))

static inline bool is_obj_type(Value v, ObjType t)
{
    return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#define IS_STRING(v)    is_obj_type(v, OBJ_STRING)
#define IS_ARRAY(v)     is_obj_type(v, OBJ_ARRAY)
#define IS_TABLE(v)     is_obj_type(v, OBJ_TABLE)
#define IS_FUNCTION(v)  is_obj_type(v, OBJ_FUNCTION)
#define IS_CLOSURE(v)   is_obj_type(v, OBJ_CLOSURE)
#define IS_NATIVE(v)    is_obj_type(v, OBJ_NATIVE)

uint32_t hash_string(const char *key, size_t length);

/* copy `length` bytes and intern; the object owns its own copy */
ObjString  *string_copy(Heap *h, const char *chars, size_t length);
/* take ownership of a heap buffer `chars` (length bytes) and intern */
ObjString  *string_take(Heap *h, char *chars, size_t length);

ObjArray    *array_new(Heap *h);
ObjTable    *table_obj_new(Heap *h);
ObjFunction *function_new(Heap *h);

/* value-keyed table operations (keys must be hashable: nil/bool/int/float/string) */
bool  value_hashable(Value v);
bool  vtable_get(ObjTable *t, Value key, Value *out);
int   vtable_set(ObjTable *t, Value key, Value value);   /* 0 ok, -1 nomem */
bool  vtable_delete(ObjTable *t, Value key);
void  vtable_free(ObjTable *t);
ObjClosure  *closure_new(Heap *h, ObjFunction *fn);
ObjUpvalue  *upvalue_new(Heap *h, Value *slot);
ObjNative   *native_new(Heap *h, NativeFn fn, int arity);

void obj_free(Heap *h, Obj *obj);   /* free one object's storage */

void value_print(Value v);          /* write a human-readable form to stdout */

#endif /* COIL_OBJECT_H */
