// parser_operators.cpp — Operator precedence lookup and expression tree reordering.

#include "parser.hpp"

// ===========================================================================
// Operator precedence
// ===========================================================================

int Parser::op_precedence_index(const char *op,
    struct expressionable_op_precedence_group **group_out)
{
    *group_out = nullptr;
    for (int i = 0; i < TOTAL_OPERATOR_GROUPS; i++)
    {
        for (int b = 0; op_precedence[i].operators[b]; b++)
        {
            if (S_EQ(op, op_precedence[i].operators[b]))
            {
                *group_out = &op_precedence[i];
                return i;
            }
        }
    }
    return -1;
}

bool Parser::left_has_priority(const char *op_left, const char *op_right)
{
    if (S_EQ(op_left, op_right))
        return false;

    struct expressionable_op_precedence_group *group_left  = nullptr;
    struct expressionable_op_precedence_group *group_right = nullptr;
    int prec_left  = op_precedence_index(op_left,  &group_left);
    int prec_right = op_precedence_index(op_right, &group_right);

    if (group_left && group_left->associtivity == ASSOCIATIVITY_RIGHT_TO_LEFT)
        return false;

    return prec_left <= prec_right;
}

// ===========================================================================
// Expression tree reordering
// ===========================================================================

void Parser::shift_children_left(struct node *node)
{
    assert(node->type == NODE_TYPE_EXPRESSION);
    assert(node->exp.right->type == NODE_TYPE_EXPRESSION);

    const char  *right_op    = node->exp.right->exp.op;
    struct node *new_el      = node->exp.left;
    struct node *new_er      = node->exp.right->exp.left;

    make_exp_node(new_el, new_er, node->exp.op);
    struct node *new_left_sub = node_pop();
    struct node *new_right    = node->exp.right->exp.right;

    node->exp.left  = new_left_sub;
    node->exp.right = new_right;
    node->exp.op    = right_op;
}

void Parser::move_right_left_to_left(struct node *node)
{
    make_exp_node(node->exp.left, node->exp.right->exp.left, node->exp.op);
    struct node *completed = node_pop();

    const char *new_op  = node->exp.right->exp.op;
    node->exp.left  = completed;
    node->exp.right = node->exp.right->exp.right;
    node->exp.op    = new_op;
}

void Parser::reorder_expression(struct node **node_out)
{
    struct node *node = *node_out;
    if (node->type != NODE_TYPE_EXPRESSION)
        return;

    if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
        node->exp.right &&
        node->exp.right->type != NODE_TYPE_EXPRESSION)
        return;

    if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
        node->exp.right &&
        node->exp.right->type == NODE_TYPE_EXPRESSION)
    {
        const char *right_op = node->exp.right->exp.op;
        if (left_has_priority(node->exp.op, right_op))
        {
            shift_children_left(node);
            reorder_expression(&node->exp.left);
            reorder_expression(&node->exp.right);
        }
    }

    if ((is_array_node(node->exp.left) || is_node_assignment(node->exp.right)) ||
        (node_is_expression(node->exp.left, "()") &&
         node_is_expression(node->exp.right, ",")))
    {
        move_right_left_to_left(node);
    }
}
