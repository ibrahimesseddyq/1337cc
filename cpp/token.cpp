#include "compiler.hpp"

#define PRIMITIVE_TYPES_TOTAL 7

static const char *primitive_types[PRIMITIVE_TYPES_TOTAL] = {
    "void", "char", "short", "int", "long", "float", "double"
};

// Returns true if token is a non-null identifier token.
bool token_is_identifier(struct token *token)
{
    return token && token->type == TOKEN_TYPE_IDENTIFIER;
}

// Returns true if token is a keyword token whose string value equals value.
// Both conditions (type and string) must hold; a null token returns false.
bool token_is_keyword(struct token *token, const char *value)
{
    return token && token->type == TOKEN_TYPE_KEYWORD && S_EQ(token->sval, value);
}

// Returns true if token is a symbol token whose character value equals c.
// A null token returns false.
bool token_is_symbol(struct token *token, char c)
{
    return token && token->type == TOKEN_TYPE_SYMBOL && token->cval == c;
}

// Returns true if token is an operator token whose string value equals val.
// A null token returns false.
bool token_is_operator(struct token *token, const char *val)
{
    return token && token->type == TOKEN_TYPE_OPERATOR && S_EQ(token->sval, val);
}

// Returns true if token represents whitespace that carries no semantic meaning.
// Matches newline tokens, comment tokens, and the backslash-newline line
// continuation symbol ('\'). A null token returns false.
bool token_is_nl_or_comment_or_newline_seperator(struct token *token)
{
    if (!token)
        return false;
    return token->type == TOKEN_TYPE_NEWLINE ||
           token->type == TOKEN_TYPE_COMMENT ||
           token_is_symbol(token, '\\');
}

// Returns true if token is a keyword that names a built-in primitive type.
// Checks against the static list: void, char, short, int, long, float, double.
// A null token or a non-keyword token always returns false.
bool token_is_primitive_keyword(struct token *token)
{
    if (!token)
        return false;
    if (token->type != TOKEN_TYPE_KEYWORD)
        return false;
    for (int i = 0; i < PRIMITIVE_TYPES_TOTAL; i++)
    {
        if (S_EQ(primitive_types[i], token->sval))
            return true;
    }
    return false;
}
