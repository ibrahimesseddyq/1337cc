/**
 * @file lexer.c
 * @brief Lexical analyzer for the compiler frontend.
 *
 * This module tokenizes input provided via a lex_process with pluggable
 * character source functions (`peek_char`, `next_char`, `push_char`). It
 * supports:
 *  - identifiers and keywords
 *  - numbers (decimal, binary 'b', hex 'x' postfix forms)
 *  - strings and character literals (with simple escape handling)
 *  - single- and multi-line comments
 *  - operators and symbols
 *  - simple tracking of parentheses/expression context to capture "between-brackets"
 *    buffers for expressions (used by downstream parsing).
 *
 * The implementation relies on a small Buffer helper and a Vector helper.
 * Many helper functions are declared static/private to this translation unit.
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include "helpers/buffer.h"
 #include <string.h>
 #include <assert.h>
 #include <ctype.h>
 
 /**
  * @brief Helper macro to consume characters while `exp` evaluates true.
  *
  * Writes consumed characters into `buffer` (via buffer_write) and advances
  * the input using nextc(). `c` must be a char variable in scope.
  */
 #define LEX_GETC_IF(buffer, c, exp)     \
     for (c = peekc(); exp; c = peekc()) \
     {                                   \
         buffer_write(buffer, c);        \
         nextc();                        \
     }
 
 /* Forward declarations for exported/used routines */
 struct token *read_next_token();
 bool lex_is_in_expression();
 
 /* Module state (single active lex process at a time) */
 static struct lex_process *lex_process;
 static struct token tmp_token;
 
 /* --- Low-level character access helpers ---------------------------------- */
 
 /**
  * @brief Peek the next character from the lex source without consuming it.
  *
  * Delegates to the current lex_process function implementation.
  *
  * @return Next character (or EOF sentinel).
  */
 static char peekc()
 {
     return lex_process->function->peek_char(lex_process);
 }
 
 /**
  * @brief Consume the next character from the lex source.
  *
  * Updates the lex_process file position (line/col). If the lexer is currently
  * inside an expression (parentheses nesting > 0), the consumed character is
  * appended to the parentheses_buffer so downstream code can access the
  * expression contents.
  *
  * @return The consumed character.
  */
 static char nextc()
 {
     char c = lex_process->function->next_char(lex_process);
     if (lex_is_in_expression())
     {
         buffer_write(lex_process->parentheses_buffer, c);
     }
     lex_process->pos.col += 1;
     if (c == '\n')
     {
         lex_process->pos.line += 1;
         lex_process->pos.col = 1;
     }
 
     return c;
 }
 
 /**
  * @brief Push a character back into the lex stream.
  *
  * Delegates to the lex_process push_char function (used when operator parsing
  * overshot or for other small corrections).
  *
  * @param c Character to push back.
  */
 static void pushc(char c)
 {
     lex_process->function->push_char(lex_process, c);
 }
 
 /**
  * @brief Assert that the next character matches the expected one.
  *
  * Consumes the next character via nextc() and asserts equality. Returns the
  * consumed character (for convenience).
  *
  * @param c Expected character.
  * @return Consumed character (equal to `c`).
  */
 static char assert_next_char(char c)
 {
     char next_c = nextc();
     assert(c == next_c);
     return next_c;
 }
 
 /**
  * @brief Return the current file position tracked by the lexer.
  *
  * @return A copy of lex_process->pos describing the current line/column/filename.
  */
 static struct pos lex_file_position()
 {
     return lex_process->pos;
 }
 
 /* --- Token creation & small helpers ------------------------------------- */
 
 /**
  * @brief Create a token structure based on a template and attach position info.
  *
  * Copies `_token` into a module-local `tmp_token`, sets the token's position
  * to the current lexer file position, and (if inside an expression) sets the
  * `between_brackets` pointer to the current parentheses buffer contents.
  *
  * Note: returns pointer to a statically stored token (tmp_token). The caller
  * should typically push or copy token data before calling token_create again.
  *
  * @param _token Pointer to a partially initialized token template.
  * @return Pointer to the populated token (module-local storage).
  */
 struct token *token_create(struct token *_token)
 {
     memcpy(&tmp_token, _token, sizeof(struct token));
     tmp_token.pos = lex_file_position();
     if (lex_is_in_expression())
     {
         tmp_token.between_brackets = buffer_ptr(lex_process->parentheses_buffer);
     }
     return &tmp_token;
 }
 
 /**
  * @brief Return the last token pushed into the lex_process token vector (or NULL).
  */
 static struct token *lexer_last_token()
 {
     return vector_back_or_null(lex_process->token_vec);
 }
 
 /**
  * @brief Handle whitespace token by marking the previous token's whitespace flag.
  *
  * If there is a previous token, its `whitespace` field is set true. Then the
  * whitespace character is consumed and tokenization continues by reading the
  * next token recursively.
  *
  * @return Next token after whitespace (or NULL).
  */
 static struct token *handle_whitespace()
 {
     struct token *last_token = lexer_last_token();
     if (last_token)
     {
         last_token->whitespace = true;
     }
 
     nextc();
     return read_next_token();
 }
 
 /* --- Number readers ----------------------------------------------------- */
 
 /**
  * @brief Read a decimal number string from the input and return an allocated C string.
  *
  * Consumes digit characters [0-9] into a temporary buffer and returns a
  * null-terminated string pointer (returned buffer is owned by the buffer helper).
  *
  * @return Pointer to NUL-terminated digit string (lives in a buffer).
  */
 const char *read_number_str()
 {
     const char *num = NULL;
     struct buffer *buffer = buffer_create();
     char c = peekc();
     LEX_GETC_IF(buffer, c, (c >= '0' && c <= '9'));
 
     buffer_write(buffer, 0x00);
     return buffer_ptr(buffer);
 }
 
 /**
  * @brief Read a decimal integer value from input and return its unsigned long long value.
  *
  * Uses read_number_str() and `atoll()` to convert the read digits.
  *
  * @return Parsed integer value.
  */
 unsigned long long read_number()
 {
     const char *s = read_number_str();
     return atoll(s);
 }
 
 /**
  * @brief Determine a number "type" based on a character following the numeric literal.
  *
  * Returns NUMBER_TYPE_NORMAL, NUMBER_TYPE_LONG (for 'L'), or NUMBER_TYPE_FLOAT (for 'f').
  *
  * @param c Character following a numeric literal.
  * @return number type enum value.
  */
 int lexer_number_type(char c)
 {
     int res = NUMBER_TYPE_NORMAL;
     if (c == 'L')
     {
         res = NUMBER_TYPE_LONG;
     }
     else if (c == 'f')
     {
         res = NUMBER_TYPE_FLOAT;
     }
 
     return res;
 }
 
 /**
  * @brief Create and return a TOKEN_TYPE_NUMBER token for the provided integer value.
  *
  * If the next character indicates a suffix (e.g., 'L' or 'f') the suffix is
  * consumed and reported in the token->num.type field.
  *
  * @param number Numeric value to store in the token.
  * @return Pointer to created token (module-local).
  */
 struct token *token_make_number_for_value(unsigned long number)
 {
     int number_type = lexer_number_type(peekc());
     if (number_type != NUMBER_TYPE_NORMAL)
     {
         nextc();
     }
     return token_create(&(struct token){.type = TOKEN_TYPE_NUMBER, .llnum = number, .num.type = number_type});
 }
 
 /**
  * @brief Read a numeric literal from input and produce a TOKEN_TYPE_NUMBER token.
  *
  * @return Pointer to the created number token.
  */
 struct token *token_make_number()
 {
     return token_make_number_for_value(read_number());
 }
 
 /* --- Strings ------------------------------------------------------------ */
 
 /**
  * @brief Read a string bounded by start_delim and end_delim and create a TOKEN_TYPE_STRING.
  *
  * Handles a simple escape-pass (the implementation currently just skips over a
  * backslash and continues; it does not decode escapes fully in this function).
  *
  * @param start_delim Opening delimiter (e.g., '"').
  * @param end_delim   Closing delimiter (e.g., '"').
  * @return Pointer to the created string token.
  */
 static struct token *token_make_string(char start_delim, char end_delim)
 {
     struct buffer *buf = buffer_create();
     assert(nextc() == start_delim);
     char c = nextc();
     for (; c != end_delim && c != EOF; c = nextc())
     {
         if (c == '\\')
         {
             // We need to handle an escape character.
             continue;
         }
 
         buffer_write(buf, c);
     }
 
     buffer_write(buf, 0x00);
     return token_create(&(struct token){.type = TOKEN_TYPE_STRING, .sval = buffer_ptr(buf)});
 }
 
 /* --- Operator utilities ----------------------------------------------- */
 
 /**
  * @brief Some operators are treated as single-char tokens always.
  *
  * Operators like '(', '[', ',', '.' are easier to handle as single tokens.
  */
 static bool op_treated_as_one(char op)
 {
     return op == '(' || op == '[' || op == ',' || op == '.' || op == '*' || op == '?';
 }
 
 /**
  * @brief Check whether a character is one of the recognised single-operator characters.
  *
  * This set is a superset used for two-character operator detection (e.g., '==', '+=').
  */
 static bool is_single_operator(char op)
 {
     return op == '+' ||
            op == '-' ||
            op == '/' ||
            op == '*' ||
            op == '=' ||
            op == '>' ||
            op == '<' ||
            op == '|' ||
            op == '&' ||
            op == '^' ||
            op == '%' ||
            op == '!' ||
            op == '(' ||
            op == '[' ||
            op == ',' ||
            op == '.' ||
            op == '~' ||
            op == '?';
 }
 
 /**
  * @brief Validate whether a string is one of the known operator tokens supported by the lexer.
  *
  * @param op Null-terminated operator string to validate (e.g., "+", "==", ">>").
  * @return true if operator is recognized, false otherwise.
  */
 bool op_valid(const char *op)
 {
     return S_EQ(op, "+") ||
            S_EQ(op, "-") ||
            S_EQ(op, "*") ||
            S_EQ(op, "/") ||
            S_EQ(op, "!") ||
            S_EQ(op, "^") ||
            S_EQ(op, "+=") ||
            S_EQ(op, "-=") ||
            S_EQ(op, "*=") ||
            S_EQ(op, "/=") ||
            S_EQ(op, ">>") ||
            S_EQ(op, "<<") ||
            S_EQ(op, ">=") ||
            S_EQ(op, "<=") ||
            S_EQ(op, ">") ||
            S_EQ(op, "<") ||
            S_EQ(op, "||") ||
            S_EQ(op, "&&") ||
            S_EQ(op, "|") ||
            S_EQ(op, "&") ||
            S_EQ(op, "++") ||
            S_EQ(op, "--") ||
            S_EQ(op, "=") ||
            S_EQ(op, "!=") ||
            S_EQ(op, "==") ||
            S_EQ(op, "->") ||
            S_EQ(op, "(") ||
            S_EQ(op, "[") ||
            S_EQ(op, ",") ||
            S_EQ(op, ".") ||
            S_EQ(op, "...") ||
            S_EQ(op, "~") ||
            S_EQ(op, "?") ||
            S_EQ(op, "%");
 }
 
 /**
  * @brief Helper to push back all but the first character of an operator buffer.
  *
  * Used when a multi-character operator candidate was assembled but determined
  * invalid: the extra characters (except the first) are pushed back into the
  * input stream in reverse order so subsequent tokenization sees them in order.
  *
  * @param buffer Buffer containing the operator string (null-terminated).
  */
 void read_op_flush_back_keep_first(struct buffer *buffer)
 {
     const char *data = buffer_ptr(buffer);
     int len = buffer->len;
     for (int i = len - 1; i >= 1; i--)
     {
         if (data[i] == 0x00)
         {
             continue;
         }
 
         pushc(data[i]);
     }
 }
 
 /**
  * @brief Read an operator token (single- or two-char) and validate it.
  *
  * The logic:
  *  - Consume one char (first operator char).
  *  - If the char can form a two-char operator, peek and possibly consume the next.
  *  - If we assembled a two-char op but it's not valid, push the extra char(s) back.
  *  - If operator is '(' then start expression recording (lex_new_expression()).
  *
  * @return pointer to a NUL-terminated operator string (lives in buffer).
  */
 const char *read_op()
 {
     bool single_operator = true;
     char op = nextc();
     struct buffer *buffer = buffer_create();
     buffer_write(buffer, op);
 
     if (!op_treated_as_one(op))
     {
         op = peekc();
         if (is_single_operator(op))
         {
             buffer_write(buffer, op);
             nextc();
             single_operator = false;
         }
     }
 
     // NULL TERMINATOR
     buffer_write(buffer, 0x00);
     char *ptr = buffer_ptr(buffer);
     if (!single_operator)
     {
         if (!op_valid(ptr))
         {
             read_op_flush_back_keep_first(buffer);
             ptr[1] = 0x00;
         }
     }
     else if (!op_valid(ptr))
     {
         compiler_error(lex_process->compiler, "The operator %s is not valid\n", ptr);
     }
 
     return ptr;
 }
 
 /* --- Expression tracking helpers --------------------------------------- */
 
 /**
  * @brief Notify the lexer that a new expression context has been opened (e.g., '(' consumed).
  *
  * Increments the parentheses nesting counter and allocates a parentheses_buffer
  * to record characters seen while inside the outermost expression.
  */
 static void lex_new_expression()
 {
     lex_process->current_expression_count++;
     if (lex_process->current_expression_count == 1)
     {
         lex_process->parentheses_buffer = buffer_create();
     }
 }
 
 /**
  * @brief Notify the lexer that an expression context has been closed (e.g., ')' consumed).
  *
  * Decrements the nesting counter and validates that closing occurs in balance.
  */
 static void lex_finish_expression()
 {
     lex_process->current_expression_count--;
     if (lex_process->current_expression_count < 0)
     {
         compiler_error(lex_process->compiler, "You closed an expression that you never opened\n");
     }
 }
 
 /**
  * @brief Return true if the lexer is currently inside any parentheses expression.
  *
  * @return true if parentheses nesting count > 0.
  */
 bool lex_is_in_expression()
 {
     return lex_process->current_expression_count > 0;
 }
 
 /* --- Keyword/type helpers --------------------------------------------- */
 
 /**
  * @brief Test whether a string is a datatype keyword (void, char, int, struct, union, etc).
  *
  * @param str Null-terminated C string to test.
  * @return true if it matches a datatype/reserved keyword.
  */
 bool keyword_is_datatype(const char *str)
 {
     return S_EQ(str, "void") ||
            S_EQ(str, "char") ||
            S_EQ(str, "int") ||
            S_EQ(str, "short") ||
            S_EQ(str, "float") ||
            S_EQ(str, "double") ||
            S_EQ(str, "long") ||
            S_EQ(str, "struct") ||
            S_EQ(str, "union");
 }
 
 /**
  * @brief Test whether a string is a recognized keyword by the lexer.
  *
  * This list includes type modifiers, storage specifiers, control-flow and other
  * language keywords relevant to this compiler subset.
  *
  * @param str Null-terminated C string to test.
  * @return true if the string is a recognized keyword.
  */
 bool is_keyword(const char *str)
 {
     return S_EQ(str, "unsigned") ||
            S_EQ(str, "signed") ||
            S_EQ(str, "char") ||
            S_EQ(str, "short") ||
            S_EQ(str, "int") ||
            S_EQ(str, "long") ||
            S_EQ(str, "float") ||
            S_EQ(str, "double") ||
            S_EQ(str, "void") ||
            S_EQ(str, "struct") ||
            S_EQ(str, "union") ||
            S_EQ(str, "static") ||
            S_EQ(str, "__ignore_typecheck") ||
            S_EQ(str, "return") ||
            S_EQ(str, "include") ||
            S_EQ(str, "sizeof") ||
            S_EQ(str, "if") ||
            S_EQ(str, "else") ||
            S_EQ(str, "while") ||
            S_EQ(str, "for") ||
            S_EQ(str, "do") ||
            S_EQ(str, "break") ||
            S_EQ(str, "continue") ||
            S_EQ(str, "switch") ||
            S_EQ(str, "case") ||
            S_EQ(str, "default") ||
            S_EQ(str, "goto") ||
            S_EQ(str, "typedef") ||
            S_EQ(str, "const") ||
            S_EQ(str, "extern") ||
            S_EQ(str, "restrict");
 }
 
 /* --- Token constructors: operators, comments, symbols, identifiers --- */
 
 /**
  * @brief Create an operator token or an include-string token when encountering '<' after include.
  *
  * If the upcoming token is an include directive, `< ... >` is handled as a string token.
  * Otherwise read an operator string via read_op() and create a TOKEN_TYPE_OPERATOR token.
  * If the operator starts with '(' the lexer begins expression recording.
  *
  * @return Pointer to the created token.
  */
 static struct token *token_make_operator_or_string()
 {
     char op = peekc();
     if (op == '<')
     {
         struct token *last_token = lexer_last_token();
         if (token_is_keyword(last_token, "include"))
         {
             return token_make_string('<', '>');
         }
     }
 
     struct token *token = token_create(&(struct token){.type = TOKEN_TYPE_OPERATOR, .sval = read_op()});
     if (op == '(')
     {
         lex_new_expression();
     }
 
     return token;
 }
 
 /**
  * @brief Read a single-line comment (//...) and return a COMMENT token.
  *
  * Consumes characters until newline or EOF and returns a token with the comment text.
  */
 struct token *token_make_one_line_comment()
 {
     struct buffer *buffer = buffer_create();
     char c = 0;
     LEX_GETC_IF(buffer, c, c != '\n' && c != EOF);
     return token_create(&(struct token){.type = TOKEN_TYPE_COMMENT, .sval = buffer_ptr(buffer)});
 }
 
 /**
  * @brief Read a C-style multiline comment (/* ... * /) and return a COMMENT token.
  *
  * Ensures the comment is closed properly; otherwise emits a compiler error.
  */
 struct token *token_make_multiline_comment()
 {
     struct buffer *buffer = buffer_create();
     char c = 0;
     while (1)
     {
         LEX_GETC_IF(buffer, c, c != '*' && c != EOF);
         if (c == EOF)
         {
             compiler_error(lex_process->compiler, "You did not close this multiline comment\n");
         }
         else if (c == '*')
         {
             // Skip the *
             nextc();
 
             if (peekc() == '/')
             {
                 nextc();
                 break;
             }
         }
     }
     return token_create(&(struct token){.type = TOKEN_TYPE_COMMENT, .sval = buffer_ptr(buffer)});
 }
 
 /**
  * @brief Handle a potential comment starting with '/'.
  *
  * If the sequence is '//' or '/*' the corresponding comment token is produced.
  * Otherwise, the '/' is pushed back and the token is handled as an operator.
  *
  * @return Pointer to a COMMENT token or NULL if no comment was recognized.
  */
 struct token *handle_comment()
 {
     char c = peekc();
     if (c == '/')
     {
         nextc();
         if (peekc() == '/')
         {
             nextc();
             return token_make_one_line_comment();
         }
         else if (peekc() == '*')
         {
             nextc();
             return token_make_multiline_comment();
         }
 
         pushc('/');
         return token_make_operator_or_string();
     }
 
     return NULL;
 }
 
 /**
  * @brief Create a SYMBOL token for the current character.
  *
  * Consumes one character and produces a TOKEN_TYPE_SYMBOL. If the symbol is ')'
  * this also signals the end of an expression context (lex_finish_expression()).
  *
  * @return Pointer to the created symbol token.
  */
 static struct token *token_make_symbol()
 {
     char c = nextc();
     if (c == ')')
     {
         lex_finish_expression();
     }
 
     struct token *token = token_create(&(struct token){.type = TOKEN_TYPE_SYMBOL, .cval = c});
     return token;
 }
 
 /**
  * @brief Read an identifier or keyword token.
  *
  * Consumes characters matching [A-Za-z0-9_] and returns either a TOKEN_TYPE_KEYWORD
  * (if the resulting string is in the lexer keyword list) or TOKEN_TYPE_IDENTIFIER.
  *
  * @return Pointer to the created identifier/keyword token.
  */
 static struct token *token_make_identifier_or_keyword()
 {
     struct buffer *buffer = buffer_create();
     char c = 0;
     LEX_GETC_IF(buffer, c, (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
 
     // null terminator
     buffer_write(buffer, 0x00);
 
     // Check if this is a keyword
     if (is_keyword(buffer_ptr(buffer)))
     {
         return token_create(&(struct token){.type = TOKEN_TYPE_KEYWORD, .sval = buffer_ptr(buffer)});
     }
 
     return token_create(&(struct token){.type = TOKEN_TYPE_IDENTIFIER, .sval = buffer_ptr(buffer)});
 }
 
 /**
  * @brief Attempt to read a special token starting with an alphabetic character.
  *
  * Returns an identifier/keyword token if appropriate; otherwise NULL.
  */
 struct token *read_special_token()
 {
     char c = peekc();
     if (isalpha(c) || c == '_')
     {
         return token_make_identifier_or_keyword();
     }
 
     return NULL;
 }
 
 /**
  * @brief Create a NEWLINE token by consuming the newline character.
  *
  * @return Pointer to the created NEWLINE token.
  */
 struct token *token_make_newline()
 {
     nextc();
     return token_create(&(struct token){.type = TOKEN_TYPE_NEWLINE});
 }
 
 /**
  * @brief Translate common escape sequences for character literals into actual characters.
  *
  * Accepts 'n', '\\', 't', '\'' and returns their corresponding character values.
  *
  * @param c Escape letter (e.g., 'n' for newline).
  * @return Decoded character value (or 0 if unknown).
  */
 char lex_get_escaped_char(char c)
 {
     char co = 0;
     switch (c)
     {
     case 'n':
         co = '\n';
         break;
     case '\\':
         co = '\\';
         break;
 
     case 't':
         co = '\t';
         break;
 
     case '\'':
         co = '\'';
         break;
     }
     return co;
 }
 
 /**
  * @brief Remove the last token pushed to the lex_process token vector.
  *
  * Utility used when a numeric base prefix is detected after a zero literal (e.g., 0x).
  */
 void lexer_pop_token()
 {
     vector_pop(lex_process->token_vec);
 }
 
 /* --- Hex & binary helpers --------------------------------------------- */
 
 /**
  * @brief Test whether a character is a valid hexadecimal digit (0-9, a-f, A-F).
  *
  * @param c Character to test.
  * @return true for hex digits, false otherwise.
  */
 bool is_hex_char(char c)
 {
     c = tolower(c);
     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
 }
 
 /**
  * @brief Read a sequence of hexadecimal digits and return it as a nul-terminated string.
  *
  * @return Pointer to a buffer containing the hex digit string (owned by buffer helper).
  */
 const char *read_hex_number_str()
 {
     struct buffer *buffer = buffer_create();
     char c = peekc();
     LEX_GETC_IF(buffer, c, is_hex_char(c));
     // Write our null terminator
     buffer_write(buffer, 0x00);
     return buffer_ptr(buffer);
 }
 
 /**
  * @brief Handle numeric literal of the form 0x... and produce a number token.
  *
  * Assumes the initial '0' was already read and the next char is 'x'. Consumes
  * the 'x', reads hex digits, converts to numeric value and returns a number token.
  *
  * @return Pointer to the created number token.
  */
 struct token *token_make_special_number_hexadecimal()
 {
     // Skip the "x"
     nextc();
 
     unsigned long number = 0;
     const char *number_str = read_hex_number_str();
     number = strtol(number_str, 0, 16);
     return token_make_number_for_value(number);
 }
 
 /**
  * @brief Validate that a given string contains only '0' or '1' characters.
  *
  * Emits a compiler error if any other character is found.
  *
  * @param str Null-terminated string to validate.
  */
 void lexer_validate_binary_string(const char *str)
 {
     size_t len = strlen(str);
     for (int i = 0; i < len; i++)
     {
         if (str[i] != '0' && str[i] != '1')
         {
             compiler_error(lex_process->compiler, "This is not a valid binary number\n");
         }
     }
 }
 
 /**
  * @brief Handle numeric literal of the form 0b... (binary).
  *
  * Consumes the 'b', reads digits, validates they are binary, converts and returns number token.
  *
  * @return Pointer to the created number token.
  */
 struct token *token_make_special_number_binary()
 {
     // Skip the "b"
     nextc();
 
     unsigned long number = 0;
     const char *number_str = read_number_str();
     lexer_validate_binary_string(number_str);
     number = strtol(number_str, 0, 2);
     return token_make_number_for_value(number);
 }
 
 /**
  * @brief Handle special number sequences that follow a leading zero.
  *
  * If the last token is not a zero literal (TOKEN_TYPE_NUMBER with llnum == 0),
  * fall back to identifier/keyword handling. Otherwise pop the zero token and
  * handle '0x...' and '0b...' style numeric forms.
  *
  * @return Pointer to the created token (hex/binary number) or fallback token.
  */
 struct token *token_make_special_number()
 {
     struct token *token = NULL;
     struct token *last_token = lexer_last_token();
     if (!last_token || !(last_token->type == TOKEN_TYPE_NUMBER && last_token->llnum == 0))
     {
         return token_make_identifier_or_keyword();
     }
 
     lexer_pop_token();
 
     char c = peekc();
     if (c == 'x')
     {
         token = token_make_special_number_hexadecimal();
     }
     else if (c == 'b')
     {
         token = token_make_special_number_binary();
     }
 
     return token;
 }
 
 /**
  * @brief Read a single-quoted character literal (e.g., 'a' or '\n') and produce a NUMBER token.
  *
  * Decodes simple escapes and verifies the closing quote is present.
  *
  * @return Pointer to the created token containing the character code in cval.
  */
 struct token *token_make_quote()
 {
     assert_next_char('\'');
     char c = nextc();
     if (c == '\\')
     {
         c = nextc();
         c = lex_get_escaped_char(c);
     }
 
     if (nextc() != '\'')
     {
         compiler_error(lex_process->compiler, "You opened a quote ' but did not close it with a ' character");
     }
 
     return token_create(&(struct token){.type = TOKEN_TYPE_NUMBER, .cval = c});
 }
 
 /* --- Main token read loop --------------------------------------------- */
 
 /**
  * @brief Read the next token from the input source and return it.
  *
  * This is the central dispatch: it peeks the next character and decides which
  * specialized token maker to call (number, operator, symbol, string, comment, etc).
  *
  * Note: The switch uses higher-level macros/labels (NUMERIC_CASE, OPERATOR_CASE_EXCLUDING_DIVISION, SYMBOL_CASE)
  * that should be defined appropriately by the surrounding build (these labels map input characters to cases).
  *
  * @return Pointer to the created token or NULL on EOF.
  */
 struct token *read_next_token()
 {
     struct token *token = NULL;
     char c = peekc();
 
     token = handle_comment();
     if (token)
     {
         return token;
     }
 
     switch (c)
     {
     NUMERIC_CASE:
         token = token_make_number();
         break;
 
     OPERATOR_CASE_EXCLUDING_DIVISION:
         token = token_make_operator_or_string();
         break;
 
     SYMBOL_CASE:
         token = token_make_symbol();
         break;
 
     case 'b':
     case 'x':
         token = token_make_special_number();
         break;
 
     case '"':
         token = token_make_string('"', '"');
         break;
 
     case '\'':
         token = token_make_quote();
         break;
     // We don't care about whitespace ignore them
     case ' ':
     case '\t':
         token = handle_whitespace();
         break;
 
     case '\n':
         token = token_make_newline();
         break;
     case EOF:
         // We have finished lexical analysis on the file
         break;
 
     default:
         token = read_special_token();
         if (!token)
         {
             compiler_error(lex_process->compiler, "Unexpected token\n");
         }
     }
     return token;
 }
 
 /**
  * @brief Top-level lexer entrypoint.
  *
  * Initializes expression tracking and parentheses buffer, assigns the global
  * lex_process pointer to `process`, and repeatedly reads tokens pushing them
  * into process->token_vec until EOF.
  *
  * @param process Lexing process with configured function table and token vector.
  * @return LEXICAL_ANALYSIS_ALL_OK on success.
  */
 int lex(struct lex_process *process)
 {
     process->current_expression_count = 0;
     process->parentheses_buffer = NULL;
     lex_process = process;
     process->pos.filename = process->compiler->cfile.abs_path;
 
     struct token *token = read_next_token();
     while (token)
     {
         vector_push(process->token_vec, token);
         token = read_next_token();
     }
     return LEXICAL_ANALYSIS_ALL_OK;
 }
 
 /* --- Small lexer backends for tokenizing from a memory buffer ------------ */
 
 /**
  * @brief next_char implementation for a lexer that reads from a buffer.
  *
  * Reads the next byte from the buffer passed as lex_process private data.
  */
 char lexer_string_buffer_next_char(struct lex_process *process)
 {
     struct buffer *buf = lex_process_private(process);
     return buffer_read(buf);
 }
 
 /**
  * @brief peek_char implementation for a lexer that reads from a buffer.
  *
  * Peeks the next byte from the buffer passed as private data.
  */
 char lexer_string_buffer_peek_char(struct lex_process *process)
 {
     struct buffer *buf = lex_process_private(process);
     return buffer_peek(buf);
 }
 
 /**
  * @brief push_char implementation for a lexer backed by a buffer.
  *
  * Writes the pushed char back into the private buffer (used for lookahead corrections).
  */
 void lexer_string_buffer_push_char(struct lex_process *process, char c)
 {
     struct buffer *buf = lex_process_private(process);
     buffer_write(buf, c);
 }
 
 /**
  * @brief Function table implementing the lex_process functions for a buffer source.
  */
 struct lex_process_functions lexer_string_buffer_functions = {
     .next_char = lexer_string_buffer_next_char,
     .peek_char = lexer_string_buffer_peek_char,
     .push_char = lexer_string_buffer_push_char
 };
 
 /**
  * @brief Build tokens for a given in-memory string (helper for tests or small snippets).
  *
  * Creates a buffer with the provided string, constructs a lex_process backed by
  * that buffer and runs lex() to populate tokens.
  *
  * @param compiler Compile process pointer (for error reporting and pos.filename).
  * @param str      Null-terminated input source string.
  * @return Pointer to a newly created lex_process containing tokens (or NULL on error).
  */
 struct lex_process *tokens_build_for_string(struct compile_process *compiler, const char *str)
 {
     struct buffer *buffer = buffer_create();
     buffer_printf(buffer, str);
     struct lex_process *lex_process = lex_process_create(compiler, &lexer_string_buffer_functions, buffer);
     if (!lex_process)
     {
         return NULL;
     }
 
     if (lex(lex_process) != LEXICAL_ANALYSIS_ALL_OK)
     {
         return NULL;
     }
 
     return lex_process;
 }
 