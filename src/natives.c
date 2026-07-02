/*
 * natives.c -- the built-in standard library.
 *
 * Every native has the signature
 *     bool fn(Heap *h, int argc, Value *args, Value *result)
 * returning false (with the VM raising a runtime error) on a type error.
 * Arity is checked by the VM before the call, so argc always matches unless the
 * native was registered with arity -1 (variadic).
 */

#include "natives.h"
#include "object.h"
#include "json.h"
#include "stats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static ObjString *fmt(Heap *h, const char *f, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    if (n < 0) n = 0;
    return string_copy(h, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

static const char *type_name(Value v)
{
    switch (v.type) {
    case VAL_NIL:   return "nil";
    case VAL_BOOL:  return "bool";
    case VAL_INT:   return "int";
    case VAL_FLOAT: return "float";
    case VAL_OBJ:
        switch (OBJ_TYPE(v)) {
        case OBJ_STRING:   return "string";
        case OBJ_ARRAY:    return "array";
        case OBJ_TABLE:    return "table";
        case OBJ_FUNCTION:
        case OBJ_CLOSURE:
        case OBJ_NATIVE:   return "function";
        case OBJ_SLICE:    return "view";
        default:           return "object";
        }
    }
    return "?";
}

static ObjString *to_string(Heap *h, Value v)
{
    switch (v.type) {
    case VAL_NIL:   return string_copy(h, "nil", 3);
    case VAL_BOOL:  return v.as.boolean ? string_copy(h, "true", 4) : string_copy(h, "false", 5);
    case VAL_INT:   return fmt(h, "%lld", (long long)v.as.i64);
    case VAL_FLOAT: return fmt(h, "%g", v.as.f64);
    case VAL_OBJ:
        if (IS_STRING(v)) return AS_STRING(v);
        return fmt(h, "<%s>", type_name(v));
    }
    return string_copy(h, "?", 1);
}

/* ------------------------------------------------------------------ */
/* core                                                                */

static bool n_clock(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; (void)args;
    *result = FLOAT_VAL((double)clock() / CLOCKS_PER_SEC);
    return true;
}

static bool n_type(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    *result = OBJ_VAL(string_copy(h, type_name(args[0]), strlen(type_name(args[0]))));
    return true;
}

static bool n_str(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    *result = OBJ_VAL(to_string(h, args[0]));
    return true;
}

static bool n_num(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    Value v = args[0];
    if (IS_INT(v))   { *result = FLOAT_VAL((double)AS_INT(v)); return true; }
    if (IS_FLOAT(v)) { *result = v; return true; }
    if (IS_STRING(v)) { *result = FLOAT_VAL(strtod(AS_CSTRING(v), NULL)); return true; }
    return false;
}

static bool n_int(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    Value v = args[0];
    if (IS_INT(v))    { *result = v; return true; }
    if (IS_FLOAT(v))  { *result = INT_VAL((int64_t)AS_FLOAT(v)); return true; }
    if (IS_BOOL(v))   { *result = INT_VAL(AS_BOOL(v) ? 1 : 0); return true; }
    if (IS_STRING(v)) { *result = INT_VAL((int64_t)strtoll(AS_CSTRING(v), NULL, 10)); return true; }
    return false;
}

/* ------------------------------------------------------------------ */
/* math                                                                */

static bool num_arg(Value v, double *out)
{
    if (IS_INT(v))   { *out = (double)AS_INT(v); return true; }
    if (IS_FLOAT(v)) { *out = AS_FLOAT(v); return true; }
    return false;
}

static bool n_floor(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = INT_VAL((int64_t)floor(d));
    return true;
}
static bool n_ceil(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = INT_VAL((int64_t)ceil(d));
    return true;
}
static bool n_sqrt(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = FLOAT_VAL(sqrt(d));
    return true;
}
static bool n_abs(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (IS_INT(args[0]))   { int64_t x = AS_INT(args[0]); *result = INT_VAL(x < 0 ? (int64_t)(-(uint64_t)x) : x); return true; }
    if (IS_FLOAT(args[0])) { *result = FLOAT_VAL(fabs(AS_FLOAT(args[0]))); return true; }
    return false;
}
static bool n_pow(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double a, b;
    if (!num_arg(args[0], &a) || !num_arg(args[1], &b)) return false;
    *result = FLOAT_VAL(pow(a, b));
    return true;
}
static bool n_min(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double a, b;
    if (!num_arg(args[0], &a) || !num_arg(args[1], &b)) return false;
    *result = a < b ? args[0] : args[1];
    return true;
}
static bool n_max(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double a, b;
    if (!num_arg(args[0], &a) || !num_arg(args[1], &b)) return false;
    *result = a > b ? args[0] : args[1];
    return true;
}

/* ------------------------------------------------------------------ */
/* collections                                                         */

static bool n_len(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    Value v = args[0];
    if (IS_ARRAY(v))       *result = INT_VAL((int64_t)AS_ARRAY(v)->items.count);
    else if (IS_STRING(v)) *result = INT_VAL((int64_t)AS_STRING(v)->length);
    else if (IS_TABLE(v))  *result = INT_VAL((int64_t)AS_TABLE(v)->live);
    else if (IS_SLICE(v))  *result = INT_VAL((int64_t)AS_SLICE(v)->len);
    else return false;
    return true;
}

static bool n_push(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    if (valuearray_push(&AS_ARRAY(args[0])->items, args[1]) < 0) return false;
    *result = args[0];
    return true;
}

static bool n_pop(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    if (a->items.count == 0) return false;
    *result = a->items.values[--a->items.count];
    return true;
}

static bool n_keys(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_TABLE(args[0])) return false;
    ObjTable *t = AS_TABLE(args[0]);
    ObjArray *arr = array_new(h);
    for (size_t i = 0; i < t->cap; i++)
        if (t->entries[i].state == 1)
            valuearray_push(&arr->items, t->entries[i].key);
    *result = OBJ_VAL(arr);
    return true;
}

static bool n_values(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_TABLE(args[0])) return false;
    ObjTable *t = AS_TABLE(args[0]);
    ObjArray *arr = array_new(h);
    for (size_t i = 0; i < t->cap; i++)
        if (t->entries[i].state == 1)
            valuearray_push(&arr->items, t->entries[i].value);
    *result = OBJ_VAL(arr);
    return true;
}

static bool n_has(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_TABLE(args[0]) || !value_hashable(args[1])) return false;
    Value tmp;
    *result = BOOL_VAL(vtable_get(AS_TABLE(args[0]), args[1], &tmp));
    return true;
}

static bool n_del(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_TABLE(args[0]) || !value_hashable(args[1])) return false;
    *result = BOOL_VAL(vtable_delete(AS_TABLE(args[0]), args[1]));
    return true;
}

static bool n_range(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_INT(args[0])) return false;
    int64_t n = AS_INT(args[0]);
    if (n < 0 || n > 100000000) return false;
    ObjArray *arr = array_new(h);
    for (int64_t i = 0; i < n; i++)
        valuearray_push(&arr->items, INT_VAL(i));
    *result = OBJ_VAL(arr);
    return true;
}

/* view(array, start, len) -> a zero-copy window over array[start .. start+len).
 * Unlike slice() (which copies), a view reads through to the parent array, so
 * it stays valid only while the parent does. */
static bool n_view(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return false;
    ObjArray *arr   = AS_ARRAY(args[0]);
    int64_t   start = AS_INT(args[1]);
    int64_t   len   = AS_INT(args[2]);
    if (start < 0 || len < 0) return false;
    /* compare without overflow: start <= count and len <= count - start */
    if ((uint64_t)start > arr->items.count) return false;
    if ((uint64_t)len > arr->items.count - (uint64_t)start) return false;
    *result = OBJ_VAL(slice_new(h, arr, (size_t)start, (size_t)len));
    return true;
}

/* ------------------------------------------------------------------ */
/* strings                                                             */

static bool n_ord(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_STRING(args[0]) || AS_STRING(args[0])->length == 0) return false;
    *result = INT_VAL((unsigned char)AS_CSTRING(args[0])[0]);
    return true;
}

static bool n_chr(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_INT(args[0])) return false;
    char c = (char)(AS_INT(args[0]) & 0xff);
    *result = OBJ_VAL(string_copy(h, &c, 1));
    return true;
}

static bool n_upper(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0])) return false;
    ObjString *s = AS_STRING(args[0]);
    char *buf = (char *)malloc(s->length + 1);
    if (!buf) return false;
    for (uint32_t i = 0; i < s->length; i++) buf[i] = (char)toupper((unsigned char)s->chars[i]);
    *result = OBJ_VAL(string_copy(h, buf, s->length));
    free(buf);
    return true;
}

static bool n_lower(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0])) return false;
    ObjString *s = AS_STRING(args[0]);
    char *buf = (char *)malloc(s->length + 1);
    if (!buf) return false;
    for (uint32_t i = 0; i < s->length; i++) buf[i] = (char)tolower((unsigned char)s->chars[i]);
    *result = OBJ_VAL(string_copy(h, buf, s->length));
    free(buf);
    return true;
}

static bool n_substr(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return false;
    ObjString *s = AS_STRING(args[0]);
    int64_t start = AS_INT(args[1]);
    int64_t len   = AS_INT(args[2]);
    if (start < 0) start = 0;
    if (start > (int64_t)s->length) start = s->length;
    if (len < 0) len = 0;
    if (start + len > (int64_t)s->length) len = (int64_t)s->length - start;
    *result = OBJ_VAL(string_copy(h, s->chars + start, (size_t)len));
    return true;
}

static bool n_find(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return false;
    ObjString *s = AS_STRING(args[0]);
    ObjString *sub = AS_STRING(args[1]);
    if (sub->length == 0) { *result = INT_VAL(0); return true; }
    if (sub->length > s->length) { *result = INT_VAL(-1); return true; }
    for (size_t i = 0; i + sub->length <= s->length; i++) {
        if (memcmp(s->chars + i, sub->chars, sub->length) == 0) {
            *result = INT_VAL((int64_t)i);
            return true;
        }
    }
    *result = INT_VAL(-1);
    return true;
}

static bool n_repeat(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0]) || !IS_INT(args[1])) return false;
    ObjString *s = AS_STRING(args[0]);
    int64_t n = AS_INT(args[1]);
    if (n < 0) n = 0;
    size_t total = (size_t)n * s->length;
    if (total > (64u << 20)) return false;            /* guard absurd sizes */
    char *buf = (char *)malloc(total + 1);
    if (!buf) return false;
    for (int64_t i = 0; i < n; i++)
        memcpy(buf + (size_t)i * s->length, s->chars, s->length);
    *result = OBJ_VAL(string_copy(h, buf, total));
    free(buf);
    return true;
}

static bool n_assert(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!value_truthy(args[0])) return false;          /* raises a runtime error */
    *result = args[0];
    return true;
}

/* ------------------------------------------------------------------ */
/* more arrays & strings                                               */

static bool n_reverse(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    if (a->items.count > 1)
        for (size_t i = 0, j = a->items.count - 1; i < j; i++, j--) {
            Value t = a->items.values[i];
            a->items.values[i] = a->items.values[j];
            a->items.values[j] = t;
        }
    *result = args[0];
    return true;
}

static int cmp_values(const void *pa, const void *pb)
{
    Value a = *(const Value *)pa, b = *(const Value *)pb;
    if (IS_NUM(a) && IS_NUM(b)) {
        double x = value_as_double(a), y = value_as_double(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
        size_t n = sa->length < sb->length ? sa->length : sb->length;
        int c = memcmp(sa->chars, sb->chars, n);
        if (c) return c;
        return (sa->length > sb->length) - (sa->length < sb->length);
    }
    return (int)a.type - (int)b.type;
}

static bool n_sort(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    if (a->items.count > 1)
        qsort(a->items.values, a->items.count, sizeof(Value), cmp_values);
    *result = args[0];
    return true;
}

static bool n_slice(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    int64_t n = (int64_t)a->items.count;
    int64_t s = AS_INT(args[1]), e = AS_INT(args[2]);
    if (s < 0) s = 0;
    if (e > n) e = n;
    if (s > e) s = e;
    ObjArray *out = array_new(h);          /* only this allocates; loop is alloc-free */
    for (int64_t i = s; i < e; i++)
        valuearray_push(&out->items, a->items.values[i]);
    *result = OBJ_VAL(out);
    return true;
}

static bool n_split(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return false;
    ObjString *s = AS_STRING(args[0]), *sep = AS_STRING(args[1]);
    bool saved = h->gc_paused;
    h->gc_paused = true;                    /* allocate strings while building */
    ObjArray *out = array_new(h);
    if (sep->length == 0) {
        for (uint32_t i = 0; i < s->length; i++)
            valuearray_push(&out->items, OBJ_VAL(string_copy(h, s->chars + i, 1)));
    } else {
        size_t start = 0, i = 0;
        while (i + sep->length <= s->length) {
            if (memcmp(s->chars + i, sep->chars, sep->length) == 0) {
                valuearray_push(&out->items, OBJ_VAL(string_copy(h, s->chars + start, i - start)));
                i += sep->length;
                start = i;
            } else i++;
        }
        valuearray_push(&out->items, OBJ_VAL(string_copy(h, s->chars + start, s->length - start)));
    }
    h->gc_paused = saved;
    *result = OBJ_VAL(out);
    return true;
}

static bool n_join(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_STRING(args[1])) return false;
    ObjArray  *a = AS_ARRAY(args[0]);
    ObjString *sep = AS_STRING(args[1]);
    bool saved = h->gc_paused;
    h->gc_paused = true;

    size_t total = 0;
    for (size_t i = 0; i < a->items.count; i++) {
        if (i) total += sep->length;
        total += to_string(h, a->items.values[i])->length;
        if (total > (64u << 20)) { h->gc_paused = saved; return false; }
    }
    char *buf = (char *)malloc(total + 1);
    if (!buf) { h->gc_paused = saved; return false; }
    size_t off = 0;
    for (size_t i = 0; i < a->items.count; i++) {
        if (i) { memcpy(buf + off, sep->chars, sep->length); off += sep->length; }
        ObjString *es = to_string(h, a->items.values[i]);
        memcpy(buf + off, es->chars, es->length);
        off += es->length;
    }
    ObjString *res = string_copy(h, buf, total);
    free(buf);
    h->gc_paused = saved;
    *result = OBJ_VAL(res);
    return true;
}

/* Build a printable label for a value.  It allocates a scratch string, so it
 * guards that allocation against a collection the way split()/join() do.
 * Several of the reporting/debug natives call this while the collector is
 * already running, so it re-enables collection on the way out. */
static ObjString *cell_label(Heap *h, Value v)
{
    /* to_string may allocate a scratch string; pause the collector across that
     * allocation so the fresh string can't be reclaimed before we hand it back,
     * then restore the caller's collection state. */
    bool saved = h->gc_paused;
    h->gc_paused = true;
    ObjString *s = to_string(h, v);
    h->gc_paused = saved;
    return s;
}

/* tabulate(array) -> array of [index, label] pairs.
 *   tabulate(["a", "b"]) == [[0, "a"], [1, "b"]]
 * Enumerates an array, pairing each element with its position and a printable
 * label.  Allocates strings and sub-arrays while building, so it guards the
 * whole build against a collection. */
static bool n_tabulate(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *src = AS_ARRAY(args[0]);

    bool saved = h->gc_paused;
    h->gc_paused = true;                       /* guard the whole build */
    ObjArray *out = array_new(h);              /* held only in this C local */
    for (size_t i = 0; i < src->items.count; i++) {
        ObjString *label = cell_label(h, src->items.values[i]);
        ObjArray *pair = array_new(h);         /* may collect here */
        valuearray_push(&pair->items, INT_VAL((int64_t)i));
        valuearray_push(&pair->items, OBJ_VAL(label));
        valuearray_push(&out->items, OBJ_VAL(pair));
    }
    h->gc_paused = saved;
    *result = OBJ_VAL(out);
    return true;
}

static bool n_contains(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return false;
    ObjString *s = AS_STRING(args[0]), *sub = AS_STRING(args[1]);
    if (sub->length == 0) { *result = BOOL_VAL(true); return true; }
    if (sub->length > s->length) { *result = BOOL_VAL(false); return true; }
    for (size_t i = 0; i + sub->length <= s->length; i++)
        if (memcmp(s->chars + i, sub->chars, sub->length) == 0) { *result = BOOL_VAL(true); return true; }
    *result = BOOL_VAL(false);
    return true;
}

static bool n_replace(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) return false;
    ObjString *s = AS_STRING(args[0]), *from = AS_STRING(args[1]), *to = AS_STRING(args[2]);
    if (from->length == 0) { *result = args[0]; return true; }

    size_t cap = s->length + 1, len = 0;
    char  *buf = (char *)malloc(cap);
    if (!buf) return false;
    size_t i = 0;
    while (i < s->length) {
        if (i + from->length <= s->length && memcmp(s->chars + i, from->chars, from->length) == 0) {
            if (len + to->length + 1 > cap) { while (len + to->length + 1 > cap) cap *= 2; char *nb = realloc(buf, cap); if (!nb) { free(buf); return false; } buf = nb; }
            memcpy(buf + len, to->chars, to->length); len += to->length;
            i += from->length;
        } else {
            if (len + 1 > cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) { free(buf); return false; } buf = nb; }
            buf[len++] = s->chars[i++];
        }
    }
    ObjString *res = string_copy(h, buf, len);
    free(buf);
    *result = OBJ_VAL(res);
    return true;
}

/* more math */
static bool math1(Value *args, Value *result, double (*f)(double))
{
    double d;
    if (!num_arg(args[0], &d)) return false;
    *result = FLOAT_VAL(f(d));
    return true;
}
static bool n_sin(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, sin); }
static bool n_cos(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, cos); }
static bool n_tan(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, tan); }
static bool n_exp(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, exp); }
static bool n_log(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, log); }
static bool n_round(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = INT_VAL((int64_t)(d < 0 ? d - 0.5 : d + 0.5));
    return true;
}

static bool n_json_parse(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0])) return false;
    bool ok;
    Value v = json_parse(h, AS_CSTRING(args[0]), AS_STRING(args[0])->length, &ok);
    if (!ok) return false;
    *result = v;
    return true;
}

static bool n_json_str(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    ObjString *s = json_stringify(h, args[0]);
    if (!s) return false;
    *result = OBJ_VAL(s);
    return true;
}

/* ------------------------------------------------------------------ */
/* extended math                                                       */

#define COIL_PI 3.14159265358979323846
#define COIL_E  2.71828182845904523536

static bool n_sign(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = INT_VAL(d < 0 ? -1 : (d > 0 ? 1 : 0));
    return true;
}
static bool n_clamp(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double x, lo, hi;
    if (!num_arg(args[0], &x) || !num_arg(args[1], &lo) || !num_arg(args[2], &hi)) return false;
    *result = x < lo ? args[1] : (x > hi ? args[2] : args[0]);
    return true;
}
static bool n_gcd(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_INT(args[0]) || !IS_INT(args[1])) return false;
    int64_t a = AS_INT(args[0]), b = AS_INT(args[1]);
    uint64_t x = a < 0 ? -(uint64_t)a : (uint64_t)a;      /* magnitudes: no UB */
    uint64_t y = b < 0 ? -(uint64_t)b : (uint64_t)b;
    while (y) { uint64_t t = x % y; x = y; y = t; }
    *result = INT_VAL((int64_t)x);
    return true;
}
static bool n_hypot(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double a, b;
    if (!num_arg(args[0], &a) || !num_arg(args[1], &b)) return false;
    *result = FLOAT_VAL(hypot(a, b));
    return true;
}
static bool n_log2(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, log2); }
static bool n_log10(Heap *h, int argc, Value *args, Value *result) { (void)h; (void)argc; return math1(args, result, log10); }
static bool n_cbrt(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, cbrt); }
static bool n_atan(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, atan); }
static bool n_asin(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, asin); }
static bool n_acos(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; return math1(args, result, acos); }
static bool n_deg(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = FLOAT_VAL(d * 180.0 / COIL_PI);
    return true;
}
static bool n_rad(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc; double d;
    if (!num_arg(args[0], &d)) return false;
    *result = FLOAT_VAL(d * COIL_PI / 180.0);
    return true;
}
static bool n_pi(Heap *h, int argc, Value *args, Value *result) { (void)h; (void)argc; (void)args; *result = FLOAT_VAL(COIL_PI); return true; }
static bool n_e(Heap *h, int argc, Value *args, Value *result)  { (void)h; (void)argc; (void)args; *result = FLOAT_VAL(COIL_E);  return true; }
static bool n_is_nan(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    *result = BOOL_VAL(IS_FLOAT(args[0]) && isnan(AS_FLOAT(args[0])));
    return true;
}

/* ------------------------------------------------------------------ */
/* array aggregates & mutation                                         */

static bool n_sum(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    int all_int = 1; int64_t isum = 0; double dsum = 0;
    for (size_t i = 0; i < a->items.count; i++) {
        Value v = a->items.values[i];
        if (IS_INT(v))        { isum = (int64_t)((uint64_t)isum + (uint64_t)AS_INT(v)); dsum += (double)AS_INT(v); }
        else if (IS_FLOAT(v)) { all_int = 0; dsum += AS_FLOAT(v); }
        else return false;
    }
    *result = all_int ? INT_VAL(isum) : FLOAT_VAL(dsum);
    return true;
}
static bool n_avg(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    if (a->items.count == 0) return false;
    double dsum = 0;
    for (size_t i = 0; i < a->items.count; i++) {
        double d;
        if (!num_arg(a->items.values[i], &d)) return false;
        dsum += d;
    }
    *result = FLOAT_VAL(dsum / (double)a->items.count);
    return true;
}
static bool n_extreme(Value arr, Value *result, int want_max)
{
    if (!IS_ARRAY(arr)) return false;
    ObjArray *a = AS_ARRAY(arr);
    if (a->items.count == 0) return false;
    double best; if (!num_arg(a->items.values[0], &best)) return false;
    Value bestv = a->items.values[0];
    for (size_t i = 1; i < a->items.count; i++) {
        double d;
        if (!num_arg(a->items.values[i], &d)) return false;
        if ((want_max && d > best) || (!want_max && d < best)) { best = d; bestv = a->items.values[i]; }
    }
    *result = bestv;
    return true;
}
static bool n_max_of(Heap *h, int argc, Value *args, Value *result) { (void)h; (void)argc; return n_extreme(args[0], result, 1); }
static bool n_min_of(Heap *h, int argc, Value *args, Value *result) { (void)h; (void)argc; return n_extreme(args[0], result, 0); }

static bool n_index_of(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    for (size_t i = 0; i < a->items.count; i++)
        if (value_equal(a->items.values[i], args[1])) { *result = INT_VAL((int64_t)i); return true; }
    *result = INT_VAL(-1);
    return true;
}
static bool n_count(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    int64_t c = 0;
    for (size_t i = 0; i < a->items.count; i++)
        if (value_equal(a->items.values[i], args[1])) c++;
    *result = INT_VAL(c);
    return true;
}
static bool n_insert(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_INT(args[1])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    int64_t idx = AS_INT(args[1]);
    if (idx < 0 || idx > (int64_t)a->items.count) return false;
    if (valuearray_push(&a->items, args[2]) < 0) return false;    /* grow by one */
    for (size_t k = a->items.count - 1; k > (size_t)idx; k--)
        a->items.values[k] = a->items.values[k - 1];
    a->items.values[idx] = args[2];
    *result = args[0];
    return true;
}
static bool n_remove_at(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_INT(args[1])) return false;
    ObjArray *a = AS_ARRAY(args[0]);
    int64_t idx = AS_INT(args[1]);
    if (idx < 0 || idx >= (int64_t)a->items.count) return false;
    Value removed = a->items.values[idx];
    for (size_t k = (size_t)idx; k + 1 < a->items.count; k++)
        a->items.values[k] = a->items.values[k + 1];
    a->items.count--;
    *result = removed;
    return true;
}
static bool n_concat(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) return false;
    ObjArray *x = AS_ARRAY(args[0]), *y = AS_ARRAY(args[1]);
    ObjArray *out = array_new(h);          /* only object allocation; args root */
    for (size_t i = 0; i < x->items.count; i++) valuearray_push(&out->items, x->items.values[i]);
    for (size_t i = 0; i < y->items.count; i++) valuearray_push(&out->items, y->items.values[i]);
    *result = OBJ_VAL(out);
    return true;
}
static bool n_fill(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_INT(args[0])) return false;
    int64_t n = AS_INT(args[0]);
    if (n < 0 || n > 100000000) return false;
    ObjArray *out = array_new(h);
    for (int64_t i = 0; i < n; i++) valuearray_push(&out->items, args[1]);
    *result = OBJ_VAL(out);
    return true;
}

/* ------------------------------------------------------------------ */
/* extended strings                                                    */

static bool n_trim(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0])) return false;
    ObjString *s = AS_STRING(args[0]);
    size_t a = 0, b = s->length;
    while (a < b && (unsigned char)s->chars[a] <= ' ') a++;
    while (b > a && (unsigned char)s->chars[b - 1] <= ' ') b--;
    *result = OBJ_VAL(string_copy(h, s->chars + a, b - a));
    return true;
}
static bool n_starts_with(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return false;
    ObjString *s = AS_STRING(args[0]), *p = AS_STRING(args[1]);
    *result = BOOL_VAL(p->length <= s->length && memcmp(s->chars, p->chars, p->length) == 0);
    return true;
}
static bool n_ends_with(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return false;
    ObjString *s = AS_STRING(args[0]), *p = AS_STRING(args[1]);
    *result = BOOL_VAL(p->length <= s->length &&
                       memcmp(s->chars + s->length - p->length, p->chars, p->length) == 0);
    return true;
}
static bool n_pad(Heap *h, Value *args, Value *result, int left)
{
    if (!IS_STRING(args[0]) || !IS_INT(args[1]) || !IS_STRING(args[2])) return false;
    ObjString *s = AS_STRING(args[0]), *pads = AS_STRING(args[2]);
    int64_t width = AS_INT(args[1]);
    char pad = pads->length ? pads->chars[0] : ' ';
    if (width < 0 || width > (1 << 20)) return false;
    if ((int64_t)s->length >= width) { *result = args[0]; return true; }
    size_t total = (size_t)width, npad = total - s->length;
    char *buf = (char *)malloc(total + 1);
    if (!buf) return false;
    if (left) { memset(buf, pad, npad); memcpy(buf + npad, s->chars, s->length); }
    else      { memcpy(buf, s->chars, s->length); memset(buf + s->length, pad, npad); }
    *result = OBJ_VAL(string_copy(h, buf, total));
    free(buf);
    return true;
}
static bool n_pad_left(Heap *h, int argc, Value *args, Value *result)  { (void)argc; return n_pad(h, args, result, 1); }
static bool n_pad_right(Heap *h, int argc, Value *args, Value *result) { (void)argc; return n_pad(h, args, result, 0); }

static bool n_char_at(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_STRING(args[0]) || !IS_INT(args[1])) return false;
    ObjString *s = AS_STRING(args[0]);
    int64_t i = AS_INT(args[1]);
    if (i < 0 || i >= (int64_t)s->length) return false;
    *result = OBJ_VAL(string_copy(h, s->chars + i, 1));
    return true;
}
static bool n_hex(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    if (!IS_INT(args[0])) return false;
    int64_t v = AS_INT(args[0]);
    char buf[32];
    int n;
    if (v < 0) n = snprintf(buf, sizeof buf, "-%llx", (unsigned long long)(-(uint64_t)v));
    else       n = snprintf(buf, sizeof buf, "%llx", (unsigned long long)v);
    if (n < 0) n = 0;
    *result = OBJ_VAL(string_copy(h, buf, (size_t)n));
    return true;
}

/* ------------------------------------------------------------------ */
/* statistics -- thin wrappers over stats.{h,c} on numeric arrays           */

/* Copy a non-empty numeric array into a fresh double buffer (caller frees).
 * Returns NULL if the value is not an array, is empty, or holds a non-number. */
static double *nums_of(Value v, size_t *n_out)
{
    if (!IS_ARRAY(v)) return NULL;
    ObjArray *a = AS_ARRAY(v);
    size_t n = a->items.count;
    if (n == 0) return NULL;
    double *xs = (double *)malloc(n * sizeof(double));
    if (!xs) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (!IS_NUM(a->items.values[i])) { free(xs); return NULL; }
        xs[i] = value_as_double(a->items.values[i]);
    }
    *n_out = n;
    return xs;
}

/* build a Coil array from a double buffer.  No object is allocated inside the
 * loop (floats are inline values), so the fresh array cannot be collected mid-
 * build and needs no extra rooting. */
static Value array_of_doubles(Heap *h, const double *xs, size_t n)
{
    ObjArray *arr = array_new(h);
    for (size_t i = 0; i < n; i++)
        valuearray_push(&arr->items, FLOAT_VAL(xs[i]));
    return OBJ_VAL(arr);
}

#define STAT_SCALAR(name, call)                                             \
    static bool name(Heap *h, int argc, Value *args, Value *result)         \
    {                                                                       \
        (void)h; (void)argc;                                                \
        size_t n; double *xs = nums_of(args[0], &n);                        \
        if (!xs) return false;                                              \
        double r = (call);                                                  \
        free(xs);                                                           \
        *result = FLOAT_VAL(r);                                             \
        return true;                                                        \
    }

STAT_SCALAR(n_mean,     stats_mean(xs, n))
STAT_SCALAR(n_median,   stats_median(xs, n))
STAT_SCALAR(n_mode,     stats_mode(xs, n))
STAT_SCALAR(n_pvariance, stats_variance(xs, n, false))
STAT_SCALAR(n_pstdev,   stats_stddev(xs, n, false))
STAT_SCALAR(n_skew,     stats_skewness(xs, n))
STAT_SCALAR(n_product,  stats_product(xs, n))

/* sample variance/stddev need at least two points */
static bool n_variance(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    size_t n; double *xs = nums_of(args[0], &n);
    if (!xs) return false;
    if (n < 2) { free(xs); return false; }
    *result = FLOAT_VAL(stats_variance(xs, n, true));
    free(xs);
    return true;
}

static bool n_stdev(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    size_t n; double *xs = nums_of(args[0], &n);
    if (!xs) return false;
    if (n < 2) { free(xs); return false; }
    *result = FLOAT_VAL(stats_stddev(xs, n, true));
    free(xs);
    return true;
}

static bool n_percentile(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    if (!IS_NUM(args[1])) return false;
    size_t n; double *xs = nums_of(args[0], &n);
    if (!xs) return false;
    *result = FLOAT_VAL(stats_percentile(xs, n, value_as_double(args[1])));
    free(xs);
    return true;
}

/* two equal-length numeric arrays -> one scalar */
static double *pair_of(Value a, Value b, size_t *n_out)
{
    size_t na, nb;
    double *xs = nums_of(a, &na);
    if (!xs) return NULL;
    double *ys = nums_of(b, &nb);
    if (!ys || na != nb) { free(xs); free(ys); return NULL; }
    /* pack xs then ys into one buffer so a single free suffices */
    double *both = (double *)malloc(2 * na * sizeof(double));
    if (!both) { free(xs); free(ys); return NULL; }
    for (size_t i = 0; i < na; i++) { both[i] = xs[i]; both[na + i] = ys[i]; }
    free(xs); free(ys);
    *n_out = na;
    return both;
}

static bool n_correlation(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    size_t n; double *both = pair_of(args[0], args[1], &n);
    if (!both || n < 2) { free(both); return false; }
    *result = FLOAT_VAL(stats_correlation(both, both + n, n));
    free(both);
    return true;
}

static bool n_covariance(Heap *h, int argc, Value *args, Value *result)
{
    (void)h; (void)argc;
    size_t n; double *both = pair_of(args[0], args[1], &n);
    if (!both) return false;
    *result = FLOAT_VAL(stats_covariance(both, both + n, n));
    free(both);
    return true;
}

static bool n_linfit(Heap *h, int argc, Value *args, Value *result)
{
    (void)argc;
    size_t n; double *both = pair_of(args[0], args[1], &n);
    if (!both || n < 2) { free(both); return false; }
    double slope, intercept;
    stats_linfit(both, both + n, n, &slope, &intercept);
    free(both);
    ObjArray *arr = array_new(h);
    valuearray_push(&arr->items, FLOAT_VAL(slope));
    valuearray_push(&arr->items, FLOAT_VAL(intercept));
    *result = OBJ_VAL(arr);
    return true;
}

/* numeric array -> transformed numeric array */
#define STAT_MAP(name, fn)                                                  \
    static bool name(Heap *h, int argc, Value *args, Value *result)         \
    {                                                                       \
        (void)argc;                                                         \
        size_t n; double *xs = nums_of(args[0], &n);                        \
        if (!xs) return false;                                              \
        double *out = (double *)malloc(n * sizeof(double));                 \
        if (!out) { free(xs); return false; }                               \
        fn(xs, n, out);                                                     \
        *result = array_of_doubles(h, out, n);                             \
        free(xs); free(out);                                                \
        return true;                                                        \
    }

STAT_MAP(n_cumsum,    stats_cumsum)
STAT_MAP(n_zscore,    stats_zscore)
STAT_MAP(n_normalize, stats_normalize)

/* ------------------------------------------------------------------ */
void register_natives(VM *vm)
{
    vm_define_native(vm, "clock",  n_clock,  0);
    vm_define_native(vm, "type",   n_type,   1);
    vm_define_native(vm, "str",    n_str,    1);
    vm_define_native(vm, "num",    n_num,    1);
    vm_define_native(vm, "int",    n_int,    1);
    vm_define_native(vm, "floor",  n_floor,  1);
    vm_define_native(vm, "ceil",   n_ceil,   1);
    vm_define_native(vm, "sqrt",   n_sqrt,   1);
    vm_define_native(vm, "abs",    n_abs,    1);
    vm_define_native(vm, "pow",    n_pow,    2);
    vm_define_native(vm, "min",    n_min,    2);
    vm_define_native(vm, "max",    n_max,    2);
    vm_define_native(vm, "len",    n_len,    1);
    vm_define_native(vm, "push",   n_push,   2);
    vm_define_native(vm, "pop",    n_pop,    1);
    vm_define_native(vm, "keys",   n_keys,   1);
    vm_define_native(vm, "values", n_values, 1);
    vm_define_native(vm, "has",    n_has,    2);
    vm_define_native(vm, "del",    n_del,    2);
    vm_define_native(vm, "range",  n_range,  1);
    vm_define_native(vm, "view",   n_view,   3);
    vm_define_native(vm, "ord",    n_ord,    1);
    vm_define_native(vm, "chr",    n_chr,    1);
    vm_define_native(vm, "upper",  n_upper,  1);
    vm_define_native(vm, "lower",  n_lower,  1);
    vm_define_native(vm, "substr", n_substr, 3);
    vm_define_native(vm, "find",   n_find,   2);
    vm_define_native(vm, "repeat", n_repeat, 2);
    vm_define_native(vm, "assert", n_assert, 1);
    vm_define_native(vm, "json_parse", n_json_parse, 1);
    vm_define_native(vm, "json_str",   n_json_str,   1);
    vm_define_native(vm, "reverse",  n_reverse,  1);
    vm_define_native(vm, "sort",     n_sort,     1);
    vm_define_native(vm, "slice",    n_slice,    3);
    vm_define_native(vm, "split",    n_split,    2);
    vm_define_native(vm, "join",     n_join,     2);
    vm_define_native(vm, "contains", n_contains, 2);
    vm_define_native(vm, "replace",  n_replace,  3);
    vm_define_native(vm, "sin",      n_sin,      1);
    vm_define_native(vm, "cos",      n_cos,      1);
    vm_define_native(vm, "tan",      n_tan,      1);
    vm_define_native(vm, "exp",      n_exp,      1);
    vm_define_native(vm, "log",      n_log,      1);
    vm_define_native(vm, "round",    n_round,    1);

    /* extended math */
    vm_define_native(vm, "sign",   n_sign,   1);
    vm_define_native(vm, "clamp",  n_clamp,  3);
    vm_define_native(vm, "gcd",    n_gcd,    2);
    vm_define_native(vm, "hypot",  n_hypot,  2);
    vm_define_native(vm, "log2",   n_log2,   1);
    vm_define_native(vm, "log10",  n_log10,  1);
    vm_define_native(vm, "cbrt",   n_cbrt,   1);
    vm_define_native(vm, "atan",   n_atan,   1);
    vm_define_native(vm, "asin",   n_asin,   1);
    vm_define_native(vm, "acos",   n_acos,   1);
    vm_define_native(vm, "deg",    n_deg,    1);
    vm_define_native(vm, "rad",    n_rad,    1);
    vm_define_native(vm, "pi",     n_pi,     0);
    vm_define_native(vm, "e",      n_e,      0);
    vm_define_native(vm, "is_nan", n_is_nan, 1);

    /* array aggregates & mutation */
    vm_define_native(vm, "sum",       n_sum,       1);
    vm_define_native(vm, "avg",       n_avg,       1);
    vm_define_native(vm, "max_of",    n_max_of,    1);
    vm_define_native(vm, "min_of",    n_min_of,    1);
    vm_define_native(vm, "index_of",  n_index_of,  2);
    vm_define_native(vm, "count",     n_count,     2);
    vm_define_native(vm, "insert",    n_insert,    3);
    vm_define_native(vm, "remove_at", n_remove_at, 2);
    vm_define_native(vm, "concat",    n_concat,    2);
    vm_define_native(vm, "fill",      n_fill,      2);

    /* extended strings */
    vm_define_native(vm, "trim",        n_trim,        1);
    vm_define_native(vm, "starts_with", n_starts_with, 2);
    vm_define_native(vm, "ends_with",   n_ends_with,   2);
    vm_define_native(vm, "pad_left",    n_pad_left,    3);
    vm_define_native(vm, "pad_right",   n_pad_right,   3);
    vm_define_native(vm, "char_at",     n_char_at,     2);
    vm_define_native(vm, "hex",         n_hex,         1);
    vm_define_native(vm, "tabulate",    n_tabulate,    1);

    /* statistics */
    vm_define_native(vm, "mean",        n_mean,        1);
    vm_define_native(vm, "median",      n_median,      1);
    vm_define_native(vm, "mode",        n_mode,        1);
    vm_define_native(vm, "variance",    n_variance,    1);
    vm_define_native(vm, "pvariance",   n_pvariance,   1);
    vm_define_native(vm, "stdev",       n_stdev,       1);
    vm_define_native(vm, "pstdev",      n_pstdev,      1);
    vm_define_native(vm, "skew",        n_skew,        1);
    vm_define_native(vm, "product",     n_product,     1);
    vm_define_native(vm, "percentile",  n_percentile,  2);
    vm_define_native(vm, "correlation", n_correlation, 2);
    vm_define_native(vm, "covariance",  n_covariance,  2);
    vm_define_native(vm, "linfit",      n_linfit,      2);
    vm_define_native(vm, "cumsum",      n_cumsum,      1);
    vm_define_native(vm, "zscore",      n_zscore,      1);
    vm_define_native(vm, "normalize",   n_normalize,   1);
}
