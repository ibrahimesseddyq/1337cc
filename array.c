#include "compiler.h"
#include "helpers/vector.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

/**
 * @brief Create a new array_brackets object.
 *
 * An array_brackets object represents the dimensions of an array
 * (e.g., `int arr[5][10];` has 2 brackets).
 * Each bracket is represented as a `node*` stored in a vector.
 *
 * @return A pointer to a newly allocated array_brackets structure.
 *         Caller must free with array_brackets_free().
 */
struct array_brackets* array_brackets_new()
{
    struct array_brackets* brackets = calloc(1, sizeof(struct array_brackets));
    brackets->n_brackets = vector_create(sizeof(struct node*));
    return brackets;
}

/**
 * @brief Free an array_brackets object.
 *
 * Note: This does not free the nodes stored inside, only the container.
 *
 * @param brackets Pointer to the array_brackets object to free.
 */
void array_brackets_free(struct array_brackets* brackets)
{
    free(brackets);
}

/**
 * @brief Add a bracket node to an array_brackets object.
 *
 * Each bracket corresponds to one dimension of an array (e.g., `[10]`).
 * Only nodes of type NODE_TYPE_BRACKET are valid.
 *
 * @param brackets The array_brackets object.
 * @param bracket_node The node representing a single bracket expression.
 */
void array_brackets_add(struct array_brackets* brackets, struct node* bracket_node)
{
    assert(bracket_node->type == NODE_TYPE_BRACKET);
    vector_push(brackets->n_brackets, &bracket_node);
}

/**
 * @brief Get the underlying vector of bracket nodes.
 *
 * @param brackets The array_brackets object.
 * @return A pointer to the vector holding `struct node*` entries.
 */
struct vector* array_brackets_node_vector(struct array_brackets* brackets)
{
    return brackets->n_brackets;
}

/**
 * @brief Calculate the total size of an array type starting from a given bracket index.
 *
 * This multiplies the base datatype size by the sizes of each array dimension
 * starting at `index`.
 *
 * Example:
 * If dtype->size = 4 (int) and array has brackets [2][3][4],
 * - from index 0 → size = 4 * 2 * 3 * 4 = 96
 * - from index 1 → size = 4 * 3 * 4 = 48
 * - from index 2 → size = 4 * 4 = 16
 *
 * @param dtype    The base datatype (with size field set).
 * @param brackets The array_brackets object holding dimension nodes.
 * @param index    The starting index (0 = include all brackets).
 * @return The total size in bytes from that index, or 0 on error.
 */
size_t array_brackets_calculate_size_from_index(struct datatype* dtype, struct array_brackets* brackets, int index)
{
    struct vector* array_vec = array_brackets_node_vector(brackets);
    size_t size = dtype->size;

    if (index >= vector_count(array_vec))
    {
        return size;
    }

    vector_set_peek_pointer(array_vec, index);

    struct node* array_bracket_node = vector_peek_ptr(array_vec);
    if (!array_bracket_node)
    {
        return 0;
    }

    while (array_bracket_node)
    {
        assert(array_bracket_node->bracket.inner->type == NODE_TYPE_NUMBER);
        int number = array_bracket_node->bracket.inner->llnum;
        size *= number;
        array_bracket_node = vector_peek_ptr(array_vec);
    }

    return size;
}

/**
 * @brief Calculate the total size of an array type from the first bracket.
 *
 * Equivalent to calling array_brackets_calculate_size_from_index() with index = 0.
 *
 * @param dtype    The base datatype.
 * @param brackets The array_brackets object.
 * @return The full size in bytes of the array type.
 */
size_t array_brackets_calculate_size(struct datatype* dtype, struct array_brackets* brackets)
{
    return array_brackets_calculate_size_from_index(dtype, brackets, 0);
}

/**
 * @brief Get the total number of dimension brackets in an array datatype.
 *
 * @param dtype A datatype flagged as an array (DATATYPE_FLAG_IS_ARRAY).
 * @return The number of dimensions in the array (e.g., 2 for int[3][4]).
 */
int array_total_indexes(struct datatype* dtype)
{
    assert(dtype->flags & DATATYPE_FLAG_IS_ARRAY);
    struct array_brackets* brackets = dtype->array.brackets;
    return vector_count(brackets->n_brackets);
}
