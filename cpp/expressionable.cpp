#include "compiler.hpp"

// Operator precedence table for expression parsing.
// Entries are ordered from highest precedence (index 0) to lowest (last index).
// Each group lists operators that share the same precedence level and their
// associativity. This mirrors the C standard operator precedence chart.
struct expressionable_op_precedence_group op_precedence[TOTAL_OPERATOR_GROUPS] = {
    // Postfix operators (highest precedence, level 1 in C).
    // Includes postfix increment/decrement, function call (), subscript [],
    // member access . and ->, and their opening-bracket forms. Left-to-right
    // associativity means chained calls like f()() or a[0][1] parse left first.
    {{"++", "--", "()", "[]", "(", "[", ".", "->", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Multiplicative operators (level 5 in C).
    // *, /, and % bind tighter than addition because multiplication has higher
    // mathematical precedence. Left-to-right so a*b/c evaluates as (a*b)/c.
    {{"*", "/", "%", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Additive operators (level 6 in C).
    // + and - are below multiplicative so that 2+3*4 correctly parses as 2+(3*4).
    {{"+", "-", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise shift operators (level 7 in C).
    // << and >> sit below additive; shifting is conceptually a scaled multiply/
    // divide but operates at the bit level, so it ranks below arithmetic.
    {{"<<", ">>", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Relational operators (level 8 in C).
    // <, <=, >, >= compare magnitude and produce a boolean result. They rank
    // below shift so that expressions like a << 1 < b parse as (a<<1) < b.
    {{"<", "<=", ">", ">=", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Equality operators (level 9 in C).
    // == and != rank below relational so that a < b == c < d means
    // (a<b) == (c<d), comparing two boolean-like results.
    {{"==", "!=", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise AND (level 10 in C).
    // Single & sits below equality so bitfield masking (x & MASK == 0) requires
    // explicit parentheses — a deliberate C design choice.
    {{"&", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise XOR (level 11 in C).
    // ^ ranks below & so that combined masks like (a & b ^ c & d) group as
    // ((a&b) ^ (c&d)).
    {{"^", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise OR (level 12 in C).
    // | is below ^ so that flag combinations a | b ^ c mean a | (b^c).
    {{"|", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Logical AND (level 13 in C).
    // && short-circuits and ranks below all bitwise operators, separating
    // bit-manipulation from boolean logic in mixed expressions.
    {{"&&", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Logical OR (level 14 in C).
    // || short-circuits and ranks below && so that a || b && c means a || (b&&c),
    // matching mathematical "and binds tighter than or".
    {{"||", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Ternary conditional (level 15 in C).
    // ? and : are parsed as a pair with right-to-left associativity so that
    // nested ternaries like a ? b : c ? d : e associate as a ? b : (c ? d : e).
    {{"?", ":", nullptr}, ASSOCIATIVITY_RIGHT_TO_LEFT},

    // Assignment operators (level 16 in C), including all compound forms.
    // Right-to-left associativity allows chained assignment: a = b = 0
    // evaluates as a = (b = 0). Compound forms (+=, -=, etc.) have the same
    // precedence as plain = per the C standard.
    {{"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", nullptr},
     ASSOCIATIVITY_RIGHT_TO_LEFT},

    // Comma operator (level 17, lowest precedence in C).
    // , evaluates its left operand for side effects, discards the result, then
    // yields the right operand. Its low rank means it parses last so that
    // assignment expressions on either side are fully parsed first.
    {{",", nullptr}, ASSOCIATIVITY_LEFT_TO_RIGHT},
};
