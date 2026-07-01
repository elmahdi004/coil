/*
 * value.c -- value operations and the growable value array
 */

#include "value.h"
#include "object.h"
#include <stdlib.h>
#include <string.h>

bool value_truthy(Value v)
{
    switch (v.type) {
    case VAL_NIL:  return false;
    case VAL_BOOL: return v.as.boolean;
    default:       return true;
    }
}

bool value_equal(Value a, Value b)
{
    /* numbers compare across int/float by numeric value */
    if (IS_NUM(a) && IS_NUM(b)) {
        if (IS_INT(a) && IS_INT(b))
            return a.as.i64 == b.as.i64;
        return value_as_double(a) == value_as_double(b);
    }
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case VAL_NIL:  return true;
    case VAL_BOOL: return a.as.boolean == b.as.boolean;
    case VAL_OBJ:  return a.as.obj == b.as.obj;   /* identity; strings interned */
    default:       return false;
    }
}

void valuearray_init(ValueArray *a)
{
    a->values = NULL;
    a->count  = 0;
    a->cap    = 0;
}

int valuearray_push(ValueArray *a, Value v)
{
    if (a->count == a->cap) {
        size_t ncap = a->cap ? a->cap * 2 : 8;
        Value *nv = (Value *)realloc(a->values, ncap * sizeof(Value));
        if (!nv)
            return -1;
        a->values = nv;
        a->cap    = ncap;
    }
    a->values[a->count] = v;
    return (int)a->count++;
}

void valuearray_free(ValueArray *a)
{
    free(a->values);
    valuearray_init(a);
}
