/*
 * optimize.c -- peephole optimizer (see optimize.h).
 *
 * Strategy: decode the chunk into an array of instructions, rewrite jump
 * operands as target *instruction indices*, apply local rewrites until a
 * fixpoint, drop unreachable instructions, then re-emit -- assigning fresh byte
 * offsets and recomputing every branch from its target index.  Because targets
 * are indices, shrinking the code can never silently break a jump.
 */

#include "optimize.h"
#include "chunk.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t   op;
    uint8_t   u8;         /* immediate / constant index (if has_u8)     */
    int       has_u8;
    int       is_jump;    /* OP_JMP / OP_JMP_IF_FALSE / OP_LOOP         */
    int       cond;       /* conditional jump (JMP_IF_FALSE)            */
    int       back;       /* backward jump (LOOP)                       */
    int       target;     /* target instruction index, for jumps       */
    const uint8_t *extra; /* CLOSURE trailing operand bytes             */
    size_t    nextra;
    int       is_target;  /* some jump lands here                       */
    int       removed;
    size_t    off;        /* original byte offset                       */
} Inst;

/* per-opcode encoded length in the source chunk (mirrors verify.c pass 1). */
static size_t enc_len(const Chunk *c, size_t off)
{
    uint8_t op = c->code[off];
    switch (op) {
    case OP_CONST: case OP_GET_LOCAL: case OP_SET_LOCAL:
    case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_DEF_GLOBAL:
    case OP_GET_UPVALUE: case OP_SET_UPVALUE:
    case OP_CALL: case OP_NEW_ARRAY: case OP_NEW_TABLE:
        return 2;
    case OP_JMP: case OP_JMP_IF_FALSE: case OP_LOOP:
        return 3;
    case OP_CLOSURE: {
        if (off + 2 > c->count) return 1;
        uint8_t ci = c->code[off + 1];
        int uvc = (ci < c->constants.count && IS_FUNCTION(c->constants.values[ci]))
                      ? AS_FUNCTION(c->constants.values[ci])->upvalue_count : 0;
        return 2 + 2 * (size_t)uvc;
    }
    default:
        return 1;
    }
}

static int has_u8_operand(uint8_t op)
{
    switch (op) {
    case OP_CONST: case OP_GET_LOCAL: case OP_SET_LOCAL:
    case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_DEF_GLOBAL:
    case OP_GET_UPVALUE: case OP_SET_UPVALUE:
    case OP_CALL: case OP_NEW_ARRAY: case OP_NEW_TABLE:
        return 1;
    default:
        return 0;
    }
}

/* decode chunk into insts[]; returns count, or -1 if the chunk looks malformed
 * (in which case the caller should just copy the original). */
static int decode(const Chunk *c, Inst **out_insts, int **out_off2idx)
{
    Inst *insts = (Inst *)calloc(c->count ? c->count : 1, sizeof(Inst));
    int  *off2idx = (int *)malloc((c->count + 1) * sizeof(int));
    if (!insts || !off2idx) { free(insts); free(off2idx); return -1; }
    for (size_t i = 0; i <= c->count; i++) off2idx[i] = -1;

    int n = 0;
    for (size_t off = 0; off < c->count; ) {
        size_t len = enc_len(c, off);
        if (off + len > c->count) { free(insts); free(off2idx); return -1; }
        Inst *in = &insts[n];
        in->op  = c->code[off];
        in->off = off;
        off2idx[off] = n;
        if (has_u8_operand(in->op)) { in->has_u8 = 1; in->u8 = c->code[off + 1]; }
        if (in->op == OP_JMP || in->op == OP_JMP_IF_FALSE || in->op == OP_LOOP) {
            in->is_jump = 1;
            in->cond = (in->op == OP_JMP_IF_FALSE);
            in->back = (in->op == OP_LOOP);
        }
        if (in->op == OP_CLOSURE) { in->has_u8 = 1; in->u8 = c->code[off + 1]; in->extra = c->code + off + 2; in->nextra = len - 2; }
        n++;
        off += len;
    }

    /* resolve jump targets to instruction indices */
    for (int i = 0; i < n; i++) {
        if (!insts[i].is_jump) continue;
        size_t off = insts[i].off;
        uint16_t o = (uint16_t)((c->code[off + 1] << 8) | c->code[off + 2]);
        size_t tgt = insts[i].back ? (off + 3 - o) : (off + 3 + o);
        if (tgt > c->count || off2idx[tgt] < 0) { free(insts); free(off2idx); return -1; }
        insts[i].target = off2idx[tgt];
        insts[insts[i].target].is_target = 1;
    }

    *out_insts = insts;
    *out_off2idx = off2idx;
    return n;
}

/* next live instruction index at or after i, or n if none. */
static int live_from(const Inst *insts, int n, int i)
{
    while (i < n && insts[i].removed) i++;
    return i;
}

/* ---- transformations -------------------------------------------------- */

/* opcodes that push one constant value and have no side effect. */
static int is_lit_push(uint8_t op)
{
    return op == OP_CONST || op == OP_TRUE || op == OP_FALSE || op == OP_NIL;
}

static Value lit_value(const Chunk *out, const Inst *in)
{
    switch (in->op) {
    case OP_TRUE:  return BOOL_VAL(true);
    case OP_FALSE: return BOOL_VAL(false);
    case OP_NIL:   return NIL_VAL;
    default:       return out->constants.values[in->u8];   /* OP_CONST */
    }
}

/* fold a unary op applied to a literal into that literal. */
static int try_fold_unary(Chunk *out, Inst *insts, int n, int i)
{
    if (!is_lit_push(insts[i].op)) return 0;
    int j = live_from(insts, n, i + 1);
    if (j >= n || insts[j].is_target) return 0;
    uint8_t nextop = insts[j].op;
    if (nextop != OP_NEG && nextop != OP_NOT && nextop != OP_BNOT) return 0;

    Value v = lit_value(out, &insts[i]);

    if (nextop == OP_NOT) {
        /* !literal -> a one-byte boolean literal (also shrinks OP_CONST). */
        insts[i].op    = value_truthy(v) ? OP_FALSE : OP_TRUE;
        insts[i].has_u8 = 0;
        insts[j].removed = 1;
        return 1;
    }

    /* arithmetic negation only applies to numeric constants. */
    if (insts[i].op != OP_CONST) return 0;
    Value r;
    if (nextop == OP_NEG) {
        if (IS_INT(v))        r = INT_VAL((int64_t)(-(uint64_t)AS_INT(v)));
        else if (IS_FLOAT(v)) r = FLOAT_VAL(-AS_FLOAT(v));
        else return 0;
    } else { /* OP_BNOT */
        if (!IS_INT(v)) return 0;
        r = INT_VAL(~AS_INT(v));
    }
    int idx = chunk_add_const(out, r);
    if (idx < 0 || idx > 255) return 0;              /* pool full: decline */
    insts[i].u8 = (uint8_t)idx;
    insts[j].removed = 1;
    return 1;
}

/* <literal>; POP  ->  (nothing) */
static int try_drop_dead_push(Inst *insts, int n, int i)
{
    if (!is_lit_push(insts[i].op) || insts[i].is_target) return 0;
    int j = live_from(insts, n, i + 1);
    if (j >= n || insts[j].op != OP_POP || insts[j].is_target) return 0;
    insts[i].removed = 1;
    insts[j].removed = 1;
    return 1;
}

/* JMP a; ... a: JMP b  ->  JMP b  (forward, non-cyclic only). */
static int try_thread_jump(Inst *insts, int n, int i)
{
    if (!insts[i].is_jump || insts[i].back) return 0;
    int changed = 0;
    for (int guard = 0; guard < n; guard++) {
        int t = insts[i].target;
        if (t < 0 || t >= n) break;
        Inst *tgt = &insts[t];
        if (tgt->removed) break;
        if (tgt->op != OP_JMP) break;               /* only chase unconditional */
        if (tgt->target <= i) break;                /* keep the jump forward     */
        if (tgt->target == insts[i].target) break;  /* self loop guard           */
        insts[i].target = tgt->target;
        changed = 1;
    }
    return changed;
}

/* an unconditional JMP whose target is the immediately following live
 * instruction is a no-op. */
static int try_drop_jump_to_next(Inst *insts, int n, int i)
{
    if (insts[i].op != OP_JMP || insts[i].is_target) return 0;
    int next = live_from(insts, n, i + 1);
    if (insts[i].target != next) return 0;
    insts[i].removed = 1;
    return 1;
}

/* mark unreachable instructions removed (simple worklist reachability). */
static void remove_unreachable(Inst *insts, int n)
{
    if (n == 0) return;
    char *seen = (char *)calloc(n, 1);
    int  *work = (int *)malloc(n * sizeof(int));
    if (!seen || !work) { free(seen); free(work); return; }
    int wc = 0;
    work[wc++] = 0;
    while (wc > 0) {
        int i = work[--wc];
        while (i < n && insts[i].removed) i++;       /* skip already-dead */
        if (i >= n || seen[i]) continue;
        seen[i] = 1;
        uint8_t op = insts[i].op;
        if (insts[i].is_jump) {
            if (insts[i].target >= 0 && insts[i].target < n) work[wc++] = insts[i].target;
            if (insts[i].cond) work[wc++] = i + 1;       /* conditional falls through */
            /* unconditional JMP/LOOP: no fall-through */
        } else if (op == OP_RETURN || op == OP_HALT) {
            /* terminates this path */
        } else {
            work[wc++] = i + 1;                          /* fall through */
        }
    }
    for (int i = 0; i < n; i++)
        if (!seen[i]) insts[i].removed = 1;
    free(seen);
    free(work);
}

/* ---- re-emit ---------------------------------------------------------- */

static int reemit(Chunk *out, Inst *insts, int n)
{
    int *new_off = (int *)malloc((n + 1) * sizeof(int));
    if (!new_off) return 0;

    /* assign byte offsets to surviving instructions */
    size_t off = 0;
    for (int i = 0; i < n; i++) {
        new_off[i] = (int)off;
        if (insts[i].removed) continue;
        if (insts[i].is_jump)        off += 3;
        else if (insts[i].op == OP_CLOSURE) off += 2 + insts[i].nextra;
        else if (insts[i].has_u8)    off += 2;
        else                         off += 1;
    }
    new_off[n] = (int)off;

    for (int i = 0; i < n; i++) {
        if (insts[i].removed) continue;
        if (chunk_write(out, insts[i].op, 1) < 0) { free(new_off); return 0; }
        if (insts[i].is_jump) {
            int here_end = new_off[i] + 3;
            int tgt = new_off[insts[i].target];
            long rel = insts[i].back ? (here_end - tgt) : (tgt - here_end);
            if (rel < 0 || rel > 0xFFFF) { free(new_off); return 0; }
            chunk_write(out, (uint8_t)(rel >> 8), 1);
            chunk_write(out, (uint8_t)(rel & 0xFF), 1);
        } else if (insts[i].op == OP_CLOSURE) {
            chunk_write(out, insts[i].u8, 1);
            for (size_t k = 0; k < insts[i].nextra; k++)
                chunk_write(out, insts[i].extra[k], 1);
        } else if (insts[i].has_u8) {
            chunk_write(out, insts[i].u8, 1);
        }
    }
    free(new_off);
    return 1;
}

/* ---- driver ----------------------------------------------------------- */

ObjFunction *optimize_function(Heap *h, ObjFunction *fn)
{
    bool saved = h->gc_paused;
    h->gc_paused = true;

    ObjFunction *out = function_new(h);
    out->arity         = fn->arity;
    out->upvalue_count = fn->upvalue_count;
    out->num_slots     = fn->num_slots;
    out->name          = fn->name;

    /* out->chunk starts with a copy of the source constant pool; folds append. */
    for (size_t i = 0; i < fn->chunk.constants.count; i++)
        if (chunk_add_const(&out->chunk, fn->chunk.constants.values[i]) < 0)
            goto bail;

    Inst *insts = NULL;
    int  *off2idx = NULL;
    int n = decode(&fn->chunk, &insts, &off2idx);
    free(off2idx);
    if (n < 0) goto copy_raw;                          /* malformed: verbatim copy */

    for (int pass = 0; pass < 10; pass++) {
        int changed = 0;
        for (int i = 0; i < n; i++) {
            if (insts[i].removed) continue;
            changed |= try_fold_unary(&out->chunk, insts, n, i);
            changed |= try_drop_dead_push(insts, n, i);
            changed |= try_thread_jump(insts, n, i);
            changed |= try_drop_jump_to_next(insts, n, i);
        }
        if (!changed) break;
    }
    remove_unreachable(insts, n);

    if (!reemit(&out->chunk, insts, n)) { free(insts); goto bail; }
    free(insts);

    h->gc_paused = saved;
    return out;

copy_raw:
    for (size_t i = 0; i < fn->chunk.count; i++)
        if (chunk_write(&out->chunk, fn->chunk.code[i], 1) < 0) goto bail;
    h->gc_paused = saved;
    return out;

bail:
    h->gc_paused = saved;
    return NULL;
}
