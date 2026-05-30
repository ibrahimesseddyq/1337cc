// parser_body.cpp — Body parsing and variable-size accounting.

#include "parser.hpp"

// ===========================================================================
// Size accounting
// ===========================================================================

void Parser::struct_or_union_var_size(History h, size_t *accum,
                                       struct node *node)
{
    *accum += variable_size(node);
    if (node->var.type.flags & DATATYPE_FLAG_IS_POINTER)
        return;

    struct node *largest =
        variable_struct_or_union_body_node(node)->body.largest_var_node;
    if (largest)
    {
        *accum += align_value(static_cast<int>(*accum),
                              static_cast<int>(largest->var.type.size));
    }
}

void Parser::append_size_for_variable_list(History h, size_t *accum,
                                            struct vector *vec)
{
    vector_set_peek_pointer(vec, 0);
    struct node *node = vector_peek_ptr_typed<struct node>(vec);
    while (node)
    {
        append_size_for_node(h, accum, node);
        node = vector_peek_ptr_typed<struct node>(vec);
    }
}

void Parser::append_size_for_node(History h, size_t *accum, struct node *node)
{
    if (!node)
        return;

    if (node->type == NODE_TYPE_VARIABLE)
    {
        if (node_is_struct_or_union_variable(node))
        {
            struct_or_union_var_size(h, accum, node);
            return;
        }
        *accum += variable_size(node);
    }
    else if (node->type == NODE_TYPE_VARIABLE_LIST)
    {
        append_size_for_variable_list(h, accum, node->var_list.list);
    }
}

void Parser::finalize_body(History h, struct node *body_node,
                            struct vector *body_vec, size_t *variable_size,
                            struct node *largest_align_eligible,
                            struct node *largest_possible)
{
    if (h.flags & HISTORY_INSIDE_UNION)
    {
        if (largest_possible)
            *variable_size = ::variable_size(largest_possible);
    }

    int pad = compute_sum_padding(body_vec);
    *variable_size += static_cast<size_t>(pad);

    if (largest_align_eligible)
    {
        *variable_size = static_cast<size_t>(
            align_value(static_cast<int>(*variable_size),
                        static_cast<int>(largest_align_eligible->var.type.size)));
    }

    body_node->body.largest_var_node = largest_align_eligible;
    body_node->body.padded           = (pad != 0);
    body_node->body.size             = *variable_size;
    body_node->body.statements       = body_vec;
}

// ===========================================================================
// Body parsing
// ===========================================================================

void Parser::parse_body_single_statement(size_t *variable_size,
                                          struct vector *body_vec,
                                          History h)
{
    make_body_node(nullptr, 0, false, nullptr);
    struct node *body_node = node_pop();
    body_node->binded.owner = parser_current_body;
    parser_current_body     = body_node;

    parse_statement(history_down(h, h.flags));
    struct node *stmt_node = node_pop();
    vector_push(body_vec, &stmt_node);
    append_size_for_node(h, variable_size, stmt_node);

    struct node *largest = nullptr;
    if (stmt_node->type == NODE_TYPE_VARIABLE)
        largest = stmt_node;

    finalize_body(h, body_node, body_vec, variable_size, largest, largest);
    parser_current_body = body_node->binded.owner;
    node_push(body_node);
}

void Parser::parse_body_multiple_statements(size_t *variable_size,
                                             struct vector *body_vec,
                                             History h)
{
    make_body_node(nullptr, 0, false, nullptr);
    struct node *body_node = node_pop();
    body_node->binded.owner = parser_current_body;
    parser_current_body     = body_node;

    struct node *largest_possible        = nullptr;
    struct node *largest_align_eligible  = nullptr;

    expect_sym('{');

    while (!peek_is_symbol('}'))
    {
        parse_statement(history_down(h, h.flags));
        struct node *stmt_node = node_pop();

        if (stmt_node->type == NODE_TYPE_VARIABLE)
        {
            if (!largest_possible ||
                largest_possible->var.type.size <= stmt_node->var.type.size)
            {
                largest_possible = stmt_node;
            }

            if (variable_node_is_primitive(stmt_node))
            {
                if (!largest_align_eligible ||
                    largest_align_eligible->var.type.size <= stmt_node->var.type.size)
                {
                    largest_align_eligible = stmt_node;
                }
            }
        }

        vector_push(body_vec, &stmt_node);
        append_size_for_node(h, variable_size,
                              variable_node_or_list(stmt_node));
    }

    expect_sym('}');

    finalize_body(h, body_node, body_vec, variable_size,
                  largest_align_eligible, largest_possible);
    parser_current_body = body_node->binded.owner;
    node_push(body_node);
}

void Parser::parse_body(size_t *variable_size, History h)
{
    new_scope();

    size_t tmp_size = 0;
    if (!variable_size)
        variable_size = &tmp_size;

    struct vector *body_vec = vector_create(sizeof(struct node *));

    if (!peek_is_symbol('{'))
    {
        parse_body_single_statement(variable_size, body_vec, h);
        finish_scope();
        return;
    }

    parse_body_multiple_statements(variable_size, body_vec, h);
    finish_scope();

    if (h.flags & HISTORY_INSIDE_FUNCTION_BODY)
        parser_current_function->func.stack_size += *variable_size;
}
