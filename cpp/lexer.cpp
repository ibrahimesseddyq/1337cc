#include "compiler.hpp"
#include "helpers/vector.hpp"
#include "helpers/buffer.hpp"
#include <cstring>
#include <cassert>
#include <cctype>
#include <cstdlib>

// LEX_GETC_IF - convenience macro that repeatedly peeks at the next character,
// writes it into 'buffer', and advances the input while 'exp' holds true.
// 'c' is updated to the newly peeked character on each iteration so callers
// can inspect the terminating character after the loop.
#define LEX_GETC_IF(buffer, c, exp)     \
    for (c = peekc(); exp; c = peekc()) \
    {                                   \
        buffer_write(buffer, c);        \
        nextc();                        \
    }

// ============================================================================
// Lexer class
//
// Encapsulates all lexer state and logic for a single lex pass.  It holds a
// pointer to the lex_process that owns the I/O callbacks, the source position
// counter, and the output token vector.  One Lexer object is created per call
// to lex() and is destroyed when the call returns.
//
// The main entry point is run(), which repeatedly calls read_next_token() and
// pushes each returned token into m_process->token_vec until EOF.
// ============================================================================
class Lexer
{
public:
    explicit Lexer(struct lex_process *process)
        : m_process(process), m_tmp_token{}
    {
    }

    int run();

private:
    struct lex_process *m_process;
    struct token m_tmp_token;

    // --- character I/O ---
    char peekc()  { return m_process->function->peek_char(m_process); }
    char nextc();
    void pushc(char c) { m_process->function->push_char(m_process, c); }
    char assert_next_char(char c);

    // --- position ---
    struct pos lex_file_position() { return m_process->pos; }

    // --- expression tracking ---
    void lex_new_expression();
    void lex_finish_expression();
    bool lex_is_in_expression() { return m_process->current_expression_count > 0; }

    // --- token helpers ---
    struct token *token_create(struct token *t);
    struct token *lexer_last_token();
    struct token *handle_whitespace();

    // --- readers ---
    const char *read_number_str();
    unsigned long long read_number();
    int lexer_number_type(char c);
    struct token *token_make_number_for_value(unsigned long number);
    struct token *token_make_number();
    struct token *token_make_string(char start_delim, char end_delim);

    // --- operators ---
    bool op_treated_as_one(char op);
    bool is_single_operator(char op);
    bool op_valid(const char *op);
    void read_op_flush_back_keep_first(struct buffer *buf);
    const char *read_op();
    struct token *token_make_operator_or_string();

    // --- comments ---
    struct token *token_make_one_line_comment();
    struct token *token_make_multiline_comment();
    struct token *handle_comment();

    // --- symbols / identifiers ---
    struct token *token_make_symbol();
    struct token *token_make_identifier_or_keyword();
    struct token *read_special_token();
    struct token *token_make_newline();

    // --- character literals ---
    char lex_get_escaped_char(char c);
    struct token *token_make_quote();

    // --- hex / binary numbers ---
    void lexer_pop_token();
    bool is_hex_char(char c);
    const char *read_hex_number_str();
    struct token *token_make_special_number_hexadecimal();
    void lexer_validate_binary_string(const char *str);
    struct token *token_make_special_number_binary();
    struct token *token_make_special_number();

    // --- main loop ---
    struct token *read_next_token();
};

// ============================================================================
// Lexer method implementations
// ============================================================================

// Consumes and returns the next character from the source via the I/O callback.
// If the lexer is currently inside a parenthesised expression
// (lex_is_in_expression() is true), the character is also appended to
// m_process->parentheses_buffer so that the full expression text is available
// on the resulting token.  Advances the column counter and, on a newline,
// resets it to 1 and increments the line counter.
// Returns the consumed character.
char Lexer::nextc()
{
    char c = m_process->function->next_char(m_process);
    if (lex_is_in_expression())
        buffer_write(m_process->parentheses_buffer, c);
    m_process->pos.col += 1;
    if (c == '\n')
    {
        m_process->pos.line += 1;
        m_process->pos.col = 1;
    }
    return c;
}

// Consumes the next character and asserts that it equals 'c'.
// Used to consume mandatory delimiter characters (e.g. opening quotes) while
// catching malformed input early.  Triggers an assertion failure if the actual
// character does not match 'c'.
// Returns the consumed character.
char Lexer::assert_next_char(char c)
{
    char next_c = nextc();
    assert(c == next_c);
    return next_c;
}

// Opens a new parenthesised expression context.
// Increments the nesting counter.  When the counter transitions from 0 to 1 a
// fresh parentheses_buffer is allocated; subsequent characters consumed via
// nextc() will be written into that buffer until the matching close is seen.
void Lexer::lex_new_expression()
{
    m_process->current_expression_count++;
    if (m_process->current_expression_count == 1)
        m_process->parentheses_buffer = buffer_create();
}

// Closes the innermost parenthesised expression context.
// Decrements the nesting counter.  If the counter would go negative (i.e. a
// closing parenthesis was seen with no matching open), a compiler_error is
// raised and compilation is aborted.
void Lexer::lex_finish_expression()
{
    m_process->current_expression_count--;
    if (m_process->current_expression_count < 0)
        compiler_error(m_process->compiler, "You closed an expression that you never opened\n");
}

// Copies the token struct pointed to by 't' into the internal scratch buffer
// m_tmp_token, fills in the current source position, and — if the lexer is
// currently inside a parenthesised expression — stores the accumulated
// expression text in the token's between_brackets field.
// Returns a pointer to m_tmp_token.  Callers must push the token into the
// output vector before calling token_create() again, since only one scratch
// slot exists.
struct token *Lexer::token_create(struct token *t)
{
    memcpy(&m_tmp_token, t, sizeof(struct token));
    m_tmp_token.pos = lex_file_position();
    if (lex_is_in_expression())
        m_tmp_token.between_brackets =
            static_cast<const char *>(buffer_ptr(m_process->parentheses_buffer));
    return &m_tmp_token;
}

// Returns a pointer to the most recently pushed token in the output vector,
// or nullptr if no tokens have been emitted yet.
// Used by several readers to inspect the token that immediately precedes the
// one being built (e.g. to detect a preceding "include" keyword or a leading
// zero for hex/binary number detection).
struct token *Lexer::lexer_last_token()
{
    return static_cast<struct token *>(vector_back_or_null(m_process->token_vec));
}

// Handles a whitespace character (' ' or '\t') in the token stream.
// Rather than emitting a dedicated whitespace token, it sets the whitespace
// flag on the last emitted token (indicating that whitespace follows it) and
// then consumes the whitespace character before tail-calling read_next_token()
// to produce the real next token.
// Returns whatever token read_next_token() produces next.
struct token *Lexer::handle_whitespace()
{
    struct token *last_token = lexer_last_token();
    if (last_token)
        last_token->whitespace = true;
    nextc();
    return read_next_token();
}

// Reads a run of ASCII decimal digits from the source into a newly allocated
// buffer and returns a NUL-terminated string containing those digits.
// Stops (without consuming) the first non-digit character.
// The returned pointer is owned by the internal buffer and must not be freed
// by the caller.
const char *Lexer::read_number_str()
{
    struct buffer *buf = buffer_create();
    char c = peekc();
    LEX_GETC_IF(buf, c, (c >= '0' && c <= '9'));
    buffer_write(buf, 0x00);
    return static_cast<const char *>(buffer_ptr(buf));
}

// Reads a run of decimal digit characters from the source and converts them
// to an unsigned 64-bit integer using atoll().
// Returns the numeric value.
unsigned long long Lexer::read_number()
{
    const char *s = read_number_str();
    return static_cast<unsigned long long>(atoll(s));
}

// Maps a numeric suffix character to its NUMBER_TYPE constant.
// 'L' maps to NUMBER_TYPE_LONG, 'f' maps to NUMBER_TYPE_FLOAT, and any other
// character (including no suffix) maps to NUMBER_TYPE_NORMAL.
// 'c' should be the character currently at the peek position — the caller is
// responsible for consuming it if the return value is not NUMBER_TYPE_NORMAL.
int Lexer::lexer_number_type(char c)
{
    if (c == 'L') return NUMBER_TYPE_LONG;
    if (c == 'f') return NUMBER_TYPE_FLOAT;
    return NUMBER_TYPE_NORMAL;
}

// Builds a TOKEN_TYPE_NUMBER token for the integer value 'number'.
// Peeks at the next character to detect an optional type suffix ('L' or 'f')
// and consumes it if present.  Stores the raw 64-bit value in token.llnum and
// the resolved number type in token.num.type.
// Returns the newly created token.
struct token *Lexer::token_make_number_for_value(unsigned long number)
{
    int number_type = lexer_number_type(peekc());
    if (number_type != NUMBER_TYPE_NORMAL)
        nextc();
    struct token t{};
    t.type = TOKEN_TYPE_NUMBER;
    t.llnum = number;
    t.num.type = number_type;
    return token_create(&t);
}

// Reads a decimal integer literal from the current source position and wraps
// it in a TOKEN_TYPE_NUMBER token.
// Delegates digit consumption to read_number() and token construction to
// token_make_number_for_value().
// Returns the newly created token.
struct token *Lexer::token_make_number()
{
    return token_make_number_for_value(static_cast<unsigned long>(read_number()));
}

// Reads a quoted string literal delimited by 'start_delim' and 'end_delim'
// and returns a TOKEN_TYPE_STRING token whose sval points to the unquoted
// content.
// The opening delimiter is consumed and asserted via nextc().  Each subsequent
// character is written into the buffer; a backslash is skipped (the next
// character, i.e. the escape body, is written as-is without interpretation).
// Reading stops when 'end_delim' or EOF is reached.
// This is also used to lex angle-bracketed include paths by passing '<' and '>'.
struct token *Lexer::token_make_string(char start_delim, char end_delim)
{
    struct buffer *buf = buffer_create();
    assert(nextc() == start_delim);
    char c = nextc();
    for (; c != end_delim && c != EOF; c = nextc())
    {
        if (c == '\\')
            continue;
        buffer_write(buf, c);
    }
    buffer_write(buf, 0x00);
    struct token t{};
    t.type = TOKEN_TYPE_STRING;
    t.sval = static_cast<const char *>(buffer_ptr(buf));
    return token_create(&t);
}

// Returns true if 'op' is a character that should always be treated as a
// complete, stand-alone operator rather than as the first character of a
// two-character operator sequence.
// The characters treated this way are: '(' '[' ',' '.' '*' '?'
bool Lexer::op_treated_as_one(char op)
{
    return op == '(' || op == '[' || op == ',' || op == '.' || op == '*' || op == '?';
}

// Returns true if 'op' can appear as the first (or only) character of any
// valid operator token.  This is a broad set; multi-character validation is
// done separately by op_valid().
bool Lexer::is_single_operator(char op)
{
    return op == '+' || op == '-' || op == '/' || op == '*' || op == '=' ||
           op == '>' || op == '<' || op == '|' || op == '&' || op == '^' ||
           op == '%' || op == '!' || op == '(' || op == '[' || op == ',' ||
           op == '.' || op == '~' || op == '?';
}

// Returns true if 'op' is one of the complete operator strings recognised by
// the compiler.  Used to validate a candidate multi-character operator string
// after it has been assembled from up to two source characters.
// The recognised set includes all C arithmetic, bitwise, comparison, logical,
// assignment, and access operators as well as the variadic ellipsis "...".
bool Lexer::op_valid(const char *op)
{
    return S_EQ(op, "+")  || S_EQ(op, "-")  || S_EQ(op, "*")  || S_EQ(op, "/")  ||
           S_EQ(op, "!")  || S_EQ(op, "^")  || S_EQ(op, "+=") || S_EQ(op, "-=") ||
           S_EQ(op, "*=") || S_EQ(op, "/=") || S_EQ(op, ">>") || S_EQ(op, "<<") ||
           S_EQ(op, ">=") || S_EQ(op, "<=") || S_EQ(op, ">")  || S_EQ(op, "<")  ||
           S_EQ(op, "||") || S_EQ(op, "&&") || S_EQ(op, "|")  || S_EQ(op, "&")  ||
           S_EQ(op, "++") || S_EQ(op, "--") || S_EQ(op, "=")  || S_EQ(op, "!=") ||
           S_EQ(op, "==") || S_EQ(op, "->") || S_EQ(op, "(")  || S_EQ(op, "[")  ||
           S_EQ(op, ",")  || S_EQ(op, ".")  || S_EQ(op, "...") || S_EQ(op, "~") ||
           S_EQ(op, "?")  || S_EQ(op, "%");
}

// Pushes all characters in 'buf' except the first one back into the input
// stream (in reverse order so the original source order is preserved).
// Called when a two-character candidate operator is not valid and must be
// split: the first character is kept as the operator, and the rest are
// returned to be re-read on the next call.
// NUL bytes in the buffer are skipped over.
void Lexer::read_op_flush_back_keep_first(struct buffer *buf)
{
    const char *data = static_cast<const char *>(buffer_ptr(buf));
    int len = buf->len;
    for (int i = len - 1; i >= 1; i--)
    {
        if (data[i] == 0x00)
            continue;
        pushc(data[i]);
    }
}

// Reads between one and two source characters and returns a NUL-terminated
// string representing the longest valid operator at the current position.
// Strategy:
//   1. Consume the first character unconditionally.
//   2. If the first character is not in the "treat-as-one" set, peek at the
//      next character; if it is also a valid operator starter, consume it too.
//   3. Validate the resulting one- or two-character string against op_valid().
//      If the two-character string is not valid, push the second character back
//      via read_op_flush_back_keep_first() and use only the first.
//   4. A single character that is not valid causes a compiler_error.
// Returns a pointer to the NUL-terminated operator string stored in an
// internal buffer.
const char *Lexer::read_op()
{
    bool single_operator = true;
    char op = nextc();
    struct buffer *buf = buffer_create();
    buffer_write(buf, op);

    if (!op_treated_as_one(op))
    {
        op = peekc();
        if (is_single_operator(op))
        {
            buffer_write(buf, op);
            nextc();
            single_operator = false;
        }
    }

    buffer_write(buf, 0x00);
    char *ptr = static_cast<char *>(buffer_ptr(buf));
    if (!single_operator)
    {
        if (!op_valid(ptr))
        {
            read_op_flush_back_keep_first(buf);
            ptr[1] = 0x00;
        }
    }
    else if (!op_valid(ptr))
    {
        compiler_error(m_process->compiler, "The operator %s is not valid\n", ptr);
    }
    return ptr;
}

// Produces either a TOKEN_TYPE_OPERATOR token or, in the special case of '<'
// following an "include" keyword, a TOKEN_TYPE_STRING token containing the
// angle-bracketed include path.
// For the operator case it delegates to read_op() for the actual character
// consumption and then calls token_create().  If the operator is '(' it also
// opens a new expression context via lex_new_expression() so that the text
// inside the parentheses is captured.
// Returns the newly created token.
struct token *Lexer::token_make_operator_or_string()
{
    char op = peekc();
    if (op == '<')
    {
        struct token *last_token = lexer_last_token();
        if (token_is_keyword(last_token, "include"))
            return token_make_string('<', '>');
    }

    struct token t{};
    t.type = TOKEN_TYPE_OPERATOR;
    t.sval = read_op();
    struct token *tok = token_create(&t);
    if (op == '(')
        lex_new_expression();
    return tok;
}

// Reads a single-line comment (everything from the current position up to but
// not including the terminating newline or EOF) and returns a TOKEN_TYPE_COMMENT
// token whose sval contains the comment body text.
// The leading "//" has already been consumed by handle_comment() before this
// is called.  The newline itself is not consumed, so it will be seen on the
// next call to read_next_token() and produce a TOKEN_TYPE_NEWLINE token.
struct token *Lexer::token_make_one_line_comment()
{
    struct buffer *buf = buffer_create();
    char c = 0;
    LEX_GETC_IF(buf, c, c != '\n' && c != EOF);
    struct token t{};
    t.type = TOKEN_TYPE_COMMENT;
    t.sval = static_cast<const char *>(buffer_ptr(buf));
    return token_create(&t);
}

// Reads a block comment (everything between the already-consumed "/*" and the
// closing "*/") and returns a TOKEN_TYPE_COMMENT token.
// The function scans for '*' characters; when one is found it checks whether
// the following character is '/' — if so the comment is closed and scanning
// stops.  If EOF is reached before the closing "*/" a compiler_error is raised.
// The sval of the returned token holds the raw comment body text (excluding
// the delimiters).
struct token *Lexer::token_make_multiline_comment()
{
    struct buffer *buf = buffer_create();
    char c = 0;
    while (1)
    {
        LEX_GETC_IF(buf, c, c != '*' && c != EOF);
        if (c == EOF)
        {
            compiler_error(m_process->compiler, "You did not close this multiline comment\n");
        }
        else if (c == '*')
        {
            nextc();
            if (peekc() == '/')
            {
                nextc();
                break;
            }
        }
    }
    struct token t{};
    t.type = TOKEN_TYPE_COMMENT;
    t.sval = static_cast<const char *>(buffer_ptr(buf));
    return token_create(&t);
}

// Tries to lex a comment starting at the current source position.
// Peeks at the current character: if it is '/' the function consumes it and
// checks the following character:
//   - Another '/' -> consume it and delegate to token_make_one_line_comment().
//   - A '*'       -> consume it and delegate to token_make_multiline_comment().
//   - Anything else -> push the '/' back and fall through to
//     token_make_operator_or_string() to produce a division operator.
// If the current character is not '/' at all, returns nullptr so that the
// caller knows no comment was found.
struct token *Lexer::handle_comment()
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
    return nullptr;
}

// Consumes a single punctuation/symbol character and returns a
// TOKEN_TYPE_SYMBOL token whose cval holds that character.
// If the character is ')' the innermost expression context is closed via
// lex_finish_expression().
struct token *Lexer::token_make_symbol()
{
    char c = nextc();
    if (c == ')')
        lex_finish_expression();
    struct token t{};
    t.type = TOKEN_TYPE_SYMBOL;
    t.cval = c;
    return token_create(&t);
}

// Reads an identifier or keyword from the current source position.
// Consumes characters that are alphanumeric or '_' and stores them in a
// buffer.  The resulting string is then checked against the full set of
// recognised keywords (including data-type names via keyword_is_datatype() and
// all control-flow/storage-class keywords listed inline).  If a match is found
// the token type is TOKEN_TYPE_KEYWORD; otherwise TOKEN_TYPE_IDENTIFIER.
// The token's sval points to the NUL-terminated identifier/keyword string.
// Returns the newly created token.
struct token *Lexer::token_make_identifier_or_keyword()
{
    struct buffer *buf = buffer_create();
    char c = 0;
    LEX_GETC_IF(buf, c,
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_');
    buffer_write(buf, 0x00);

    const char *str = static_cast<const char *>(buffer_ptr(buf));
    struct token t{};
    if (keyword_is_datatype(str) || /* check full keyword list */ [&]() {
        return S_EQ(str, "unsigned") || S_EQ(str, "signed")  || S_EQ(str, "static") ||
               S_EQ(str, "__ignore_typecheck") || S_EQ(str, "return") ||
               S_EQ(str, "include") || S_EQ(str, "sizeof") || S_EQ(str, "if")    ||
               S_EQ(str, "else")    || S_EQ(str, "while")  || S_EQ(str, "for")   ||
               S_EQ(str, "do")      || S_EQ(str, "break")  || S_EQ(str, "continue") ||
               S_EQ(str, "switch")  || S_EQ(str, "case")   || S_EQ(str, "default") ||
               S_EQ(str, "goto")    || S_EQ(str, "typedef") || S_EQ(str, "const") ||
               S_EQ(str, "extern")  || S_EQ(str, "restrict");
    }())
    {
        t.type = TOKEN_TYPE_KEYWORD;
    }
    else
    {
        t.type = TOKEN_TYPE_IDENTIFIER;
    }
    t.sval = str;
    return token_create(&t);
}

// Attempts to lex a token whose first character is a letter or underscore.
// Peeks at the current character; if it is alphabetic or '_' it delegates to
// token_make_identifier_or_keyword() and returns the result.
// Returns nullptr if the character does not start an identifier, allowing
// read_next_token() to handle it via the default error path.
struct token *Lexer::read_special_token()
{
    char c = peekc();
    if (isalpha(c) || c == '_')
        return token_make_identifier_or_keyword();
    return nullptr;
}

// Consumes the newline character at the current source position and returns a
// TOKEN_TYPE_NEWLINE token.  The newline is consumed by nextc() so the
// position counters are updated correctly.
struct token *Lexer::token_make_newline()
{
    nextc();
    struct token t{};
    t.type = TOKEN_TYPE_NEWLINE;
    return token_create(&t);
}

// Translates a single-character escape code (the character after the
// backslash) into the corresponding ASCII value.
// Recognised codes: 'n' -> newline, '\\' -> backslash, 't' -> tab,
// '\'' -> single quote.  Any unrecognised code returns 0.
char Lexer::lex_get_escaped_char(char c)
{
    switch (c)
    {
    case 'n':  return '\n';
    case '\\': return '\\';
    case 't':  return '\t';
    case '\'': return '\'';
    default:   return 0;
    }
}

// Removes the most recently pushed token from the output vector.
// Used by token_make_special_number() to retract the leading '0' token that
// was emitted before the 'x' or 'b' prefix character was seen.
void Lexer::lexer_pop_token()
{
    vector_pop(m_process->token_vec);
}

// Returns true if 'c' is a valid hexadecimal digit (0-9 or a-f, case-
// insensitive).  The character is lower-cased before comparison so both
// upper- and lower-case hex letters are accepted.
bool Lexer::is_hex_char(char c)
{
    c = static_cast<char>(tolower(c));
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// Reads a run of hexadecimal digit characters from the current source position
// into a fresh buffer and returns the NUL-terminated string.
// Stops (without consuming) the first character that is not a hex digit.
// The returned pointer is owned by the internal buffer.
const char *Lexer::read_hex_number_str()
{
    struct buffer *buf = buffer_create();
    char c = peekc();
    LEX_GETC_IF(buf, c, is_hex_char(c));
    buffer_write(buf, 0x00);
    return static_cast<const char *>(buffer_ptr(buf));
}

// Lexes the digits of a hexadecimal integer literal (the "0x" prefix has
// already been consumed by token_make_special_number()).
// Consumes the 'x' character, reads the remaining hex digits via
// read_hex_number_str(), converts them to an unsigned long with base-16
// strtol, and delegates token creation to token_make_number_for_value().
// Returns a TOKEN_TYPE_NUMBER token whose llnum holds the integer value.
struct token *Lexer::token_make_special_number_hexadecimal()
{
    nextc(); // skip 'x'
    unsigned long number = strtol(read_hex_number_str(), nullptr, 16);
    return token_make_number_for_value(number);
}

// Validates that every character in 'str' is either '0' or '1'.
// Raises a compiler_error and aborts if any other character is found.
// Called after reading the digit string of a binary literal to catch
// invalid digits (e.g. "0b123").
void Lexer::lexer_validate_binary_string(const char *str)
{
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] != '0' && str[i] != '1')
            compiler_error(m_process->compiler, "This is not a valid binary number\n");
    }
}

// Lexes the digits of a binary integer literal (the "0b" prefix has already
// been consumed by token_make_special_number()).
// Consumes the 'b' character, reads decimal digit characters via
// read_number_str(), validates them as binary digits, converts the string to
// an unsigned long with base-2 strtol, and delegates token creation to
// token_make_number_for_value().
// Returns a TOKEN_TYPE_NUMBER token whose llnum holds the integer value.
struct token *Lexer::token_make_special_number_binary()
{
    nextc(); // skip 'b'
    const char *number_str = read_number_str();
    lexer_validate_binary_string(number_str);
    unsigned long number = strtol(number_str, nullptr, 2);
    return token_make_number_for_value(number);
}

// Entry point for lexing number literals that begin with a prefix character
// ('x' for hexadecimal, 'b' for binary) after a leading '0'.
// Checks the last emitted token: if it is a NUMBER token with value 0, pops
// it (retracting the leading zero) and then dispatches based on the next
// character:
//   - 'x' -> token_make_special_number_hexadecimal()
//   - 'b' -> token_make_special_number_binary()
// If the last token is not a zero-valued number (e.g. the 'x' or 'b' appears
// in an identifier context), falls through to token_make_identifier_or_keyword()
// so it is treated as a plain identifier character instead.
// Returns the newly created token, or nullptr if an unrecognised prefix is seen.
struct token *Lexer::token_make_special_number()
{
    struct token *last_token = lexer_last_token();
    if (!last_token || !(last_token->type == TOKEN_TYPE_NUMBER && last_token->llnum == 0))
        return token_make_identifier_or_keyword();

    lexer_pop_token();
    char c = peekc();
    if (c == 'x')
        return token_make_special_number_hexadecimal();
    if (c == 'b')
        return token_make_special_number_binary();
    return nullptr;
}

// Lexes a character literal of the form 'c' or '\e' and returns a
// TOKEN_TYPE_NUMBER token whose cval holds the character value.
// The opening single quote is consumed and asserted.  If the next character is
// a backslash the following escape code character is consumed and resolved via
// lex_get_escaped_char().  The closing single quote is then consumed; if it is
// missing a compiler_error is raised.
// Character literals are represented as number tokens because in C a char
// literal has type int.
struct token *Lexer::token_make_quote()
{
    assert_next_char('\'');
    char c = nextc();
    if (c == '\\')
    {
        c = nextc();
        c = lex_get_escaped_char(c);
    }
    if (nextc() != '\'')
        compiler_error(m_process->compiler,
                       "You opened a quote ' but did not close it with a ' character");

    struct token t{};
    t.type = TOKEN_TYPE_NUMBER;
    t.cval = c;
    return token_create(&t);
}

// Produces the next token from the source stream, or nullptr at EOF.
// First checks for a comment (which may resolve to a comment token or an
// operator token if the '/' is not followed by '/' or '*').  Then dispatches
// on the peeked character using the following priority:
//   - NUMERIC_CASE macro expansion   -> token_make_number()
//   - OPERATOR_CASE (excl. '/')      -> token_make_operator_or_string()
//   - SYMBOL_CASE                    -> token_make_symbol()
//   - 'b' or 'x'                     -> token_make_special_number()
//   - '"'                            -> token_make_string('"', '"')
//   - '\''                           -> token_make_quote()
//   - ' ' or '\t'                    -> handle_whitespace() (no token emitted directly)
//   - '\n'                           -> token_make_newline()
//   - EOF                            -> returns nullptr
//   - anything else                  -> read_special_token() (identifiers/keywords);
//                                       if that also returns nullptr, compiler_error.
// Returns a pointer to the token, or nullptr at end of file.
struct token *Lexer::read_next_token()
{
    struct token *token = nullptr;
    char c = peekc();

    token = handle_comment();
    if (token)
        return token;

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

    case ' ':
    case '\t':
        token = handle_whitespace();
        break;

    case '\n':
        token = token_make_newline();
        break;

    case EOF:
        break;

    default:
        token = read_special_token();
        if (!token)
            compiler_error(m_process->compiler, "Unexpected token\n");
    }
    return token;
}

// Runs the complete lexical analysis pass over the source attached to
// m_process.
// Initialises the expression nesting counter and the source-file position,
// then repeatedly calls read_next_token() until it returns nullptr (EOF),
// pushing each token into m_process->token_vec.
// Returns LEXICAL_ANALYSIS_ALL_OK on success.  Errors are reported via
// compiler_error() which does not return.
int Lexer::run()
{
    m_process->current_expression_count = 0;
    m_process->parentheses_buffer = nullptr;
    m_process->pos.filename = m_process->compiler->cfile.abs_path;

    struct token *token = read_next_token();
    while (token)
    {
        vector_push(m_process->token_vec, token);
        token = read_next_token();
    }
    return LEXICAL_ANALYSIS_ALL_OK;
}

// ============================================================================
// Public C API
// ============================================================================

// Public entry point for the lexer.  Creates a Lexer instance bound to
// 'process' and calls run() to tokenise the entire source.
// 'process' must already have its I/O function table, compiler pointer, token
// vector, and initial position populated by the caller.
// Returns LEXICAL_ANALYSIS_ALL_OK on success; errors abort via compiler_error.
int lex(struct lex_process *process)
{
    Lexer lexer(process);
    return lexer.run();
}

// --- String-backed lexer for tokens_build_for_string ---

// I/O callback: reads and returns the next byte from the buffer stored in
// process->private_data.  Used when lexing a string literal rather than a
// file.
static char lexer_string_buffer_next_char(struct lex_process *process)
{
    struct buffer *buf = static_cast<struct buffer *>(lex_process_private(process));
    return buffer_read(buf);
}

// I/O callback: returns the next byte from the buffer without consuming it.
// Mirrors lexer_string_buffer_next_char() but leaves the read position
// unchanged.
static char lexer_string_buffer_peek_char(struct lex_process *process)
{
    struct buffer *buf = static_cast<struct buffer *>(lex_process_private(process));
    return buffer_peek(buf);
}

// I/O callback: pushes character 'c' back into the buffer so it will be
// returned by the next call to lexer_string_buffer_next_char().
static void lexer_string_buffer_push_char(struct lex_process *process, char c)
{
    struct buffer *buf = static_cast<struct buffer *>(lex_process_private(process));
    buffer_write(buf, c);
}

// Function table wiring the three string-buffer I/O callbacks together into
// the lex_process_functions interface expected by the Lexer.
static struct lex_process_functions lexer_string_buffer_functions = {
    lexer_string_buffer_next_char,
    lexer_string_buffer_peek_char,
    lexer_string_buffer_push_char
};

// Tokenises the NUL-terminated C source string 'str' in the context of
// 'compiler' and returns a fully populated lex_process ready for the parser.
// Internally copies 'str' into a heap buffer, creates a lex_process backed by
// that buffer (using the string-buffer I/O callbacks), runs the lexer, and
// returns the resulting lex_process.
// Returns nullptr if lex_process creation fails or if lexical analysis
// encounters an error.
struct lex_process *tokens_build_for_string(struct compile_process *compiler, const char *str)
{
    struct buffer *buf = buffer_create();
    buffer_printf(buf, "%s", str);
    struct lex_process *proc = lex_process_create(compiler, &lexer_string_buffer_functions, buf);
    if (!proc)
        return nullptr;
    if (lex(proc) != LEXICAL_ANALYSIS_ALL_OK)
        return nullptr;
    return proc;
}

// Returns true if 'str' names a built-in C data type keyword.
// Recognised types: void, char, int, short, float, double, long, struct, union.
// Used by token_make_identifier_or_keyword() to distinguish type keywords from
// other keywords and from plain identifiers.
bool keyword_is_datatype(const char *str)
{
    return S_EQ(str, "void")   || S_EQ(str, "char")  || S_EQ(str, "int")    ||
           S_EQ(str, "short")  || S_EQ(str, "float") || S_EQ(str, "double") ||
           S_EQ(str, "long")   || S_EQ(str, "struct") || S_EQ(str, "union");
}
