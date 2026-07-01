/*
 * value.h -- Coil runtime values (tagged union)
 */

#ifndef COIL_VALUE_H
#define COIL_VALUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct Obj Obj;          /* heap object header (object.h) */

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool    boolean;
        int64_t i64;
        double  f64;
        Obj    *obj;
    } as;
} Value;

#define NIL_VAL          ((Value){ VAL_NIL,   { .i64 = 0 } })
#define BOOL_VAL(b)      ((Value){ VAL_BOOL,  { .boolean = (b) } })
#define INT_VAL(n)       ((Value){ VAL_INT,   { .i64 = (n) } })
#define FLOAT_VAL(n)     ((Value){ VAL_FLOAT, { .f64 = (n) } })
#define OBJ_VAL(o)       ((Value){ VAL_OBJ,   { .obj = (Obj *)(o) } })

#define IS_NIL(v)        ((v).type == VAL_NIL)
#define IS_BOOL(v)       ((v).type == VAL_BOOL)
#define IS_INT(v)        ((v).type == VAL_INT)
#define IS_FLOAT(v)      ((v).type == VAL_FLOAT)
#define IS_OBJ(v)        ((v).type == VAL_OBJ)
#define IS_NUM(v)        (IS_INT(v) || IS_FLOAT(v))

#define AS_BOOL(v)       ((v).as.boolean)
#define AS_INT(v)        ((v).as.i64)
#define AS_FLOAT(v)      ((v).as.f64)
#define AS_OBJ(v)        ((v).as.obj)

/* numeric coercion: an INT or FLOAT as a double */
static inline double value_as_double(Value v)
{
    return IS_INT(v) ? (double)v.as.i64 : v.as.f64;
}

bool  value_equal(Value a, Value b);
bool  value_truthy(Value v);

/* a growable Value array (constant pools, array objects, the value stack) */
typedef struct {
    Value  *values;
    size_t  count;
    size_t  cap;
} ValueArray;

void   valuearray_init(ValueArray *a);
int    valuearray_push(ValueArray *a, Value v);   /* returns index, or -1 */
void   valuearray_free(ValueArray *a);

#endif /* COIL_VALUE_H */
