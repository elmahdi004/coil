/*
 * lexer.c -- Coil source tokenizer
 */

#include "lexer.h"
#include <string.h>
#include <stdlib.h>

void lexer_init(Lexer *lx, const char *source, size_t len)
{
    lx->src  = source;
    lx->cur  = source;
    lx->end  = source + len;
    lx->line = 1;
}

static int at_end(const Lexer *lx) { return lx->cur >= lx->end; }

static char peek(const Lexer *lx) { return at_end(lx) ? '\0' : *lx->cur; }

static char peek2(const Lexer *lx)
{
    return (lx->cur + 1 >= lx->end) ? '\0' : lx->cur[1];
}

static char advance(Lexer *lx)
{
    char c = *lx->cur++;
    if (c == '\n') lx->line++;
    return c;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) { return is_alpha(c) || is_digit(c); }

/* build a token of `type` spanning [start, lx->cur) */
static Token make(const Lexer *lx, TokType type, const char *start)
{
    Token t;
    t.type   = type;
    t.start  = start;
    t.length = (size_t)(lx->cur - start);
    t.line   = lx->line;
    t.ival   = 0;
    t.fval   = 0.0;
    return t;
}

/* an error token carries a static message in start/length */
static Token error_tok(const Lexer *lx, const char *msg)
{
    Token t;
    t.type   = TOK_ERROR;
    t.start  = msg;
    t.length = strlen(msg);
    t.line   = lx->line;
    t.ival   = 0;
    t.fval   = 0.0;
    return t;
}

static void skip_trivia(Lexer *lx)
{
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '#') {
            while (!at_end(lx) && peek(lx) != '\n')
                advance(lx);
        } else {
            break;
        }
    }
}

/* keyword lookup over an identifier lexeme */
static TokType keyword(const char *s, size_t n)
{
    struct { const char *kw; TokType t; } table[] = {
        { "let", TOK_LET }, { "fn", TOK_FN }, { "return", TOK_RETURN },
        { "if", TOK_IF }, { "else", TOK_ELSE }, { "while", TOK_WHILE },
        { "for", TOK_FOR }, { "and", TOK_AND }, { "or", TOK_OR },
        { "print", TOK_PRINT }, { "true", TOK_TRUE }, { "false", TOK_FALSE },
        { "nil", TOK_NIL },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (strlen(table[i].kw) == n && memcmp(table[i].kw, s, n) == 0)
            return table[i].t;
    return TOK_IDENT;
}

static Token number(Lexer *lx, const char *start)
{
    int is_float = 0;

    while (is_digit(peek(lx)))
        advance(lx);

    if (peek(lx) == '.' && is_digit(peek2(lx))) {
        is_float = 1;
        advance(lx);                 /* consume '.' */
        while (is_digit(peek(lx)))
            advance(lx);
    }

    if (peek(lx) == 'e' || peek(lx) == 'E') {
        is_float = 1;
        advance(lx);
        if (peek(lx) == '+' || peek(lx) == '-')
            advance(lx);
        while (is_digit(peek(lx)))
            advance(lx);
    }

    Token t = make(lx, is_float ? TOK_FLOAT : TOK_INT, start);

    /* decode into a NUL-terminated scratch buffer */
    char buf[64];
    size_t n = t.length < sizeof(buf) - 1 ? t.length : sizeof(buf) - 1;
    memcpy(buf, start, n);
    buf[n] = '\0';

    if (is_float)
        t.fval = strtod(buf, NULL);
    else
        t.ival = (int64_t)strtoll(buf, NULL, 10);
    return t;
}

static Token string(Lexer *lx)
{
    const char *content = lx->cur;   /* first byte after the opening quote */

    while (!at_end(lx) && peek(lx) != '"') {
        char c = advance(lx);
        if (c == '\\' && !at_end(lx))
            advance(lx);             /* skip the escaped byte; decoded later */
    }

    if (at_end(lx))
        return error_tok(lx, "unterminated string");

    Token t;
    t.type   = TOK_STRING;
    t.start  = content;
    t.length = (size_t)(lx->cur - content);   /* raw, between the quotes */
    t.line   = lx->line;
    t.ival   = 0;
    t.fval   = 0.0;

    advance(lx);                     /* closing quote */
    return t;
}

static Token ident(Lexer *lx, const char *start)
{
    while (is_alnum(peek(lx)))
        advance(lx);
    Token t = make(lx, TOK_IDENT, start);
    t.type = keyword(start, t.length);
    return t;
}

/* emit a one- or two-character operator token */
static Token op2(Lexer *lx, const char *start, char next,
                 TokType two, TokType one)
{
    if (peek(lx) == next) {
        advance(lx);
        return make(lx, two, start);
    }
    return make(lx, one, start);
}

Token lexer_next(Lexer *lx)
{
    skip_trivia(lx);

    const char *start = lx->cur;
    if (at_end(lx))
        return make(lx, TOK_EOF, start);

    char c = advance(lx);

    if (is_digit(c)) return number(lx, start);
    if (is_alpha(c)) return ident(lx, start);

    switch (c) {
    case '(': return make(lx, TOK_LPAREN, start);
    case ')': return make(lx, TOK_RPAREN, start);
    case '{': return make(lx, TOK_LBRACE, start);
    case '}': return make(lx, TOK_RBRACE, start);
    case '[': return make(lx, TOK_LBRACKET, start);
    case ']': return make(lx, TOK_RBRACKET, start);
    case ',': return make(lx, TOK_COMMA, start);
    case ';': return make(lx, TOK_SEMI, start);
    case '.': return make(lx, TOK_DOT, start);
    case ':': return make(lx, TOK_COLON, start);
    case '+': return make(lx, TOK_PLUS, start);
    case '-': return make(lx, TOK_MINUS, start);
    case '*': return make(lx, TOK_STAR, start);
    case '/': return make(lx, TOK_SLASH, start);
    case '%': return make(lx, TOK_PERCENT, start);
    case '&': return make(lx, TOK_AMP, start);
    case '|': return make(lx, TOK_PIPE, start);
    case '^': return make(lx, TOK_CARET, start);
    case '~': return make(lx, TOK_TILDE, start);
    case '=': return op2(lx, start, '=', TOK_EQ, TOK_ASSIGN);
    case '!':
        if (peek(lx) == '=') { advance(lx); return make(lx, TOK_NE, start); }
        return make(lx, TOK_BANG, start);
    case '<':
        if (peek(lx) == '<') { advance(lx); return make(lx, TOK_SHL, start); }
        return op2(lx, start, '=', TOK_LE, TOK_LT);
    case '>':
        if (peek(lx) == '>') { advance(lx); return make(lx, TOK_SHR, start); }
        return op2(lx, start, '=', TOK_GE, TOK_GT);
    case '"': return string(lx);
    default:  break;
    }

    return error_tok(lx, "unexpected character");
}
