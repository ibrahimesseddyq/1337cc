// parser_expr.cpp — Expression parsing (single-token nodes, binary ops,
//                   parentheses, array subscript, comma, cast, ternary).

#include "parser.hpp"

// ===========================================================================
// Expression parsing
// ===========================================================================

void Parser::parse_expressionable(History h)
{
    while (parse_expressionable_single(h) == 0)
        ;
}

void Parser::parse_expressionable_root(History h)
{
    parse_expressionable(h);
    struct node *result = node_pop();
    node_push(result);
}

int Parser::parse_expressionable_single(History h)
{
    struct token *tok = peek_token();
    if (!tok)
        return -1;

    h.flags |= NODE_FLAG_INSIDE_EXPRESSION;

    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
    case TOKEN_TYPE_STRING:
        parse_single_token_to_node();
        return 0;

    case TOKEN_TYPE_IDENTIFIER:
        parse_identifier(h);
        return 0;

    case TOKEN_TYPE_OPERATOR:
        parse_exp(h);
        return 0;

    case TOKEN_TYPE_KEYWORD:
        parse_keyword(h);
        return 0;

    default:
        return -1;
    }
}

void Parser::parse_single_token_to_node()
{
    struct token *tok = next_token();
    struct node  *n   = nullptr;
    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
    {
        struct node tmp{};
        tmp.type     = NODE_TYPE_NUMBER;
        tmp.llnum    = tok->llnum;
        tmp.num.type = tok->num.type;
        n = node_create(&tmp);
        break;
    }
    case TOKEN_TYPE_IDENTIFIER:
    {
        struct node tmp{};
        tmp.type = NODE_TYPE_IDENTIFIER;
        tmp.sval = tok->sval;
        n = node_create(&tmp);
        break;
    }
    case TOKEN_TYPE_STRING:
    {
        struct node tmp{};
        tmp.type = NODE_TYPE_STRING;
        tmp.sval = tok->sval;
        n = node_create(&tmp);
        break;
    }
    default:
        compiler_error(m_process,
            "Token type cannot be converted to a single AST node\n");
    }
    (void)n;
}

void Parser::parse_identifier(History h)
{
    assert(peek_token()->type == TOKEN_TYPE_IDENTIFIER);
    parse_single_token_to_node();
}

int Parser::parse_exp(History h)
{
    struct token *tok = peek_token();

    if (S_EQ(tok->sval, "("))
        parse_parenthesized_expression(h);
    else if (S_EQ(tok->sval, "["))
        parse_array_access(h);
    else if (S_EQ(tok->sval, "?"))
        parse_ternary_expression(h);
    else if (S_EQ(tok->sval, ","))
        parse_comma_expression(h);
    else
        parse_exp_normal(h);

    return 0;
}

void Parser::parse_exp_normal(History h)
{
    struct token *op_tok   = peek_token();
    const char   *op       = op_tok->sval;
    struct node  *node_left = node_peek_expressionable_or_null();
    if (!node_left)
        return;

    next_token();

    node_pop();
    node_left->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    parse_expressionable(history_down(h, h.flags));
    struct node *node_right = node_pop();
    node_right->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    make_exp_node(node_left, node_right, op);
    struct node *exp_node = node_pop();

    reorder_expression(&exp_node);
    node_push(exp_node);
}

void Parser::parse_additional_exp()
{
    if (peek_token()->type == TOKEN_TYPE_OPERATOR)
        parse_expressionable(make_history(0));
}

void Parser::parse_parenthesized_expression(History h)
{
    expect_op("(");

    if (peek_token()->type == TOKEN_TYPE_KEYWORD)
    {
        parse_cast_expression();
        return;
    }

    struct node *left_node = nullptr;
    struct node *tmp_node  = node_peek_or_null();
    if (tmp_node && node_is_value_type(tmp_node))
    {
        left_node = tmp_node;
        node_pop();
    }

    struct node *exp_node = m_blank_node;
    if (!peek_is_symbol(')'))
    {
        parse_expressionable_root(make_history(0));
        exp_node = node_pop();
    }
    expect_sym(')');

    make_exp_parentheses_node(exp_node);

    if (left_node)
    {
        struct node *parens_node = node_pop();
        make_exp_node(left_node, parens_node, "()");
    }

    parse_additional_exp();
}

void Parser::parse_array_access(History h)
{
    struct node *left_node = node_peek_or_null();
    if (left_node)
        node_pop();

    expect_op("[");
    parse_expressionable_root(h);
    expect_sym(']');

    struct node *exp_node = node_pop();
    make_bracket_node(exp_node);

    if (left_node)
    {
        struct node *bracket_node = node_pop();
        make_exp_node(left_node, bracket_node, "[]");
    }
}

void Parser::parse_comma_expression(History h)
{
    next_token();
    struct node *left_node = node_pop();
    parse_expressionable_root(h);
    struct node *right_node = node_pop();
    make_exp_node(left_node, right_node, ",");
}

void Parser::parse_cast_expression()
{
    struct datatype dtype{};
    parse_datatype(&dtype);
    expect_sym(')');

    parse_expressionable(make_history(0));
    struct node *operand = node_pop();
    make_cast_node(&dtype, operand);
}

void Parser::parse_ternary_expression(History h)
{
    struct node *cond_node = node_pop();
    expect_op("?");

    parse_expressionable_root(history_down(h, HISTORY_PARENS_NOT_CALL));
    struct node *true_node = node_pop();

    expect_sym(':');

    parse_expressionable_root(history_down(h, HISTORY_PARENS_NOT_CALL));
    struct node *false_node = node_pop();

    make_tenary_node(true_node, false_node);
    struct node *ternary_node = node_pop();
    make_exp_node(cond_node, ternary_node, "?");
}
