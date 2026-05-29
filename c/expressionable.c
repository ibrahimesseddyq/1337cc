#include "compiler.h"

/**
 * @brief Operator precedence table used by the parser.
 *
 * Each entry in this table represents a group of operators
 * that share the same precedence level.
 *
 * Format for each group:
 *   - .operators: A NULL-terminated list of operator strings.
 *   - .associtivity: Operator associativity (left-to-right or right-to-left).
 *
 * Precedence order:
 *   - The first group (index 0) has the highest precedence.
 *   - The last group has the lowest precedence.
 *
 * This table is referenced by the expression parser to
 * correctly order operators in the AST (Abstract Syntax Tree).
 */
struct expressionable_op_precedence_group op_precedence[TOTAL_OPERATOR_GROUPS] = {
    // Postfix operators (highest precedence)
    {.operators={"++", "--", "()", "[]", "(", "[", ".", "->", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Multiplicative
    {.operators={"*", "/", "%", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Additive
    {.operators={"+", "-", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise shift
    {.operators={"<<", ">>", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Relational
    {.operators={"<", "<=", ">", ">=", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Equality
    {.operators={"==", "!=", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise AND
    {.operators={"&", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise XOR
    {.operators={"^", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Bitwise OR
    {.operators={"|", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Logical AND
    {.operators={"&&", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Logical OR
    {.operators={"||", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT},

    // Ternary conditional
    {.operators={"?", ":", NULL},
     .associtivity=ASSOCIATIVITY_RIGHT_TO_LEFT},

    // Assignment (including compound assignments)
    {.operators={"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", NULL},
     .associtivity=ASSOCIATIVITY_RIGHT_TO_LEFT},

    // Comma operator (lowest precedence)
    {.operators={",", NULL},
     .associtivity=ASSOCIATIVITY_LEFT_TO_RIGHT}
};
