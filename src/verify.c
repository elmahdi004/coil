/*
 * verify.c -- bytecode verifier (see verify.h).
 */

#include "verify.h"
#include "vm.h"          /* MAX_FRAME_STACK */
#include "chunk.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* collect every function reachable through constant references         */
typedef struct {
    ObjFunction **items;
    size_t        count;
    size_t        cap;
} FList;

static int flist_has(FList *l, ObjFunction *f)
{
    for (size_t i = 0; i < l->count; i++)
        if (l->items[i] == f) return 1;
    return 0;
}

static int collect_fns(FList *l, ObjFunction *f)
{
    if (flist_has(l, f)) return 1;
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        ObjFunction **ni = (ObjFunction **)realloc(l->items, nc * sizeof(*ni));
        if (!ni) return 0;
        l->items = ni;
        l->cap   = nc;
    }
    l->items[l->count++] = f;
    for (size_t i = 0; i < f->chunk.constants.count; i++) {
        Value c = f->chunk.constants.values[i];
        if (IS_FUNCTION(c) && !collect_fns(l, AS_FUNCTION(c)))
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* pass 1: linear decode -- instruction boundaries + static checks      */
static int pass1(ObjFunction *f, uint8_t *boundary)
{
    const uint8_t *code = f->chunk.code;
    size_t  n  = f->chunk.count;
    size_t  cc = f->chunk.constants.count;
    Value  *K  = f->chunk.constants.values;

    size_t off = 0;
    while (off < n) {
        boundary[off] = 1;
        uint8_t op = code[off];
        size_t  len;
        switch (op) {
        case OP_NIL: case OP_TRUE: case OP_FALSE: case OP_POP:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_NEG: case OP_NOT:
        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_BNOT:
        case OP_SHL: case OP_SHR:
        case OP_EQ: case OP_NE: case OP_LT:
        case OP_LE: case OP_GT: case OP_GE: case OP_RETURN:
        case OP_CLOSE_UPVALUE: case OP_INDEX_GET: case OP_INDEX_SET:
        case OP_LEN: case OP_PRINT: case OP_HALT:
            len = 1; break;

        case OP_CALL: case OP_NEW_ARRAY: case OP_NEW_TABLE:
        case OP_GET_LOCAL: case OP_SET_LOCAL:
            if (off + 2 > n) return 0;
            len = 2; break;

        case OP_CONST:
            if (off + 2 > n) return 0;
            if (code[off + 1] >= cc) return 0;
            len = 2; break;

        case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_DEF_GLOBAL: {
            if (off + 2 > n) return 0;
            uint8_t ci = code[off + 1];
            if (ci >= cc || !IS_STRING(K[ci])) return 0;
            len = 2; break;
        }

        case OP_GET_UPVALUE: case OP_SET_UPVALUE:
            if (off + 2 > n) return 0;
            if (code[off + 1] >= f->upvalue_count) return 0;
            len = 2; break;

        case OP_JMP: case OP_JMP_IF_FALSE: {
            if (off + 3 > n) return 0;
            uint16_t o = (uint16_t)((code[off + 1] << 8) | code[off + 2]);
            if (off + 3 + (size_t)o >= n) return 0;       /* forward, in range */
            len = 3; break;
        }
        case OP_LOOP: {
            if (off + 3 > n) return 0;
            uint16_t o = (uint16_t)((code[off + 1] << 8) | code[off + 2]);
            if ((size_t)o > off + 3) return 0;            /* backward, not before 0 */
            len = 3; break;
        }

        case OP_CLOSURE: {
            if (off + 2 > n) return 0;
            uint8_t ci = code[off + 1];
            if (ci >= cc || !IS_FUNCTION(K[ci])) return 0;
            int uvc = AS_FUNCTION(K[ci])->upvalue_count;
            len = 2 + 2 * (size_t)uvc;
            if (off + len > n) return 0;
            break;
        }

        default:
            return 0;                                     /* unknown opcode */
        }
        off += len;
    }
    return off == n;                                      /* must tile exactly */
}

/* ------------------------------------------------------------------ */
/* pass 2: stack-height abstract interpretation                         */
static int pass2(ObjFunction *f, const uint8_t *boundary)
{
    const uint8_t *code = f->chunk.code;
    size_t  n = f->chunk.count;
    Value  *K = f->chunk.constants.values;

    int    *inh  = (int *)malloc(n * sizeof(int));
    size_t *work = (size_t *)malloc(n * sizeof(size_t));
    if (!inh || !work) { free(inh); free(work); return 0; }
    for (size_t i = 0; i < n; i++) inh[i] = -1;

    size_t wc = 0;
    int start_h = f->arity + 1;                /* callee slot + arguments */
    inh[0] = start_h;
    work[wc++] = 0;
    int ok = 1;

#define FAIL() do { ok = 0; goto done; } while (0)
#define NEED(k) do { if (h < (k)) FAIL(); } while (0)
#define SUCC(s, sh) do {                                  \
        size_t s_ = (s); int sh_ = (sh);                  \
        if (s_ >= n || !boundary[s_]) FAIL();             \
        if (sh_ < 0 || sh_ > MAX_FRAME_STACK) FAIL();     \
        if (inh[s_] == -1) { inh[s_] = sh_; work[wc++] = s_; } \
        else if (inh[s_] != sh_) FAIL();                  \
    } while (0)

    while (wc > 0) {
        size_t off = work[--wc];
        int    h   = inh[off];
        uint8_t op = code[off];
        int    ho  = h;
        size_t len = 1;

        switch (op) {
        case OP_CONST:  ho = h + 1; len = 2; break;
        case OP_NIL: case OP_TRUE: case OP_FALSE: ho = h + 1; len = 1; break;
        case OP_POP:    NEED(1); ho = h - 1; len = 1; break;

        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
            NEED(2); ho = h - 1; len = 1; break;
        case OP_NEG: case OP_NOT: case OP_BNOT: NEED(1); ho = h; len = 1; break;

        case OP_GET_LOCAL: { uint8_t s = code[off + 1]; if (s >= h) FAIL(); ho = h + 1; len = 2; break; }
        case OP_SET_LOCAL: { uint8_t s = code[off + 1]; if (s >= h) FAIL(); NEED(1); ho = h; len = 2; break; }
        case OP_GET_GLOBAL: ho = h + 1; len = 2; break;
        case OP_SET_GLOBAL: NEED(1); ho = h; len = 2; break;
        case OP_DEF_GLOBAL: NEED(1); ho = h - 1; len = 2; break;
        case OP_GET_UPVALUE: ho = h + 1; len = 2; break;
        case OP_SET_UPVALUE: NEED(1); ho = h; len = 2; break;

        case OP_CALL: { uint8_t argc = code[off + 1]; NEED(argc + 1); ho = h - argc; len = 2; break; }
        case OP_RETURN: NEED(1); len = 1; goto no_fall;    /* terminates path */
        case OP_HALT:   len = 1; goto no_fall;

        case OP_CLOSURE: {
            uint8_t ci = code[off + 1];
            int uvc = AS_FUNCTION(K[ci])->upvalue_count;
            for (int j = 0; j < uvc; j++) {
                uint8_t is_local = code[off + 2 + 2 * j];
                uint8_t idx      = code[off + 3 + 2 * j];
                if (is_local) { if (idx >= h) FAIL(); }
                else          { if (idx >= f->upvalue_count) FAIL(); }
            }
            ho = h + 1; len = 2 + 2 * uvc; break;
        }
        case OP_CLOSE_UPVALUE: NEED(1); ho = h - 1; len = 1; break;
        case OP_NEW_ARRAY: { uint8_t c = code[off + 1]; NEED(c); ho = h - c + 1; len = 2; break; }
        case OP_NEW_TABLE: { uint8_t c = code[off + 1]; NEED(2 * c); ho = h - 2 * c + 1; len = 2; break; }
        case OP_INDEX_GET: NEED(2); ho = h - 1; len = 1; break;
        case OP_INDEX_SET: NEED(3); ho = h - 2; len = 1; break;
        case OP_LEN:   NEED(1); ho = h; len = 1; break;
        case OP_PRINT: NEED(1); ho = h - 1; len = 1; break;

        case OP_JMP: {
            uint16_t o = (uint16_t)((code[off + 1] << 8) | code[off + 2]);
            SUCC(off + 3 + (size_t)o, h);
            goto next;                                     /* no fall-through */
        }
        case OP_JMP_IF_FALSE: {
            uint16_t o = (uint16_t)((code[off + 1] << 8) | code[off + 2]);
            SUCC(off + 3 + (size_t)o, h);                  /* taken: cond still on stack */
            SUCC(off + 3, h);                              /* fall: cond still on stack */
            goto next;
        }
        case OP_LOOP: {
            uint16_t o = (uint16_t)((code[off + 1] << 8) | code[off + 2]);
            size_t target = off + 3 - (size_t)o;
            /* A backward edge is a control-flow merge just like a forward jump:
             * the height arriving from the loop's tail must equal the height
             * already inferred at the target.  SUCC re-checks the boundary and
             * range and enforces that equality, rejecting any loop whose body
             * has a non-zero net stack effect. */
            SUCC(target, h);
            goto next;
        }
        default: FAIL();
        }

        if (ho < 0 || ho > MAX_FRAME_STACK) FAIL();
        SUCC(off + len, ho);                               /* fall-through */
    next:
        continue;
    no_fall:
        continue;
    }

done:
#undef FAIL
#undef NEED
#undef SUCC
    free(inh);
    free(work);
    return ok;
}

static int verify_function(ObjFunction *f)
{
    size_t n = f->chunk.count;
    if (n == 0) return 0;
    if (f->arity < 0 || f->arity > 255) return 0;
    if (f->upvalue_count < 0 || f->upvalue_count > 255) return 0;
    if (f->arity + 1 > MAX_FRAME_STACK) return 0;

    uint8_t *boundary = (uint8_t *)calloc(n, 1);
    if (!boundary) return 0;
    int ok = pass1(f, boundary) && pass2(f, boundary);
    free(boundary);
    return ok;
}

int verify_module(ObjFunction *entry)
{
    if (!entry || entry->arity != 0 || entry->upvalue_count != 0)
        return VERIFY_FAIL;

    FList l = {0};
    if (!collect_fns(&l, entry)) { free(l.items); return VERIFY_FAIL; }

    int ok = 1;
    for (size_t i = 0; i < l.count && ok; i++)
        if (!verify_function(l.items[i])) ok = 0;

    free(l.items);
    return ok ? VERIFY_OK : VERIFY_FAIL;
}
