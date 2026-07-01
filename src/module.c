/*
 * module.c -- bytecode module serialization and loading.
 */

#include "module.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>

#define MODULE_MAGIC0 'C'
#define MODULE_MAGIC1 'O'
#define MODULE_MAGIC2 'I'
#define MODULE_MAGIC3 'L'
#define MODULE_VERSION 1

enum { CT_NIL, CT_BOOL, CT_INT, CT_FLOAT, CT_STRING, CT_FUNC };

/* ================================================================== */
/* serialize                                                           */
/* ================================================================== */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    int      ok;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
    if (!b->ok) return;
    if (b->len + extra > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 256;
        while (ncap < b->len + extra) ncap *= 2;
        uint8_t *nd = (uint8_t *)realloc(b->data, ncap);
        if (!nd) { b->ok = 0; return; }
        b->data = nd;
        b->cap  = ncap;
    }
}

static void put_bytes(Buf *b, const void *p, size_t n)
{
    if (n == 0) return;                 /* nothing to copy (and p may be NULL) */
    buf_reserve(b, n);
    if (!b->ok) return;
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static void put_u8(Buf *b, uint8_t v)  { put_bytes(b, &v, 1); }
static void put_u16(Buf *b, uint16_t v){ uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put_bytes(b, t, 2); }
static void put_u32(Buf *b, uint32_t v){ uint8_t t[4]; for (int i = 0; i < 4; i++) t[i] = (uint8_t)(v >> (8 * i)); put_bytes(b, t, 4); }
static void put_u64(Buf *b, uint64_t v){ uint8_t t[8]; for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i)); put_bytes(b, t, 8); }

typedef struct {
    ObjFunction **items;
    size_t        count;
    size_t        cap;
} FnList;

static int fn_index(FnList *l, ObjFunction *f)
{
    for (size_t i = 0; i < l->count; i++)
        if (l->items[i] == f) return (int)i;
    return -1;
}

static int collect(FnList *l, ObjFunction *f)
{
    int idx = fn_index(l, f);
    if (idx >= 0) return idx;

    if (l->count == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 8;
        ObjFunction **ni = (ObjFunction **)realloc(l->items, ncap * sizeof(*ni));
        if (!ni) return -1;
        l->items = ni;
        l->cap   = ncap;
    }
    idx = (int)l->count;
    l->items[l->count++] = f;

    for (size_t i = 0; i < f->chunk.constants.count; i++) {
        Value c = f->chunk.constants.values[i];
        if (IS_FUNCTION(c) && collect(l, AS_FUNCTION(c)) < 0)
            return -1;
    }
    return idx;
}

static void put_const(Buf *b, Value v, FnList *l)
{
    if (IS_NIL(v))        { put_u8(b, CT_NIL); }
    else if (IS_BOOL(v))  { put_u8(b, CT_BOOL); put_u8(b, AS_BOOL(v) ? 1 : 0); }
    else if (IS_INT(v))   { put_u8(b, CT_INT); put_u64(b, (uint64_t)AS_INT(v)); }
    else if (IS_FLOAT(v)) { uint64_t bits; double d = AS_FLOAT(v); memcpy(&bits, &d, 8); put_u8(b, CT_FLOAT); put_u64(b, bits); }
    else if (IS_STRING(v)){ ObjString *s = AS_STRING(v); put_u8(b, CT_STRING); put_u32(b, s->length); put_bytes(b, s->chars, s->length); }
    else if (IS_FUNCTION(v)) { put_u8(b, CT_FUNC); put_u16(b, (uint16_t)fn_index(l, AS_FUNCTION(v))); }
    else { b->ok = 0; }   /* unserializable constant */
}

static void put_function(Buf *b, ObjFunction *f, FnList *l)
{
    put_u8(b, (uint8_t)f->arity);
    put_u8(b, (uint8_t)f->upvalue_count);
    put_u16(b, f->num_slots);
    if (f->name) {
        put_u8(b, 1);
        put_u32(b, f->name->length);
        put_bytes(b, f->name->chars, f->name->length);
    } else {
        put_u8(b, 0);
    }
    put_u32(b, (uint32_t)f->chunk.count);
    put_bytes(b, f->chunk.code, f->chunk.count);
    put_u16(b, (uint16_t)f->chunk.constants.count);
    for (size_t i = 0; i < f->chunk.constants.count; i++)
        put_const(b, f->chunk.constants.values[i], l);
}

int module_serialize(ObjFunction *entry, uint8_t **out, size_t *out_len)
{
    FnList l = {0};
    if (collect(&l, entry) < 0) { free(l.items); return MODULE_ERR_NOMEM; }
    if (l.count > 0xFFFF)       { free(l.items); return MODULE_ERR_FORMAT; }

    Buf b = { NULL, 0, 0, 1 };
    uint8_t magic[4] = { MODULE_MAGIC0, MODULE_MAGIC1, MODULE_MAGIC2, MODULE_MAGIC3 };
    put_bytes(&b, magic, 4);
    put_u16(&b, MODULE_VERSION);
    put_u16(&b, (uint16_t)l.count);
    for (size_t i = 0; i < l.count; i++)
        put_function(&b, l.items[i], &l);
    put_u16(&b, (uint16_t)fn_index(&l, entry));

    free(l.items);
    if (!b.ok) { free(b.data); return MODULE_ERR_NOMEM; }
    *out = b.data;
    *out_len = b.len;
    return MODULE_OK;
}

/* ================================================================== */
/* load (untrusted)                                                    */
/* ================================================================== */

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    int            ok;
} Reader;

static uint8_t rd_u8(Reader *r)
{
    if (r->p + 1 > r->end) { r->ok = 0; return 0; }
    return *r->p++;
}
static uint16_t rd_u16(Reader *r)
{
    if (r->p + 2 > r->end) { r->ok = 0; return 0; }
    uint16_t v = (uint16_t)(r->p[0] | (r->p[1] << 8));
    r->p += 2;
    return v;
}
static uint32_t rd_u32(Reader *r)
{
    if (r->p + 4 > r->end) { r->ok = 0; return 0; }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)r->p[i] << (8 * i);
    r->p += 4;
    return v;
}
static uint64_t rd_u64(Reader *r)
{
    if (r->p + 8 > r->end) { r->ok = 0; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)r->p[i] << (8 * i);
    r->p += 8;
    return v;
}
static const uint8_t *rd_bytes(Reader *r, size_t n)
{
    if (n > (size_t)(r->end - r->p)) { r->ok = 0; return NULL; }
    const uint8_t *p = r->p;
    r->p += n;
    return p;
}

static int load_const(Reader *r, Heap *h, ObjFunction **funcs, size_t fcount,
                      Value *out)
{
    uint8_t tag = rd_u8(r);
    if (!r->ok) return 0;
    switch (tag) {
    case CT_NIL:   *out = NIL_VAL; return 1;
    case CT_BOOL:  *out = BOOL_VAL(rd_u8(r) != 0); return r->ok;
    case CT_INT:   *out = INT_VAL((int64_t)rd_u64(r)); return r->ok;
    case CT_FLOAT: { uint64_t bits = rd_u64(r); double d; memcpy(&d, &bits, 8); *out = FLOAT_VAL(d); return r->ok; }
    case CT_STRING: {
        uint32_t len = rd_u32(r);
        const uint8_t *p = rd_bytes(r, len);
        if (!r->ok) return 0;
        *out = OBJ_VAL(string_copy(h, (const char *)p, len));
        return 1;
    }
    case CT_FUNC: {
        uint16_t idx = rd_u16(r);
        if (!r->ok || idx >= fcount) { r->ok = 0; return 0; }
        *out = OBJ_VAL(funcs[idx]);
        return 1;
    }
    default:
        r->ok = 0;
        return 0;
    }
}

static int load_function(Reader *r, Heap *h, ObjFunction *f,
                         ObjFunction **funcs, size_t fcount)
{
    f->arity         = rd_u8(r);
    f->upvalue_count = rd_u8(r);
    f->num_slots     = rd_u16(r);

    uint8_t has_name = rd_u8(r);
    if (!r->ok) return 0;
    if (has_name) {
        uint32_t len = rd_u32(r);
        const uint8_t *p = rd_bytes(r, len);
        if (!r->ok) return 0;
        f->name = string_copy(h, (const char *)p, len);
    }

    uint32_t code_len = rd_u32(r);
    const uint8_t *code = rd_bytes(r, code_len);
    if (!r->ok) return 0;
    for (uint32_t i = 0; i < code_len; i++)
        if (chunk_write(&f->chunk, code[i], 0) < 0) return 0;

    uint16_t const_count = rd_u16(r);
    if (!r->ok) return 0;
    for (uint16_t i = 0; i < const_count; i++) {
        Value c;
        if (!load_const(r, h, funcs, fcount, &c)) return 0;
        if (chunk_add_const(&f->chunk, c) < 0) return 0;
    }
    return 1;
}

int module_load(Heap *h, const uint8_t *data, size_t size, ObjFunction **entry)
{
    Reader r = { data, data + size, 1 };
    int result = MODULE_ERR_FORMAT;
    ObjFunction **funcs = NULL;
    size_t fcount = 0;

    const uint8_t *magic = rd_bytes(&r, 4);
    if (!r.ok || magic[0] != MODULE_MAGIC0 || magic[1] != MODULE_MAGIC1 ||
        magic[2] != MODULE_MAGIC2 || magic[3] != MODULE_MAGIC3)
        return MODULE_ERR_FORMAT;
    if (rd_u16(&r) != MODULE_VERSION)
        return MODULE_ERR_FORMAT;

    uint16_t function_count = rd_u16(&r);
    if (!r.ok || function_count == 0 || function_count > size)
        return MODULE_ERR_FORMAT;

    /* don't let collection during the load free our half-built functions */
    h->gc_paused = true;

    funcs = (ObjFunction **)calloc(function_count, sizeof(ObjFunction *));
    if (!funcs) { result = MODULE_ERR_NOMEM; goto done; }
    for (uint16_t i = 0; i < function_count; i++)
        funcs[i] = function_new(h);
    fcount = function_count;

    for (uint16_t i = 0; i < function_count; i++)
        if (!load_function(&r, h, funcs[i], funcs, fcount))
            goto done;

    uint16_t entry_index = rd_u16(&r);
    if (!r.ok || entry_index >= function_count)
        goto done;

    *entry = funcs[entry_index];
    result = MODULE_OK;

done:
    free(funcs);
    h->gc_paused = false;
    return result;
}
