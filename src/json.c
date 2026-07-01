/*
 * json.c -- JSON parser and serializer over Coil values.
 */

#include "json.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define JSON_MAX_DEPTH 200

/* ================================================================== */
/* parser                                                              */
typedef struct {
    const char *p;
    const char *end;
    Heap       *h;
    int         depth;
    int         ok;
} JParser;

static void jskip(JParser *j)
{
    while (j->p < j->end) {
        char c = *j->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->p++;
        else break;
    }
}

static int jpeek(JParser *j) { return j->p < j->end ? (unsigned char)*j->p : -1; }

static Value jvalue(JParser *j);

static int jhex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* parse a JSON string (j->p on the opening quote); returns an owned buffer */
static char *jstring_raw(JParser *j, size_t *out_len)
{
    j->p++;                                  /* opening quote */
    size_t cap = 16, len = 0;
    char  *buf = (char *)malloc(cap);
    if (!buf) { j->ok = 0; return NULL; }

    while (j->p < j->end && *j->p != '"') {
        char enc[4]; size_t enc_n;
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) { free(buf); j->ok = 0; return NULL; }
            char e = *j->p++;
            char one = 0;
            switch (e) {
            case '"': one = '"'; break;  case '\\': one = '\\'; break;
            case '/': one = '/'; break;  case 'b': one = '\b'; break;
            case 'f': one = '\f'; break; case 'n': one = '\n'; break;
            case 'r': one = '\r'; break; case 't': one = '\t'; break;
            case 'u': {
                if (j->end - j->p < 4) { free(buf); j->ok = 0; return NULL; }
                int h0 = jhex(j->p[0]), h1 = jhex(j->p[1]), h2 = jhex(j->p[2]), h3 = jhex(j->p[3]);
                if ((h0 | h1 | h2 | h3) < 0) { free(buf); j->ok = 0; return NULL; }
                int cp = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
                j->p += 4;
                if (cp < 0x80) { enc[0] = (char)cp; enc_n = 1; }
                else if (cp < 0x800) { enc[0] = (char)(0xC0 | (cp >> 6)); enc[1] = (char)(0x80 | (cp & 0x3F)); enc_n = 2; }
                else { enc[0] = (char)(0xE0 | (cp >> 12)); enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[2] = (char)(0x80 | (cp & 0x3F)); enc_n = 3; }
                goto append;
            }
            default: free(buf); j->ok = 0; return NULL;
            }
            enc[0] = one; enc_n = 1;
        } else {
            enc[0] = c; enc_n = 1;
        }
    append:
        if (len + enc_n > cap) {
            while (len + enc_n > cap) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); j->ok = 0; return NULL; }
            buf = nb;
        }
        memcpy(buf + len, enc, enc_n);
        len += enc_n;
    }
    if (j->p >= j->end) { free(buf); j->ok = 0; return NULL; }
    j->p++;                                  /* closing quote */
    *out_len = len;
    return buf;
}

static Value jstring(JParser *j)
{
    size_t len; char *raw = jstring_raw(j, &len);
    if (!j->ok) return NIL_VAL;
    Value v = OBJ_VAL(string_copy(j->h, raw, len));
    free(raw);
    return v;
}

static Value jnumber(JParser *j)
{
    const char *start = j->p;
    int is_real = 0;
    if (jpeek(j) == '-') j->p++;
    while (j->p < j->end) {
        char c = *j->p;
        if (c >= '0' && c <= '9') j->p++;
        else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            if (c == '.' || c == 'e' || c == 'E') is_real = 1;
            j->p++;
        } else break;
    }
    size_t n = (size_t)(j->p - start);
    if (n == 0 || n >= 64) { j->ok = 0; return NIL_VAL; }
    char tmp[64];
    memcpy(tmp, start, n); tmp[n] = '\0';
    if (is_real) return FLOAT_VAL(strtod(tmp, NULL));
    return INT_VAL((int64_t)strtoll(tmp, NULL, 10));
}

static Value jarray(JParser *j)
{
    if (++j->depth > JSON_MAX_DEPTH) { j->ok = 0; return NIL_VAL; }
    j->p++;                                  /* '[' */
    ObjArray *arr = array_new(j->h);
    jskip(j);
    if (jpeek(j) == ']') { j->p++; j->depth--; return OBJ_VAL(arr); }
    for (;;) {
        Value el = jvalue(j);
        if (!j->ok) return NIL_VAL;
        valuearray_push(&arr->items, el);
        jskip(j);
        int c = jpeek(j);
        if (c == ',') { j->p++; continue; }
        if (c == ']') { j->p++; break; }
        j->ok = 0; return NIL_VAL;
    }
    j->depth--;
    return OBJ_VAL(arr);
}

static Value jobject(JParser *j)
{
    if (++j->depth > JSON_MAX_DEPTH) { j->ok = 0; return NIL_VAL; }
    j->p++;                                  /* '{' */
    ObjTable *t = table_obj_new(j->h);
    jskip(j);
    if (jpeek(j) == '}') { j->p++; j->depth--; return OBJ_VAL(t); }
    for (;;) {
        jskip(j);
        if (jpeek(j) != '"') { j->ok = 0; return NIL_VAL; }
        size_t klen; char *kraw = jstring_raw(j, &klen);
        if (!j->ok) return NIL_VAL;
        Value key = OBJ_VAL(string_copy(j->h, kraw, klen));
        free(kraw);
        jskip(j);
        if (jpeek(j) != ':') { j->ok = 0; return NIL_VAL; }
        j->p++;
        Value val = jvalue(j);
        if (!j->ok) return NIL_VAL;
        if (vtable_set(t, key, val) != 0) { j->ok = 0; return NIL_VAL; }
        jskip(j);
        int c = jpeek(j);
        if (c == ',') { j->p++; continue; }
        if (c == '}') { j->p++; break; }
        j->ok = 0; return NIL_VAL;
    }
    j->depth--;
    return OBJ_VAL(t);
}

static Value jvalue(JParser *j)
{
    jskip(j);
    int c = jpeek(j);
    switch (c) {
    case '{': return jobject(j);
    case '[': return jarray(j);
    case '"': return jstring(j);
    case 't':
        if (j->end - j->p >= 4 && memcmp(j->p, "true", 4) == 0) { j->p += 4; return BOOL_VAL(true); }
        j->ok = 0; return NIL_VAL;
    case 'f':
        if (j->end - j->p >= 5 && memcmp(j->p, "false", 5) == 0) { j->p += 5; return BOOL_VAL(false); }
        j->ok = 0; return NIL_VAL;
    case 'n':
        if (j->end - j->p >= 4 && memcmp(j->p, "null", 4) == 0) { j->p += 4; return NIL_VAL; }
        j->ok = 0; return NIL_VAL;
    default:
        if (c == '-' || (c >= '0' && c <= '9')) return jnumber(j);
        j->ok = 0; return NIL_VAL;
    }
}

Value json_parse(Heap *h, const char *text, size_t len, bool *ok)
{
    JParser j = { text, text + len, h, 0, 1 };
    bool saved = h->gc_paused;
    h->gc_paused = true;                     /* in-progress values aren't rooted */

    Value v = jvalue(&j);
    jskip(&j);
    if (j.p != j.end) j.ok = 0;              /* trailing garbage */

    h->gc_paused = saved;
    *ok = j.ok != 0;
    return j.ok ? v : NIL_VAL;
}

/* ================================================================== */
/* serializer                                                          */
typedef struct { char *data; size_t len, cap; int ok; } SBuf;

static void sput(SBuf *b, const char *s, size_t n)
{
    if (!b->ok) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + n) nc *= 2;
        char *nd = (char *)realloc(b->data, nc);
        if (!nd) { b->ok = 0; return; }
        b->data = nd; b->cap = nc;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}
static void sputc(SBuf *b, char c) { sput(b, &c, 1); }
static void sputs(SBuf *b, const char *s) { sput(b, s, strlen(s)); }

static void sput_jstr(SBuf *b, const char *s, size_t n)
{
    sputc(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  sputs(b, "\\\""); break;
        case '\\': sputs(b, "\\\\"); break;
        case '\n': sputs(b, "\\n");  break;
        case '\r': sputs(b, "\\r");  break;
        case '\t': sputs(b, "\\t");  break;
        default:
            if (c < 0x20) { char e[8]; snprintf(e, sizeof(e), "\\u%04x", c); sputs(b, e); }
            else sputc(b, (char)c);
        }
    }
    sputc(b, '"');
}

static void sval(SBuf *b, Value v, int depth)
{
    if (depth > JSON_MAX_DEPTH) { b->ok = 0; return; }
    switch (v.type) {
    case VAL_NIL:   sputs(b, "null"); break;
    case VAL_BOOL:  sputs(b, v.as.boolean ? "true" : "false"); break;
    case VAL_INT:   { char t[24]; snprintf(t, sizeof(t), "%lld", (long long)v.as.i64); sputs(b, t); break; }
    case VAL_FLOAT: { char t[32]; snprintf(t, sizeof(t), "%g", v.as.f64); sputs(b, t); break; }
    case VAL_OBJ:
        switch (OBJ_TYPE(v)) {
        case OBJ_STRING: sput_jstr(b, AS_CSTRING(v), AS_STRING(v)->length); break;
        case OBJ_ARRAY: {
            ObjArray *a = AS_ARRAY(v);
            sputc(b, '[');
            for (size_t i = 0; i < a->items.count; i++) {
                if (i) sputc(b, ',');
                sval(b, a->items.values[i], depth + 1);
            }
            sputc(b, ']');
            break;
        }
        case OBJ_TABLE: {
            ObjTable *t = AS_TABLE(v);
            sputc(b, '{');
            int first = 1;
            for (size_t i = 0; i < t->cap; i++) {
                if (t->entries[i].state != 1) continue;
                Value k = t->entries[i].key;
                if (!IS_STRING(k)) continue;       /* JSON keys are strings */
                if (!first) sputc(b, ',');
                first = 0;
                sput_jstr(b, AS_CSTRING(k), AS_STRING(k)->length);
                sputc(b, ':');
                sval(b, t->entries[i].value, depth + 1);
            }
            sputc(b, '}');
            break;
        }
        default: sputs(b, "null"); break;          /* functions etc. */
        }
        break;
    }
}

ObjString *json_stringify(Heap *h, Value v)
{
    SBuf b = { NULL, 0, 0, 1 };
    bool saved = h->gc_paused;
    h->gc_paused = true;
    sval(&b, v, 0);
    ObjString *result = b.ok ? string_copy(h, b.data ? b.data : "", b.len) : NULL;
    h->gc_paused = saved;
    free(b.data);
    return result;
}
