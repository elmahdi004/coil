/*
 * object.c -- heap object construction, interning and destruction
 */

#include "object.h"
#include "table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Obj *alloc_obj(Heap *h, size_t size, ObjType type)
{
    Obj *o = (Obj *)reallocate(h, NULL, 0, size);
    o->type   = type;
    o->marked = false;
    o->next   = h->objects;
    h->objects = o;
    return o;
}

#define ALLOC_OBJ(h, ctype, otype) \
    ((ctype *)alloc_obj((h), sizeof(ctype), (otype)))

uint32_t hash_string(const char *key, size_t length)
{
    uint32_t hash = 2166136261u;            /* FNV-1a */
    for (size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619u;
    }
    return hash;
}

/* wrap an owned, NUL-terminated buffer in an ObjString and intern it */
static ObjString *alloc_string(Heap *h, char *chars, uint32_t length,
                               uint32_t hash)
{
    ObjString *s = ALLOC_OBJ(h, ObjString, OBJ_STRING);
    s->length = length;
    s->hash   = hash;
    s->chars  = chars;
    table_set(&h->strings, s, NIL_VAL);     /* the intern set */
    return s;
}

ObjString *string_copy(Heap *h, const char *chars, size_t length)
{
    uint32_t hash = hash_string(chars, length);
    ObjString *interned =
        table_find_string(&h->strings, chars, (uint32_t)length, hash);
    if (interned)
        return interned;

    char *owned = ALLOCATE(h, char, length + 1);
    memcpy(owned, chars, length);
    owned[length] = '\0';
    return alloc_string(h, owned, (uint32_t)length, hash);
}

ObjString *string_take(Heap *h, char *chars, size_t length)
{
    uint32_t hash = hash_string(chars, length);
    ObjString *interned =
        table_find_string(&h->strings, chars, (uint32_t)length, hash);
    if (interned) {
        FREE_ARRAY(h, char, chars, length + 1);
        return interned;
    }
    return alloc_string(h, chars, (uint32_t)length, hash);
}

ObjArray *array_new(Heap *h)
{
    ObjArray *a = ALLOC_OBJ(h, ObjArray, OBJ_ARRAY);
    valuearray_init(&a->items);
    return a;
}

ObjTable *table_obj_new(Heap *h)
{
    ObjTable *t = ALLOC_OBJ(h, ObjTable, OBJ_TABLE);
    t->entries = NULL;
    t->count   = 0;
    t->live    = 0;
    t->cap     = 0;
    return t;
}

ObjFunction *function_new(Heap *h)
{
    ObjFunction *f = ALLOC_OBJ(h, ObjFunction, OBJ_FUNCTION);
    f->arity         = 0;
    f->upvalue_count = 0;
    f->num_slots     = 1;        /* slot 0 is always the callee */
    f->name          = NULL;
    chunk_init(&f->chunk);
    return f;
}

ObjClosure *closure_new(Heap *h, ObjFunction *fn)
{
    ObjUpvalue **upvalues = ALLOCATE(h, ObjUpvalue *, fn->upvalue_count);
    for (int i = 0; i < fn->upvalue_count; i++)
        upvalues[i] = NULL;

    ObjClosure *c = ALLOC_OBJ(h, ObjClosure, OBJ_CLOSURE);
    c->fn            = fn;
    c->upvalues      = upvalues;
    c->upvalue_count = fn->upvalue_count;
    return c;
}

ObjUpvalue *upvalue_new(Heap *h, Value *slot)
{
    ObjUpvalue *u = ALLOC_OBJ(h, ObjUpvalue, OBJ_UPVALUE);
    u->location = slot;
    u->closed   = NIL_VAL;
    u->next     = NULL;
    return u;
}

ObjNative *native_new(Heap *h, NativeFn fn, int arity)
{
    ObjNative *n = ALLOC_OBJ(h, ObjNative, OBJ_NATIVE);
    n->fn    = fn;
    n->arity = arity;
    return n;
}

void value_print(Value v)
{
    switch (v.type) {
    case VAL_NIL:   printf("nil"); break;
    case VAL_BOOL:  printf(v.as.boolean ? "true" : "false"); break;
    case VAL_INT:   printf("%lld", (long long)v.as.i64); break;
    case VAL_FLOAT: printf("%g", v.as.f64); break;
    case VAL_OBJ:
        switch (OBJ_TYPE(v)) {
        case OBJ_STRING:   printf("%s", AS_CSTRING(v)); break;
        case OBJ_ARRAY:    printf("[array %zu]", AS_ARRAY(v)->items.count); break;
        case OBJ_TABLE:    printf("[table %zu]", AS_TABLE(v)->live); break;
        case OBJ_FUNCTION: {
            ObjFunction *f = AS_FUNCTION(v);
            printf("<fn %s>", f->name ? f->name->chars : "script");
            break;
        }
        case OBJ_CLOSURE: {
            ObjFunction *f = AS_CLOSURE(v)->fn;
            printf("<fn %s>", f->name ? f->name->chars : "script");
            break;
        }
        case OBJ_UPVALUE:  printf("<upvalue>"); break;
        case OBJ_NATIVE:   printf("<native fn>"); break;
        }
        break;
    }
}

void obj_free(Heap *h, Obj *obj)
{
    switch (obj->type) {
    case OBJ_STRING: {
        ObjString *s = (ObjString *)obj;
        FREE_ARRAY(h, char, s->chars, s->length + 1);
        FREE(h, ObjString, obj);
        break;
    }
    case OBJ_ARRAY: {
        ObjArray *a = (ObjArray *)obj;
        valuearray_free(&a->items);
        FREE(h, ObjArray, obj);
        break;
    }
    case OBJ_TABLE: {
        ObjTable *t = (ObjTable *)obj;
        vtable_free(t);
        FREE(h, ObjTable, obj);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *f = (ObjFunction *)obj;
        chunk_free(&f->chunk);
        FREE(h, ObjFunction, obj);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *c = (ObjClosure *)obj;
        FREE_ARRAY(h, ObjUpvalue *, c->upvalues, c->upvalue_count);
        FREE(h, ObjClosure, obj);   /* the wrapped function is freed separately */
        break;
    }
    case OBJ_UPVALUE:
        FREE(h, ObjUpvalue, obj);
        break;
    case OBJ_NATIVE:
        FREE(h, ObjNative, obj);
        break;
    }
}
