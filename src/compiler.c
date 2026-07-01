/*
 * compiler.c -- single-pass Pratt-parsing compiler: source -> bytecode.
 *
 * Compiles directly to a Chunk inside an ObjFunction; locals live on the
 * runtime value stack, globals in a hash table by name, and captured locals
 * become upvalues so closures can outlive their defining frame.
 */

#include "compiler.h"
#include "lexer.h"
#include "memory.h"
#include "gc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS  256
#define MAX_UPVALUES 256

typedef struct {
    Lexer lex;
    Token current;
    Token previous;
    bool  had_error;
    bool  panic_mode;
} Parser;

typedef enum {
    PREC_NONE, PREC_ASSIGNMENT, PREC_OR, PREC_AND,
    PREC_BOR, PREC_BXOR, PREC_BAND, PREC_EQUALITY,
    PREC_COMPARISON, PREC_SHIFT, PREC_TERM, PREC_FACTOR,
    PREC_UNARY, PREC_CALL, PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn    prefix;
    ParseFn    infix;
    Precedence prec;
} ParseRule;

typedef struct {
    Token name;
    int   depth;        /* -1 = declared but not yet initialized */
    bool  is_captured;
} Local;

typedef struct {
    uint8_t index;
    bool    is_local;
} Upvalue;

typedef enum { TYPE_SCRIPT, TYPE_FUNCTION } FunctionType;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction     *function;
    FunctionType     type;
    Local            locals[MAX_LOCALS];
    int              local_count;
    Upvalue          upvalues[MAX_UPVALUES];
    int              scope_depth;
} Compiler;

static Parser    parser;
static Compiler *current;
static Heap     *cheap;

/* ------------------------------------------------------------------ */
static Chunk *current_chunk(void) { return &current->function->chunk; }

static void error_at(Token *tok, const char *msg)
{
    if (parser.panic_mode) return;
    parser.panic_mode = true;
    fprintf(stderr, "[line %d] error", tok->line);
    if (tok->type == TOK_EOF)
        fprintf(stderr, " at end");
    else if (tok->type != TOK_ERROR)
        fprintf(stderr, " at '%.*s'", (int)tok->length, tok->start);
    fprintf(stderr, ": %s\n", msg);
    parser.had_error = true;
}

static void error(const char *msg)       { error_at(&parser.previous, msg); }
static void error_current(const char *m) { error_at(&parser.current, m); }

static void advance(void)
{
    parser.previous = parser.current;
    for (;;) {
        parser.current = lexer_next(&parser.lex);
        if (parser.current.type != TOK_ERROR) break;
        error_current(parser.current.start);
    }
}

static void consume(TokType type, const char *msg)
{
    if (parser.current.type == type) { advance(); return; }
    error_current(msg);
}

static bool check(TokType type) { return parser.current.type == type; }

static bool match(TokType type)
{
    if (!check(type)) return false;
    advance();
    return true;
}

/* ------------------------------------------------------------------ */
/* emit helpers                                                        */
static void emit_byte(uint8_t b) { chunk_write(current_chunk(), b, parser.previous.line); }

static void emit_bytes(uint8_t a, uint8_t b) { emit_byte(a); emit_byte(b); }

static int emit_jump(uint8_t op)
{
    emit_byte(op);
    emit_byte(0xff);
    emit_byte(0xff);
    return (int)current_chunk()->count - 2;
}

static void patch_jump(int offset)
{
    int jump = (int)current_chunk()->count - offset - 2;
    if (jump > 0xffff)
        error("jump too large");
    current_chunk()->code[offset]     = (uint8_t)((jump >> 8) & 0xff);
    current_chunk()->code[offset + 1] = (uint8_t)(jump & 0xff);
}

static void emit_loop(int loop_start)
{
    emit_byte(OP_LOOP);
    int offset = (int)current_chunk()->count - loop_start + 2;
    if (offset > 0xffff)
        error("loop body too large");
    emit_byte((uint8_t)((offset >> 8) & 0xff));
    emit_byte((uint8_t)(offset & 0xff));
}

static uint8_t make_constant(Value v)
{
    int idx = chunk_add_const(current_chunk(), v);
    if (idx < 0 || idx > 255) {
        error("too many constants in one chunk");
        return 0;
    }
    return (uint8_t)idx;
}

static void emit_constant(Value v) { emit_bytes(OP_CONST, make_constant(v)); }

static void emit_return(void) { emit_byte(OP_NIL); emit_byte(OP_RETURN); }

/* ------------------------------------------------------------------ */
static void init_compiler(Compiler *c, FunctionType type)
{
    c->enclosing   = current;
    c->function    = NULL;
    c->type        = type;
    c->local_count = 0;
    c->scope_depth = 0;
    c->function    = function_new(cheap);
    current        = c;

    if (type != TYPE_SCRIPT)
        c->function->name = string_copy(cheap, parser.previous.start,
                                        parser.previous.length);

    /* slot 0 holds the executing function/closure */
    Local *local = &c->locals[c->local_count++];
    local->depth       = 0;
    local->is_captured = false;
    local->name.start  = "";
    local->name.length = 0;
}

static ObjFunction *end_compiler(void)
{
    emit_return();
    ObjFunction *fn = current->function;
    current = current->enclosing;
    return fn;
}

static void begin_scope(void) { current->scope_depth++; }

static void end_scope(void)
{
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        if (current->locals[current->local_count - 1].is_captured)
            emit_byte(OP_CLOSE_UPVALUE);
        else
            emit_byte(OP_POP);
        current->local_count--;
    }
}

/* ------------------------------------------------------------------ */
/* variables                                                           */
static uint8_t identifier_constant(Token *name)
{
    ObjString *s = string_copy(cheap, name->start, name->length);
    return make_constant(OBJ_VAL(s));
}

static bool identifiers_equal(Token *a, Token *b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler *c, Token *name)
{
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (identifiers_equal(name, &c->locals[i].name)) {
            if (c->locals[i].depth == -1)
                error("cannot read local variable in its own initializer");
            return i;
        }
    }
    return -1;
}

static int add_upvalue(Compiler *c, uint8_t index, bool is_local)
{
    int count = c->function->upvalue_count;
    for (int i = 0; i < count; i++)
        if (c->upvalues[i].index == index && c->upvalues[i].is_local == is_local)
            return i;
    if (count == MAX_UPVALUES) {
        error("too many closure variables in function");
        return 0;
    }
    c->upvalues[count].is_local = is_local;
    c->upvalues[count].index    = index;
    return c->function->upvalue_count++;
}

static int resolve_upvalue(Compiler *c, Token *name)
{
    if (c->enclosing == NULL)
        return -1;
    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(c->enclosing, name);
    if (upvalue != -1)
        return add_upvalue(c, (uint8_t)upvalue, false);
    return -1;
}

static void add_local(Token name)
{
    if (current->local_count == MAX_LOCALS) {
        error("too many local variables in function");
        return;
    }
    Local *local = &current->locals[current->local_count++];
    local->name        = name;
    local->depth       = -1;
    local->is_captured = false;

    if (current->local_count > current->function->num_slots)
        current->function->num_slots = (uint16_t)current->local_count;
}

static void declare_variable(void)
{
    if (current->scope_depth == 0)
        return;
    Token *name = &parser.previous;
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth)
            break;
        if (identifiers_equal(name, &local->name))
            error("variable with this name already declared in this scope");
    }
    add_local(*name);
}

static uint8_t parse_variable(const char *msg)
{
    consume(TOK_IDENT, msg);
    declare_variable();
    if (current->scope_depth > 0)
        return 0;
    return identifier_constant(&parser.previous);
}

static void mark_initialized(void)
{
    if (current->scope_depth == 0)
        return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global)
{
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }
    emit_bytes(OP_DEF_GLOBAL, global);
}

/* ------------------------------------------------------------------ */
/* forward declarations                                                */
static void expression(void);
static void statement(void);
static void declaration(void);
static const ParseRule *get_rule(TokType type);
static void parse_precedence(Precedence prec);

static uint8_t argument_list(void)
{
    uint8_t argc = 0;
    if (!check(TOK_RPAREN)) {
        do {
            expression();
            if (argc == 255)
                error("cannot have more than 255 arguments");
            argc++;
        } while (match(TOK_COMMA));
    }
    consume(TOK_RPAREN, "expect ')' after arguments");
    return argc;
}

/* ------------------------------------------------------------------ */
/* expression parse functions                                          */
static void number(bool can_assign)
{
    (void)can_assign;
    if (parser.previous.type == TOK_INT)
        emit_constant(INT_VAL(parser.previous.ival));
    else
        emit_constant(FLOAT_VAL(parser.previous.fval));
}

/* decode the raw (between-quotes) string lexeme, applying escapes */
static void string(bool can_assign)
{
    (void)can_assign;
    const char *src = parser.previous.start;
    size_t      n   = parser.previous.length;

    char  *buf = (char *)malloc(n + 1);
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < n) {
            char e = src[++i];
            switch (e) {
            case 'n':  buf[len++] = '\n'; break;
            case 't':  buf[len++] = '\t'; break;
            case 'r':  buf[len++] = '\r'; break;
            case '0':  buf[len++] = '\0'; break;
            case '"':  buf[len++] = '"';  break;
            case '\\': buf[len++] = '\\'; break;
            default:   buf[len++] = e;    break;
            }
        } else {
            buf[len++] = c;
        }
    }
    buf[len] = '\0';
    ObjString *s = string_copy(cheap, buf, len);
    free(buf);
    emit_constant(OBJ_VAL(s));
}

static void literal(bool can_assign)
{
    (void)can_assign;
    switch (parser.previous.type) {
    case TOK_TRUE:  emit_byte(OP_TRUE);  break;
    case TOK_FALSE: emit_byte(OP_FALSE); break;
    case TOK_NIL:   emit_byte(OP_NIL);   break;
    default: break;
    }
}

static void grouping(bool can_assign)
{
    (void)can_assign;
    expression();
    consume(TOK_RPAREN, "expect ')' after expression");
}

static void unary(bool can_assign)
{
    (void)can_assign;
    TokType op = parser.previous.type;
    parse_precedence(PREC_UNARY);
    switch (op) {
    case TOK_MINUS: emit_byte(OP_NEG);  break;
    case TOK_BANG:  emit_byte(OP_NOT);  break;
    case TOK_TILDE: emit_byte(OP_BNOT); break;
    default: break;
    }
}

static void binary(bool can_assign)
{
    (void)can_assign;
    TokType op = parser.previous.type;
    const ParseRule *rule = get_rule(op);
    parse_precedence((Precedence)(rule->prec + 1));
    switch (op) {
    case TOK_PLUS:    emit_byte(OP_ADD); break;
    case TOK_MINUS:   emit_byte(OP_SUB); break;
    case TOK_STAR:    emit_byte(OP_MUL); break;
    case TOK_SLASH:   emit_byte(OP_DIV); break;
    case TOK_PERCENT: emit_byte(OP_MOD); break;
    case TOK_EQ:      emit_byte(OP_EQ);  break;
    case TOK_NE:      emit_byte(OP_NE);  break;
    case TOK_LT:      emit_byte(OP_LT);  break;
    case TOK_LE:      emit_byte(OP_LE);  break;
    case TOK_GT:      emit_byte(OP_GT);  break;
    case TOK_GE:      emit_byte(OP_GE);  break;
    case TOK_AMP:     emit_byte(OP_BAND); break;
    case TOK_PIPE:    emit_byte(OP_BOR);  break;
    case TOK_CARET:   emit_byte(OP_BXOR); break;
    case TOK_SHL:     emit_byte(OP_SHL);  break;
    case TOK_SHR:     emit_byte(OP_SHR);  break;
    default: break;
    }
}

static void and_(bool can_assign)
{
    (void)can_assign;
    int end = emit_jump(OP_JMP_IF_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);
    patch_jump(end);
}

static void or_(bool can_assign)
{
    (void)can_assign;
    int else_jump = emit_jump(OP_JMP_IF_FALSE);
    int end_jump  = emit_jump(OP_JMP);
    patch_jump(else_jump);
    emit_byte(OP_POP);
    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static void named_variable(Token name, bool can_assign)
{
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    if (arg != -1) {
        get_op = OP_GET_LOCAL; set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE; set_op = OP_SET_UPVALUE;
    } else {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL; set_op = OP_SET_GLOBAL;
    }
    if (can_assign && match(TOK_ASSIGN)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        emit_bytes(get_op, (uint8_t)arg);
    }
}

static void variable(bool can_assign) { named_variable(parser.previous, can_assign); }

static void call(bool can_assign)
{
    (void)can_assign;
    uint8_t argc = argument_list();
    emit_bytes(OP_CALL, argc);
}

static void array_literal(bool can_assign)
{
    (void)can_assign;
    int count = 0;
    if (!check(TOK_RBRACKET)) {
        do {
            expression();
            if (count == 255)
                error("array literal too large");
            count++;
        } while (match(TOK_COMMA));
    }
    consume(TOK_RBRACKET, "expect ']' after array elements");
    emit_bytes(OP_NEW_ARRAY, (uint8_t)count);
}

static void table_literal(bool can_assign)
{
    (void)can_assign;
    int count = 0;
    if (!check(TOK_RBRACE)) {
        do {
            expression();                                   /* key */
            consume(TOK_COLON, "expect ':' after table key");
            expression();                                   /* value */
            if (count == 255)
                error("table literal too large");
            count++;
        } while (match(TOK_COMMA));
    }
    consume(TOK_RBRACE, "expect '}' after table entries");
    emit_bytes(OP_NEW_TABLE, (uint8_t)count);
}

static void subscript(bool can_assign)
{
    expression();
    consume(TOK_RBRACKET, "expect ']' after index");
    if (can_assign && match(TOK_ASSIGN)) {
        expression();
        emit_byte(OP_INDEX_SET);
    } else {
        emit_byte(OP_INDEX_GET);
    }
}

/* ------------------------------------------------------------------ */
/* parse-rule table                                                    */
static const ParseRule rules[] = {
    [TOK_INT]      = { number,        NULL,      PREC_NONE },
    [TOK_FLOAT]    = { number,        NULL,      PREC_NONE },
    [TOK_STRING]   = { string,        NULL,      PREC_NONE },
    [TOK_TRUE]     = { literal,       NULL,      PREC_NONE },
    [TOK_FALSE]    = { literal,       NULL,      PREC_NONE },
    [TOK_NIL]      = { literal,       NULL,      PREC_NONE },
    [TOK_IDENT]    = { variable,      NULL,      PREC_NONE },
    [TOK_LPAREN]   = { grouping,      call,      PREC_CALL },
    [TOK_LBRACKET] = { array_literal, subscript, PREC_CALL },
    [TOK_LBRACE]   = { table_literal, NULL,      PREC_NONE },
    [TOK_MINUS]    = { unary,         binary,    PREC_TERM },
    [TOK_PLUS]     = { NULL,          binary,    PREC_TERM },
    [TOK_STAR]     = { NULL,          binary,    PREC_FACTOR },
    [TOK_SLASH]    = { NULL,          binary,    PREC_FACTOR },
    [TOK_PERCENT]  = { NULL,          binary,    PREC_FACTOR },
    [TOK_EQ]       = { NULL,          binary,    PREC_EQUALITY },
    [TOK_NE]       = { NULL,          binary,    PREC_EQUALITY },
    [TOK_LT]       = { NULL,          binary,    PREC_COMPARISON },
    [TOK_LE]       = { NULL,          binary,    PREC_COMPARISON },
    [TOK_GT]       = { NULL,          binary,    PREC_COMPARISON },
    [TOK_GE]       = { NULL,          binary,    PREC_COMPARISON },
    [TOK_AND]      = { NULL,          and_,      PREC_AND },
    [TOK_OR]       = { NULL,          or_,       PREC_OR },
    [TOK_BANG]     = { unary,         NULL,      PREC_NONE },
    [TOK_TILDE]    = { unary,         NULL,      PREC_NONE },
    [TOK_AMP]      = { NULL,          binary,    PREC_BAND },
    [TOK_PIPE]     = { NULL,          binary,    PREC_BOR },
    [TOK_CARET]    = { NULL,          binary,    PREC_BXOR },
    [TOK_SHL]      = { NULL,          binary,    PREC_SHIFT },
    [TOK_SHR]      = { NULL,          binary,    PREC_SHIFT },
    [TOK_ERROR]    = { NULL,          NULL,      PREC_NONE },
    [TOK_EOF]      = { NULL,          NULL,      PREC_NONE },
};

static const ParseRule *get_rule(TokType type) { return &rules[type]; }

static void parse_precedence(Precedence prec)
{
    advance();
    ParseFn prefix = get_rule(parser.previous.type)->prefix;
    if (prefix == NULL) {
        error("expect expression");
        return;
    }
    bool can_assign = prec <= PREC_ASSIGNMENT;
    prefix(can_assign);

    while (prec <= get_rule(parser.current.type)->prec) {
        advance();
        ParseFn infix = get_rule(parser.previous.type)->infix;
        infix(can_assign);
    }

    if (can_assign && match(TOK_ASSIGN))
        error("invalid assignment target");
}

static void expression(void) { parse_precedence(PREC_ASSIGNMENT); }

/* ------------------------------------------------------------------ */
/* statements & declarations                                           */
static void block(void)
{
    while (!check(TOK_RBRACE) && !check(TOK_EOF))
        declaration();
    consume(TOK_RBRACE, "expect '}' after block");
}

static void function_body(FunctionType type)
{
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TOK_LPAREN, "expect '(' after function name");
    if (!check(TOK_RPAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255)
                error_current("cannot have more than 255 parameters");
            uint8_t constant = parse_variable("expect parameter name");
            define_variable(constant);
        } while (match(TOK_COMMA));
    }
    consume(TOK_RPAREN, "expect ')' after parameters");
    consume(TOK_LBRACE, "expect '{' before function body");
    block();

    ObjFunction *fn = end_compiler();
    emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(fn)));
    for (int i = 0; i < fn->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void fn_declaration(void)
{
    uint8_t global = parse_variable("expect function name");
    mark_initialized();
    function_body(TYPE_FUNCTION);
    define_variable(global);
}

static void var_declaration(void)
{
    uint8_t global = parse_variable("expect variable name");
    if (match(TOK_ASSIGN))
        expression();
    else
        emit_byte(OP_NIL);
    consume(TOK_SEMI, "expect ';' after variable declaration");
    define_variable(global);
}

static void expression_statement(void)
{
    expression();
    consume(TOK_SEMI, "expect ';' after expression");
    emit_byte(OP_POP);
}

static void print_statement(void)
{
    expression();
    consume(TOK_SEMI, "expect ';' after value");
    emit_byte(OP_PRINT);
}

static void if_statement(void)
{
    consume(TOK_LPAREN, "expect '(' after 'if'");
    expression();
    consume(TOK_RPAREN, "expect ')' after condition");

    int then_jump = emit_jump(OP_JMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    int else_jump = emit_jump(OP_JMP);
    patch_jump(then_jump);
    emit_byte(OP_POP);
    if (match(TOK_ELSE))
        statement();
    patch_jump(else_jump);
}

static void while_statement(void)
{
    int loop_start = (int)current_chunk()->count;
    consume(TOK_LPAREN, "expect '(' after 'while'");
    expression();
    consume(TOK_RPAREN, "expect ')' after condition");

    int exit_jump = emit_jump(OP_JMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    emit_loop(loop_start);
    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void for_statement(void)
{
    begin_scope();
    consume(TOK_LPAREN, "expect '(' after 'for'");

    if (match(TOK_SEMI)) {
        /* no initializer */
    } else if (match(TOK_LET)) {
        var_declaration();
    } else {
        expression_statement();
    }

    int loop_start = (int)current_chunk()->count;
    int exit_jump = -1;
    if (!match(TOK_SEMI)) {
        expression();
        consume(TOK_SEMI, "expect ';' after loop condition");
        exit_jump = emit_jump(OP_JMP_IF_FALSE);
        emit_byte(OP_POP);
    }

    if (!match(TOK_RPAREN)) {
        int body_jump = emit_jump(OP_JMP);
        int inc_start = (int)current_chunk()->count;
        expression();
        emit_byte(OP_POP);
        consume(TOK_RPAREN, "expect ')' after for clauses");
        emit_loop(loop_start);
        loop_start = inc_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }
    end_scope();
}

static void return_statement(void)
{
    if (current->type == TYPE_SCRIPT)
        error("cannot return from top-level code");
    if (match(TOK_SEMI)) {
        emit_return();
    } else {
        expression();
        consume(TOK_SEMI, "expect ';' after return value");
        emit_byte(OP_RETURN);
    }
}

static void synchronize(void)
{
    parser.panic_mode = false;
    while (parser.current.type != TOK_EOF) {
        if (parser.previous.type == TOK_SEMI) return;
        switch (parser.current.type) {
        case TOK_FN: case TOK_LET: case TOK_FOR: case TOK_IF:
        case TOK_WHILE: case TOK_RETURN: case TOK_PRINT:
            return;
        default: ;
        }
        advance();
    }
}

static void statement(void)
{
    if (match(TOK_PRINT)) {
        print_statement();
    } else if (match(TOK_LBRACE)) {
        begin_scope();
        block();
        end_scope();
    } else if (match(TOK_IF)) {
        if_statement();
    } else if (match(TOK_WHILE)) {
        while_statement();
    } else if (match(TOK_FOR)) {
        for_statement();
    } else if (match(TOK_RETURN)) {
        return_statement();
    } else {
        expression_statement();
    }
}

static void declaration(void)
{
    if (match(TOK_FN))
        fn_declaration();
    else if (match(TOK_LET))
        var_declaration();
    else
        statement();

    if (parser.panic_mode)
        synchronize();
}

/* ------------------------------------------------------------------ */
ObjFunction *compile(Heap *h, const char *source, size_t len)
{
    cheap = h;
    lexer_init(&parser.lex, source, len);
    parser.had_error  = false;
    parser.panic_mode = false;
    current = NULL;

    Compiler compiler;
    init_compiler(&compiler, TYPE_SCRIPT);

    advance();
    while (!match(TOK_EOF))
        declaration();

    ObjFunction *fn = end_compiler();
    return parser.had_error ? NULL : fn;
}

void mark_compiler_roots(Heap *h)
{
    for (Compiler *c = current; c != NULL; c = c->enclosing)
        gc_mark_object(h, (Obj *)c->function);
}
