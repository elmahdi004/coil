/*
 * asm.c -- textual assembler / disassembler for Coil bytecode (see asm.h).
 */

#include "asm.h"
#include "chunk.h"
#include "module.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* mnemonic table                                                      */

typedef enum {
    OPD_NONE,     /* no operand                                */
    OPD_U8,       /* one unsigned byte immediate               */
    OPD_CONST,    /* a literal that becomes a constant         */
    OPD_GLOBAL,   /* a name that becomes a string constant     */
    OPD_JUMP,     /* forward branch to a label (OP_JMP family) */
    OPD_LOOP      /* backward branch to a label (OP_LOOP)      */
} Operand;

static const struct { const char *name; uint8_t op; Operand kind; } MNEMONICS[] = {
    { "NIL", OP_NIL, OPD_NONE }, { "TRUE", OP_TRUE, OPD_NONE }, { "FALSE", OP_FALSE, OPD_NONE },
    { "POP", OP_POP, OPD_NONE },
    { "ADD", OP_ADD, OPD_NONE }, { "SUB", OP_SUB, OPD_NONE }, { "MUL", OP_MUL, OPD_NONE },
    { "DIV", OP_DIV, OPD_NONE }, { "MOD", OP_MOD, OPD_NONE }, { "NEG", OP_NEG, OPD_NONE },
    { "NOT", OP_NOT, OPD_NONE },
    { "BAND", OP_BAND, OPD_NONE }, { "BOR", OP_BOR, OPD_NONE }, { "BXOR", OP_BXOR, OPD_NONE },
    { "BNOT", OP_BNOT, OPD_NONE }, { "SHL", OP_SHL, OPD_NONE }, { "SHR", OP_SHR, OPD_NONE },
    { "EQ", OP_EQ, OPD_NONE }, { "NE", OP_NE, OPD_NONE }, { "LT", OP_LT, OPD_NONE },
    { "LE", OP_LE, OPD_NONE }, { "GT", OP_GT, OPD_NONE }, { "GE", OP_GE, OPD_NONE },
    { "CONST", OP_CONST, OPD_CONST },
    { "GET_LOCAL", OP_GET_LOCAL, OPD_U8 }, { "SET_LOCAL", OP_SET_LOCAL, OPD_U8 },
    { "GET_GLOBAL", OP_GET_GLOBAL, OPD_GLOBAL }, { "SET_GLOBAL", OP_SET_GLOBAL, OPD_GLOBAL },
    { "DEF_GLOBAL", OP_DEF_GLOBAL, OPD_GLOBAL },
    { "GET_UPVALUE", OP_GET_UPVALUE, OPD_U8 }, { "SET_UPVALUE", OP_SET_UPVALUE, OPD_U8 },
    { "JMP", OP_JMP, OPD_JUMP }, { "JMP_IF_FALSE", OP_JMP_IF_FALSE, OPD_JUMP },
    { "LOOP", OP_LOOP, OPD_LOOP },
    { "CALL", OP_CALL, OPD_U8 }, { "RETURN", OP_RETURN, OPD_NONE },
    { "CLOSE_UPVALUE", OP_CLOSE_UPVALUE, OPD_NONE },
    { "NEW_ARRAY", OP_NEW_ARRAY, OPD_U8 }, { "NEW_TABLE", OP_NEW_TABLE, OPD_U8 },
    { "INDEX_GET", OP_INDEX_GET, OPD_NONE }, { "INDEX_SET", OP_INDEX_SET, OPD_NONE },
    { "LEN", OP_LEN, OPD_NONE }, { "PRINT", OP_PRINT, OPD_NONE }, { "HALT", OP_HALT, OPD_NONE },
};

static int find_mnemonic(const char *s, uint8_t *op, Operand *kind)
{
    for (size_t i = 0; i < sizeof(MNEMONICS) / sizeof(MNEMONICS[0]); i++)
        if (strcmp(MNEMONICS[i].name, s) == 0) {
            *op = MNEMONICS[i].op;
            *kind = MNEMONICS[i].kind;
            return 1;
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* a tiny line tokenizer that keeps quoted strings intact              */

/* Read the next whitespace-delimited token from [*pp, lend). A leading '#'
 * ends the line. Returns 1 and fills out (NUL-terminated) on success, else 0.
 * *is_str is set when the token was a "quoted" literal. */
static int read_token(const char **pp, const char *lend,
                      char *out, size_t outsz, int *is_str)
{
    const char *p = *pp;
    *is_str = 0;
    while (p < lend && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    if (p >= lend || *p == '#') { *pp = lend; return 0; }

    size_t n = 0;
    if (*p == '"') {
        *is_str = 1;
        p++;
        while (p < lend && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < lend) {
                char e = *p++;
                switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '0': c = '\0'; break;
                default:  c = e;    break;   /* \" \\ and anything else literal */
                }
            }
            if (n + 1 < outsz) out[n++] = c;
        }
        if (p < lend && *p == '"') p++;      /* closing quote */
    } else {
        while (p < lend && *p != ' ' && *p != '\t' && *p != '\r' && *p != '#')
            if (n + 1 < outsz) out[n++] = *p++;
            else p++;
    }
    out[n] = '\0';
    *pp = p;
    return 1;
}

/* ------------------------------------------------------------------ */
/* label bookkeeping                                                   */

typedef struct { char name[64]; size_t offset; } Label;
typedef struct { size_t pos; char target[64]; int is_loop; int line; } Fixup;

typedef struct {
    Label  *labels;  size_t nlabels,  clabels;
    Fixup  *fixups;  size_t nfixups,  cfixups;
} Asm;

static int push_label(Asm *a, const char *name, size_t offset)
{
    for (size_t i = 0; i < a->nlabels; i++)
        if (strcmp(a->labels[i].name, name) == 0) return -1;   /* duplicate */
    if (a->nlabels == a->clabels) {
        size_t nc = a->clabels ? a->clabels * 2 : 8;
        Label *nl = (Label *)realloc(a->labels, nc * sizeof(Label));
        if (!nl) return -1;
        a->labels = nl; a->clabels = nc;
    }
    Label *L = &a->labels[a->nlabels++];
    snprintf(L->name, sizeof(L->name), "%s", name);
    L->offset = offset;
    return 0;
}

static int push_fixup(Asm *a, size_t pos, const char *target, int is_loop, int line)
{
    if (a->nfixups == a->cfixups) {
        size_t nc = a->cfixups ? a->cfixups * 2 : 8;
        Fixup *nf = (Fixup *)realloc(a->fixups, nc * sizeof(Fixup));
        if (!nf) return -1;
        a->fixups = nf; a->cfixups = nc;
    }
    Fixup *F = &a->fixups[a->nfixups++];
    F->pos = pos;
    snprintf(F->target, sizeof(F->target), "%s", target);
    F->is_loop = is_loop;
    F->line = line;
    return 0;
}

static const Label *find_label(const Asm *a, const char *name)
{
    for (size_t i = 0; i < a->nlabels; i++)
        if (strcmp(a->labels[i].name, name) == 0) return &a->labels[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* literal parsing for CONST                                           */

static Value parse_literal(Heap *h, const char *tok, int is_str, int *ok)
{
    *ok = 1;
    if (is_str) return OBJ_VAL(string_copy(h, tok, strlen(tok)));
    if (strcmp(tok, "true") == 0)  return BOOL_VAL(true);
    if (strcmp(tok, "false") == 0) return BOOL_VAL(false);
    if (strcmp(tok, "nil") == 0)   return NIL_VAL;

    /* number: float if it has a '.', 'e' or 'E', else integer */
    int is_float = 0;
    for (const char *c = tok; *c; c++)
        if (*c == '.' || *c == 'e' || *c == 'E') { is_float = 1; break; }
    char *endp = NULL;
    if (is_float) {
        double d = strtod(tok, &endp);
        if (endp == tok || *endp) { *ok = 0; return NIL_VAL; }
        return FLOAT_VAL(d);
    }
    long long v = strtoll(tok, &endp, 10);
    if (endp == tok || *endp) { *ok = 0; return NIL_VAL; }
    return INT_VAL((int64_t)v);
}

#define FAILF(...) do { if (err) snprintf(err, errsz, __VA_ARGS__); goto fail; } while (0)

ObjFunction *coil_assemble_fn(Heap *h, const char *text, size_t len,
                              char *err, size_t errsz)
{
    Asm a = {0};
    bool saved_pause = h->gc_paused;
    h->gc_paused = true;                     /* the half-built fn is unrooted */

    ObjFunction *fn = function_new(h);
    fn->arity = 0;
    fn->num_slots = 8;

    const char *p = text, *end = text + len;
    int line = 0;

    while (p < end) {
        line++;
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *lend = nl ? nl : end;
        const char *cur = p;
        p = nl ? nl + 1 : end;

        char tok[256]; int is_str;
        if (!read_token(&cur, lend, tok, sizeof tok, &is_str)) continue;  /* blank/comment */

        size_t tlen = strlen(tok);
        if (!is_str && tlen && tok[tlen - 1] == ':') {          /* label */
            tok[tlen - 1] = '\0';
            if (push_label(&a, tok, fn->chunk.count) < 0) FAILF("line %d: duplicate label '%s'", line, tok);
            continue;
        }
        if (!is_str && tok[0] == '.') {                          /* directive */
            char arg[256]; int a_str;
            if (!read_token(&cur, lend, arg, sizeof arg, &a_str)) FAILF("line %d: '%s' needs a value", line, tok);
            long v = strtol(arg, NULL, 10);
            if (strcmp(tok, ".arity") == 0)      fn->arity = (int)v;
            else if (strcmp(tok, ".slots") == 0) fn->num_slots = (uint16_t)v;
            else FAILF("line %d: unknown directive '%s'", line, tok);
            continue;
        }

        uint8_t op; Operand kind;
        if (!find_mnemonic(tok, &op, &kind)) FAILF("line %d: unknown mnemonic '%s'", line, tok);
        if (chunk_write(&fn->chunk, op, line) < 0) FAILF("out of memory");

        if (kind == OPD_NONE) continue;

        char arg[256]; int a_str;
        if (!read_token(&cur, lend, arg, sizeof arg, &a_str)) FAILF("line %d: '%s' needs an operand", line, tok);

        switch (kind) {
        case OPD_U8: {
            long v = strtol(arg, NULL, 10);
            if (v < 0 || v > 255) FAILF("line %d: operand %ld out of byte range", line, v);
            chunk_write(&fn->chunk, (uint8_t)v, line);
            break;
        }
        case OPD_CONST: {
            int ok; Value c = parse_literal(h, arg, a_str, &ok);
            if (!ok) FAILF("line %d: bad literal '%s'", line, arg);
            int idx = chunk_add_const(&fn->chunk, c);
            if (idx < 0 || idx > 255) FAILF("line %d: constant pool overflow", line);
            chunk_write(&fn->chunk, (uint8_t)idx, line);
            break;
        }
        case OPD_GLOBAL: {
            Value c = OBJ_VAL(string_copy(h, arg, strlen(arg)));
            int idx = chunk_add_const(&fn->chunk, c);
            if (idx < 0 || idx > 255) FAILF("line %d: constant pool overflow", line);
            chunk_write(&fn->chunk, (uint8_t)idx, line);
            break;
        }
        case OPD_JUMP:
        case OPD_LOOP:
            chunk_write(&fn->chunk, 0, line);            /* placeholder u16 */
            chunk_write(&fn->chunk, 0, line);
            if (push_fixup(&a, fn->chunk.count - 2, arg, kind == OPD_LOOP, line) < 0) FAILF("out of memory");
            break;
        default: break;
        }
    }

    /* resolve jumps */
    for (size_t i = 0; i < a.nfixups; i++) {
        Fixup *F = &a.fixups[i];
        const Label *L = find_label(&a, F->target);
        if (!L) FAILF("line %d: undefined label '%s'", F->line, F->target);
        size_t instr_end = F->pos + 2;                   /* byte after the offset */
        long off;
        if (F->is_loop) {
            if (L->offset > instr_end) FAILF("line %d: LOOP '%s' jumps forward", F->line, F->target);
            off = (long)(instr_end - L->offset);
        } else {
            if (L->offset < instr_end) FAILF("line %d: JMP '%s' jumps backward", F->line, F->target);
            off = (long)(L->offset - instr_end);
        }
        if (off < 0 || off > 0xFFFF) FAILF("line %d: jump to '%s' out of range", F->line, F->target);
        fn->chunk.code[F->pos]     = (uint8_t)(off >> 8);
        fn->chunk.code[F->pos + 1] = (uint8_t)(off & 0xFF);
    }

    free(a.labels);
    free(a.fixups);
    h->gc_paused = saved_pause;
    return fn;

fail:
    free(a.labels);
    free(a.fixups);
    h->gc_paused = saved_pause;
    return NULL;
}

int coil_assemble(const char *text, size_t len,
                  uint8_t **out, size_t *out_len, char *err, size_t errsz)
{
    /* a throwaway heap just to build + serialize (bytes are self-contained) */
    Heap h;
    heap_init(&h);
    ObjFunction *fn = coil_assemble_fn(&h, text, len, err, errsz);
    int rc = -1;
    if (fn && module_serialize(fn, out, out_len) == MODULE_OK)
        rc = 0;
    else if (err && fn)
        snprintf(err, errsz, "serialize failed");
    heap_free(&h);
    return rc;
}

/* ------------------------------------------------------------------ */
/* textual disassembler (inverse direction)                            */

typedef struct { char *buf; size_t len, cap; } SB;

static void sb_puts(SB *s, const char *str)
{
    size_t n = strlen(str);
    if (s->len + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->len + n + 1) nc *= 2;
        char *nb = (char *)realloc(s->buf, nc);
        if (!nb) return;
        s->buf = nb; s->cap = nc;
    }
    memcpy(s->buf + s->len, str, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static const char *op_name(uint8_t op, Operand *kind)
{
    for (size_t i = 0; i < sizeof(MNEMONICS) / sizeof(MNEMONICS[0]); i++)
        if (MNEMONICS[i].op == op) { *kind = MNEMONICS[i].kind; return MNEMONICS[i].name; }
    *kind = OPD_NONE;
    return NULL;
}

/* instruction length for the textual walk (mirrors verify.c pass 1). */
static size_t instr_len(const Chunk *c, size_t off)
{
    uint8_t op = c->code[off];
    Operand k;
    if (op == OP_CLOSURE) {                              /* variable length */
        if (off + 2 > c->count) return 1;
        uint8_t ci = c->code[off + 1];
        int uvc = (ci < c->constants.count && IS_FUNCTION(c->constants.values[ci]))
                      ? AS_FUNCTION(c->constants.values[ci])->upvalue_count : 0;
        return 2 + 2 * (size_t)uvc;
    }
    op_name(op, &k);
    switch (k) {
    case OPD_NONE:  return 1;
    case OPD_U8: case OPD_CONST: case OPD_GLOBAL: return 2;
    case OPD_JUMP:  case OPD_LOOP: return 3;
    }
    return 1;
}

ObjString *coil_disasm_text(Heap *h, ObjFunction *fn)
{
    const Chunk *c = &fn->chunk;

    /* pass 1: map every jump target offset to a synthetic label id */
    size_t *targets = (size_t *)calloc(c->count ? c->count : 1, sizeof(size_t));
    for (size_t off = 0; off < c->count; ) {
        uint8_t op = c->code[off];
        if ((op == OP_JMP || op == OP_JMP_IF_FALSE || op == OP_LOOP) && off + 3 <= c->count) {
            uint16_t o = (uint16_t)((c->code[off + 1] << 8) | c->code[off + 2]);
            size_t tgt = (op == OP_LOOP) ? (off + 3 - o) : (off + 3 + o);
            if (tgt < c->count) targets[tgt] = 1;
        }
        size_t l = instr_len(c, off);
        off += l ? l : 1;
    }

    SB s = {0};
    char tmp[128];
    snprintf(tmp, sizeof tmp, ".arity %d\n.slots %u\n", fn->arity, (unsigned)fn->num_slots);
    sb_puts(&s, tmp);

    for (size_t off = 0; off < c->count; ) {
        if (targets[off]) { snprintf(tmp, sizeof tmp, "L%zu:\n", off); sb_puts(&s, tmp); }
        uint8_t op = c->code[off];

        /* OP_CLOSURE is variable length and has no assembler mnemonic; render it
         * for inspection only (with its captured upvalue count). */
        if (op == OP_CLOSURE) {
            size_t l = instr_len(c, off);
            snprintf(tmp, sizeof tmp, "  CLOSURE %u", off + 1 < c->count ? c->code[off + 1] : 0);
            sb_puts(&s, tmp);
            int uvc = (int)((l >= 2 ? l - 2 : 0) / 2);
            if (uvc > 0) { snprintf(tmp, sizeof tmp, "   ; %d upvalue(s)", uvc); sb_puts(&s, tmp); }
            sb_puts(&s, "\n");
            off += l ? l : 1;
            continue;
        }

        Operand kind;
        const char *name = op_name(op, &kind);
        if (!name) { snprintf(tmp, sizeof tmp, "  ; <bad op %u>\n", op); sb_puts(&s, tmp); off++; continue; }

        sb_puts(&s, "  ");
        sb_puts(&s, name);
        if (kind == OPD_U8) {
            snprintf(tmp, sizeof tmp, " %u", c->code[off + 1]); sb_puts(&s, tmp);
        } else if (kind == OPD_CONST || kind == OPD_GLOBAL) {
            Value k = c->constants.values[c->code[off + 1]];
            if (IS_INT(k))        snprintf(tmp, sizeof tmp, " %lld", (long long)AS_INT(k));
            else if (IS_FLOAT(k)) snprintf(tmp, sizeof tmp, " %g", AS_FLOAT(k));
            else if (IS_BOOL(k))  snprintf(tmp, sizeof tmp, " %s", AS_BOOL(k) ? "true" : "false");
            else if (IS_NIL(k))   snprintf(tmp, sizeof tmp, " nil");
            else if (IS_STRING(k))snprintf(tmp, sizeof tmp, " \"%s\"", AS_CSTRING(k));
            else                  snprintf(tmp, sizeof tmp, " ?");
            sb_puts(&s, tmp);
        } else if (kind == OPD_JUMP || kind == OPD_LOOP) {
            uint16_t o = (uint16_t)((c->code[off + 1] << 8) | c->code[off + 2]);
            size_t tgt = (op == OP_LOOP) ? (off + 3 - o) : (off + 3 + o);
            snprintf(tmp, sizeof tmp, " L%zu", tgt); sb_puts(&s, tmp);
        }
        sb_puts(&s, "\n");
        size_t l = instr_len(c, off);
        off += l ? l : 1;
    }

    free(targets);
    ObjString *out = string_copy(h, s.buf ? s.buf : "", s.len);
    free(s.buf);
    return out;
}
