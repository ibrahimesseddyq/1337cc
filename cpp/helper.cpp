#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cassert>
#include <cstddef>

// Returns the byte size of a single variable node's data type.
// Asserts that var_node is of type NODE_TYPE_VARIABLE; it is a programming
// error to pass any other node kind.
// var_node  — a NODE_TYPE_VARIABLE node whose type size is needed.
size_t variable_size(struct node *var_node)
{
    assert(var_node->type == NODE_TYPE_VARIABLE);
    return datatype_size(&var_node->var.type);
}

// Sums the sizes of all variable nodes inside a NODE_TYPE_VARIABLE_LIST node.
// Iterates the list vector and accumulates the result of variable_size() for
// each entry.  Asserts that var_list_node has type NODE_TYPE_VARIABLE_LIST.
// Returns the total byte count of all variables in the list.
// var_list_node  — a NODE_TYPE_VARIABLE_LIST node whose member sizes are summed.
size_t variable_size_for_list(struct node *var_list_node)
{
    assert(var_list_node->type == NODE_TYPE_VARIABLE_LIST);
    size_t size = 0;
    vector_set_peek_pointer(var_list_node->var_list.list, 0);
    struct node *var_node =
        static_cast<struct node *>(vector_peek_ptr(var_list_node->var_list.list));
    while (var_node)
    {
        size += variable_size(var_node);
        var_node = static_cast<struct node *>(vector_peek_ptr(var_list_node->var_list.list));
    }
    return size;
}

// Returns the body node of the struct or union type held by a variable node.
// Used to walk into the member list of an inline struct or union variable when
// computing layout.
// Returns nullptr if the node is not a struct/union variable, or if neither
// branch matches (which should not happen for a well-formed AST).
// node  — a variable node that may hold a struct or union data type.
struct node *variable_struct_or_union_body_node(struct node *node)
{
    if (!node_is_struct_or_union_variable(node))
        return nullptr;

    if (node->var.type.type == DATA_TYPE_STRUCT)
        return node->var.type.struct_node->_struct.body_n;

    if (node->var.type.type == DATA_TYPE_UNION)
        return node->var.type.union_node->_union.body_n;

    return nullptr;
}

// Computes the number of bytes that must be inserted after a value of size val
// to bring the next offset up to a multiple of to (the alignment requirement).
// Returns 0 when val is already aligned or when to is <= 0.
// val  — the current byte offset or size that may need padding.
// to   — the alignment boundary in bytes (e.g. 4 for 32-bit alignment).
int padding(int val, int to)
{
    if (to <= 0)
        return 0;
    if ((val % to) == 0)
        return 0;
    return to - (val % to) % to;
}

// Rounds val up to the nearest multiple of to by adding the required padding.
// If val is already a multiple of to it is returned unchanged.
// val  — the value to align.
// to   — the alignment boundary in bytes.
int align_value(int val, int to)
{
    if (val % to)
        val += padding(val, to);
    return val;
}

// Aligns val to to, but first flips the sign of to when val is non-positive so
// that negative offsets (e.g. stack-frame offsets that grow downward) are
// aligned in the correct direction.  Asserts that to is non-negative on entry.
// val  — the signed offset to align; may be zero or negative for stack slots.
// to   — the non-negative alignment boundary in bytes.
int align_value_treat_positive(int val, int to)
{
    assert(to >= 0);
    if (val <= 0)
        to = -to;
    return align_value(val, to);
}

// Sums the padding field stored on every NODE_TYPE_VARIABLE node in vec.
// Non-variable nodes are silently skipped.  The result represents the total
// inter-member padding inserted during struct layout, which is useful when
// computing the true allocated size of a struct.
// Returns the accumulated padding in bytes.
// vec  — a vector of node pointers (typically the member list of a struct body).
int compute_sum_padding(struct vector *vec)
{
    int pad = 0;
    vector_set_peek_pointer(vec, 0);

    struct node *cur_node  = static_cast<struct node *>(vector_peek_ptr(vec));
    struct node *last_node = nullptr;

    while (cur_node)
    {
        if (cur_node->type != NODE_TYPE_VARIABLE)
        {
            cur_node = static_cast<struct node *>(vector_peek_ptr(vec));
            continue;
        }

        pad      += cur_node->var.padding;
        last_node = cur_node;
        cur_node  = static_cast<struct node *>(vector_peek_ptr(vec));
    }

    return pad;
}
