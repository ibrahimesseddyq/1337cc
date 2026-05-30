// parser_stmt.cpp — Statement parsing, control flow, keywords, top-level dispatch.

#include "parser.hpp"

// ===========================================================================
// Statements
// ===========================================================================

void Parser::parse_label(History h)
{
    expect_sym(':');
    struct node *label_name_node = node_pop();
    if (label_name_node->type != NODE_TYPE_IDENTIFIER)
        compiler_error(m_process,
            "Expected an identifier for label, got something else\n");
    make_label_node(label_name_node);
}

void Parser::parse_symbol_token(History h)
{
    if (peek_is_symbol('{'))
    {
        size_t var_size = 0;
        parse_body(&var_size, make_history(HISTORY_GLOBAL_SCOPE));
        struct node *body_node = node_pop();
        node_push(body_node);
        return;
    }

    if (peek_is_symbol(':'))
    {
        parse_label(make_history(0));
        return;
    }

    compiler_error(m_process, "Unexpected symbol token\n");
}

void Parser::parse_statement(History h)
{
    if (peek_token()->type == TOKEN_TYPE_KEYWORD)
    {
        parse_keyword(h);
        return;
    }

    parse_expressionable_root(h);

    struct token *tok = peek_token();
    if (tok->type == TOKEN_TYPE_SYMBOL && !token_is_symbol(tok, ';'))
    {
        parse_symbol_token(h);
        return;
    }

    expect_sym(';');
}

// ===========================================================================
// Control flow
// ===========================================================================

void Parser::parse_keyword_with_parens_expr(const char *keyword)
{
    expect_keyword(keyword);
    expect_op("(");
    parse_expressionable_root(make_history(0));
    expect_sym(')');
}

struct node *Parser::parse_else_block(History h)
{
    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();
    make_else_node(body_node);
    return node_pop();
}

struct node *Parser::parse_else_or_else_if(History h)
{
    if (!peek_is_keyword("else"))
        return nullptr;

    next_token();

    if (peek_is_keyword("if"))
    {
        parse_if_stmt(history_down(h, 0));
        return node_pop();
    }

    return parse_else_block(history_down(h, 0));
}

void Parser::parse_if_stmt(History h)
{
    expect_keyword("if");
    expect_op("(");
    parse_expressionable_root(h);
    expect_sym(')');

    struct node *cond_node = node_pop();
    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_if_node(cond_node, body_node, parse_else_or_else_if(h));
}

void Parser::parse_while(History h)
{
    parse_keyword_with_parens_expr("while");
    struct node *exp_node = node_pop();

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_while_node(exp_node, body_node);
}

void Parser::parse_do_while(History h)
{
    expect_keyword("do");

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    parse_keyword_with_parens_expr("while");
    struct node *exp_node = node_pop();
    expect_sym(';');

    make_do_while_node(body_node, exp_node);
}

bool Parser::parse_for_loop_part(History h)
{
    if (peek_is_symbol(';'))
    {
        next_token();
        return false;
    }
    parse_expressionable_root(h);
    expect_sym(';');
    return true;
}

bool Parser::parse_for_loop_part_loop(History h)
{
    if (peek_is_symbol(')'))
        return false;
    parse_expressionable_root(h);
    return true;
}

void Parser::parse_for_stmt(History h)
{
    struct node *init_node = nullptr;
    struct node *cond_node = nullptr;
    struct node *loop_node = nullptr;
    struct node *body_node = nullptr;

    expect_keyword("for");
    expect_op("(");

    if (parse_for_loop_part(h))
        init_node = node_pop();

    if (parse_for_loop_part(h))
        cond_node = node_pop();

    if (parse_for_loop_part_loop(h))
        loop_node = node_pop();

    expect_sym(')');

    size_t var_size = 0;
    parse_body(&var_size, h);
    body_node = node_pop();

    make_for_node(init_node, cond_node, loop_node, body_node);
}

void Parser::parse_case(History h)
{
    expect_keyword("case");
    parse_expressionable_root(h);
    struct node *case_exp = node_pop();
    expect_sym(':');
    make_case_node(case_exp);

    if (case_exp->type != NODE_TYPE_NUMBER)
        compiler_error(m_process,
            "Only numeric constants are supported in case labels\n");

    struct node *case_node = node_peek();

    if (m_switch_cases_ptr && *m_switch_cases_ptr)
    {
        struct parsed_switch_case sc;
        sc.index = static_cast<int>(case_node->stmt._case.exp->llnum);
        vector_push(*m_switch_cases_ptr, &sc);
    }
}

void Parser::parse_switch(History h)
{
    struct vector *cases  = vector_create(sizeof(struct parsed_switch_case));
    bool has_default      = false;

    struct vector **saved_ptr = m_switch_cases_ptr;
    m_switch_cases_ptr        = &cases;

    parse_keyword_with_parens_expr("switch");
    struct node *exp_node = node_pop();

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_switch_node(exp_node, body_node, cases, has_default);

    m_switch_cases_ptr = saved_ptr;
}

// ===========================================================================
// Other keyword statements
// ===========================================================================

void Parser::parse_return(History h)
{
    expect_keyword("return");

    if (peek_is_symbol(';'))
    {
        expect_sym(';');
        make_return_node(nullptr);
        return;
    }

    parse_expressionable_root(h);
    struct node *exp_node = node_pop();
    make_return_node(exp_node);
    expect_sym(';');
}

void Parser::parse_continue(History h)
{
    expect_keyword("continue");
    expect_sym(';');
    make_continue_node();
}

void Parser::parse_break(History h)
{
    expect_keyword("break");
    expect_sym(';');
    make_break_node();
}

void Parser::parse_default(History h)
{
    (void)h;
    expect_keyword("default");
    expect_sym(':');
    make_default_node();
}

void Parser::parse_goto(History h)
{
    expect_keyword("goto");
    parse_identifier(make_history(0));
    expect_sym(';');
    struct node *label_node = node_pop();
    make_goto_node(label_node);
}

// ===========================================================================
// Top-level dispatch
// ===========================================================================

void Parser::parse_keyword(History h)
{
    struct token *tok = peek_token();

    if (is_variable_modifier_keyword(tok->sval) ||
        keyword_is_datatype(tok->sval))
    {
        parse_variable_function_or_struct_union(h);
        return;
    }

    if      (S_EQ(tok->sval, "break"))    { parse_break(h);   return; }
    else if (S_EQ(tok->sval, "continue")) { parse_continue(h);return; }
    else if (S_EQ(tok->sval, "return"))   { parse_return(h);  return; }
    else if (S_EQ(tok->sval, "if"))       { parse_if_stmt(h); return; }
    else if (S_EQ(tok->sval, "for"))      { parse_for_stmt(h);return; }
    else if (S_EQ(tok->sval, "while"))    { parse_while(h);   return; }
    else if (S_EQ(tok->sval, "do"))       { parse_do_while(h);return; }
    else if (S_EQ(tok->sval, "switch"))   { parse_switch(h);  return; }
    else if (S_EQ(tok->sval, "goto"))     { parse_goto(h);    return; }
    else if (S_EQ(tok->sval, "case"))     { parse_case(h);    return; }
    else if (S_EQ(tok->sval, "default"))  { parse_default(h); return; }

    compiler_error(m_process, "Unknown or unsupported keyword\n");
}

void Parser::parse_keyword_for_global()
{
    parse_keyword(make_history(0));
    struct node *node = node_pop();
    node_push(node);
}

int Parser::parse_next()
{
    struct token *tok = peek_token();
    if (!tok)
        return -1;

    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
    case TOKEN_TYPE_IDENTIFIER:
    case TOKEN_TYPE_STRING:
        parse_expressionable(make_history(0));
        break;

    case TOKEN_TYPE_KEYWORD:
        parse_keyword_for_global();
        break;

    case TOKEN_TYPE_SYMBOL:
        parse_symbol_token(make_history(0));
        break;

    default:
        break;
    }
    return 0;
}
