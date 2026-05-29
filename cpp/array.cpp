#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cassert>
#include <cstddef>
#include <cstdlib>

// Creates a new, empty array_brackets container.
// Returns a heap-allocated array_brackets whose internal vector is ready to
// hold bracket nodes. The caller is responsible for freeing it with
// array_brackets_free().
struct array_brackets *array_brackets_new()
{
    struct array_brackets *brackets =
        static_cast<struct array_brackets *>(calloc(1, sizeof(struct array_brackets)));
    brackets->n_brackets = vector_create(sizeof(struct node *));
    return brackets;
}

// Frees a heap-allocated array_brackets container.
// Does not free the individual bracket nodes stored inside — those are owned
// by the AST node tree.
void array_brackets_free(struct array_brackets *brackets)
{
    free(brackets);
}

// Appends a bracket node to the brackets container.
// bracket_node must be of type NODE_TYPE_BRACKET; asserts otherwise.
// Nodes are stored in the order they are added, which mirrors left-to-right
// declaration order (e.g. [4][2] appends the [4] node before the [2] node).
void array_brackets_add(struct array_brackets *brackets, struct node *bracket_node)
{
    assert(bracket_node->type == NODE_TYPE_BRACKET);
    vector_push(brackets->n_brackets, &bracket_node);
}

// Returns the internal vector of bracket node pointers stored in brackets.
// Callers may use this to iterate over every dimension of an array declaration.
struct vector *array_brackets_node_vector(struct array_brackets *brackets)
{
    return brackets->n_brackets;
}

// Calculates the total byte size of an array starting from a given dimension index.
// dtype is the element type whose size field is the base unit. brackets holds
// all dimension nodes. index is the zero-based dimension at which to begin the
// product (pass 0 to cover all dimensions). Returns dtype->size if index is
// already past the last dimension. Each bracket node must contain a
// NODE_TYPE_NUMBER inner node; asserts otherwise. Returns 0 if the vector peek
// returns null before iteration begins.
size_t array_brackets_calculate_size_from_index(struct datatype *dtype,
                                                struct array_brackets *brackets,
                                                int index)
{
    struct vector *array_vec = array_brackets_node_vector(brackets);
    size_t size = dtype->size;

    if (index >= vector_count(array_vec))
        return size;

    vector_set_peek_pointer(array_vec, index);

    struct node *array_bracket_node =
        static_cast<struct node *>(vector_peek_ptr(array_vec));
    if (!array_bracket_node)
        return 0;

    while (array_bracket_node)
    {
        assert(array_bracket_node->bracket.inner->type == NODE_TYPE_NUMBER);
        int number = array_bracket_node->bracket.inner->llnum;
        size *= number;
        array_bracket_node = static_cast<struct node *>(vector_peek_ptr(array_vec));
    }

    return size;
}

// Calculates the total byte size of the full array described by dtype and brackets.
// Equivalent to calling array_brackets_calculate_size_from_index with index 0,
// which multiplies every dimension together with the element size.
size_t array_brackets_calculate_size(struct datatype *dtype, struct array_brackets *brackets)
{
    return array_brackets_calculate_size_from_index(dtype, brackets, 0);
}

// Returns the number of array dimensions declared for dtype.
// Asserts that DATATYPE_FLAG_IS_ARRAY is set on dtype. Each entry in the
// brackets vector corresponds to one dimension, so the return value equals
// the number of [] pairs in the declaration.
int array_total_indexes(struct datatype *dtype)
{
    assert(dtype->flags & DATATYPE_FLAG_IS_ARRAY);
    struct array_brackets *brackets = dtype->array.brackets;
    return vector_count(brackets->n_brackets);
}
