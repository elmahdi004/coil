/*
 * lexer.h -- Coil source tokenizer (Milestone 1)
 *
 * This is a starting contract, not a mandate -- it is your module; reshape the
 * token set, struct layout, and API however reads best to you.  The fuzz target
 * fuzz/lexer_fuzzer.c is written against this interface, so if you change a
 * signature, tell me and I'll re-wire the harness.
 */

#ifndef COIL_LEXER_H
#define COIL_LEXER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    /* literals */
    TOK_INT, TOK_FLOAT, TOK_STRING, TOK_TRUE, TOK_FALSE, TOK_NIL,
    /* identifiers & keywords */
    TOK_IDENT, TOK_LET, TOK_FN, TOK_RETURN, TOK_IF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_AND, TOK_OR, TOK_PRINT,
    /* one/two char operators & punctuation */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN, TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMI, TOK_DOT, TOK_COLON,
    TOK_BANG,                       /* !  logical not                 */
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,   /* &  |  ^  ~           */
    TOK_SHL, TOK_SHR,               /* << >>                          */
    /* control */
    TOK_EOF, TOK_ERROR
} TokType;

typedef struct {
    TokType     type;
    const char *start;   /* points into the source buffer (not owned)        */
    size_t      length;  /* lexeme length in bytes                           */
    int         line;    /* 1-based source line                              */
    /* decoded scratch (valid per-type): */
    int64_t     ival;    /* TOK_INT                                          */
    double      fval;    /* TOK_FLOAT                                        */
} Token;

typedef struct {
    const char *src;     /* start of source                                 */
    const char *cur;     /* next byte to scan                               */
    const char *end;     /* one past the last source byte                   */
    int         line;    /* current 1-based line                            */
} Lexer;

/* Bind a lexer to source[0, len). */
void lexer_init(Lexer *lx, const char *source, size_t len);

/* Scan and return the next token.  Returns TOK_EOF repeatedly at end of input
 * and TOK_ERROR (with a message in the lexeme) on a lexical error. */
Token lexer_next(Lexer *lx);

#endif /* COIL_LEXER_H */
