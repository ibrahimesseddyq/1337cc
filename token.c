/**
 * @file token_utils.c
 * @brief Small helpers for inspecting parser `struct token` values.
 *
 * This module provides predicates used throughout the parser to test token
 * kinds and values (identifiers, keywords, symbols, operators, newline/comment
 * separators, and primitive-type keywords).
 *
 * Notes:
 *  - The array `primitive_types` lists the language's primitive type keywords
 *    used by `token_is_primitive_keyword()`.
 *  - The macro name `PRIMTIIVE_TYPES_TOTAL` matches the original code (typo
 *    retained to avoid changing behavior).
 */

 #include "compiler.h"

 #define PRIMTIIVE_TYPES_TOTAL 7
 
 /**
  * @brief List of primitive type keywords recognized by the parser.
  *
  * Used by token_is_primitive_keyword() to quickly detect builtin types.
  */
 const char* primitive_types[PRIMTIIVE_TYPES_TOTAL] = {
     "void", "char", "short", "int", "long", "float", "double"
 };
 
 /**
  * @brief True when the token is a valid identifier token.
  *
  * @param token Token to test (may be NULL).
  * @return true if token != NULL and token->type == TOKEN_TYPE_IDENTIFIER.
  */
 bool token_is_identifier(struct token* token)
 {
     return token && token->type == TOKEN_TYPE_IDENTIFIER;
 }
 
 /**
  * @brief True when the token is a keyword with the given string value.
  *
  * Compares the token's sval with `value` using S_EQ (string equality helper).
  *
  * @param token Token to test (may be NULL).
  * @param value Keyword string to match.
  * @return true if token is a keyword and token->sval equals value.
  */
 bool token_is_keyword(struct token *token, const char *value)
 {
     return token && token->type == TOKEN_TYPE_KEYWORD && S_EQ(token->sval, value);
 }
 
 /**
  * @brief True when the token is a symbol matching the provided character.
  *
  * @param token Token to test (may be NULL).
  * @param c     Symbol character to match (e.g., '{', '}', ';').
  * @return true if token is a symbol and token->cval == c.
  */
 bool token_is_symbol(struct token* token, char c)
 {
     return token && token->type == TOKEN_TYPE_SYMBOL && token->cval == c;
 }
 
 /**
  * @brief True when the token is an operator whose string equals `val`.
  *
  * @param token Token to test (may be NULL).
  * @param val   Operator string to match (e.g., "+", "=", "==").
  * @return true if token is an operator and token->sval equals val.
  */
 bool token_is_operator(struct token* token, const char* val)
 {
     return token && token->type == TOKEN_TYPE_OPERATOR && S_EQ(token->sval, val);
 }
 
 /**
  * @brief True when the token is a newline, comment, or line-continuation symbol.
  *
  * The parser uses this to treat these tokens as separators in contexts where
  * whitespace/comments/newlines are significant or should be skipped.
  *
  * @param token Token to test (may be NULL).
  * @return true when token is newline, comment, or a backslash symbol ('\').
  */
 bool token_is_nl_or_comment_or_newline_seperator(struct token *token)
 {
     if (!token)
         return false;
         
     return token->type == TOKEN_TYPE_NEWLINE ||
            token->type == TOKEN_TYPE_COMMENT ||
            token_is_symbol(token, '\\');
 }
 
 /**
  * @brief True when the token is a primitive type keyword (void/char/short/int/...).
  *
  * Scans the `primitive_types` table to detect whether the token's sval matches a
  * builtin primitive type name.
  *
  * @param token Token to test (may be NULL).
  * @return true if token is a KEYWORD and one of the primitive type names.
  */
 bool token_is_primitive_keyword(struct token* token)
 {
     if (!token)
         return false;
 
     if (token->type != TOKEN_TYPE_KEYWORD)
         return false;
 
     for (int i = 0; i < PRIMTIIVE_TYPES_TOTAL; i++)
     {
         if (S_EQ(primitive_types[i], token->sval))
             return true;
     }
 
     return false;
 }
 