#include "compiler.h"
#include <stddef.h>

/**
 * @brief Check if a given type name represents a struct or union.
 *
 * @param name The type name to check (e.g., "struct", "union").
 * @return true if the name is "struct" or "union", false otherwise.
 */
bool datatype_is_struct_or_union_for_name(const char* name)
{
    return S_EQ(name, "union") || S_EQ(name, "struct");
}

size_t datatype_size(struct datatype* dtype);

/**
 * @brief Determine if a datatype is either a struct or a union.
 *
 * @param dtype The datatype to check.
 * @return true if the type is DATA_TYPE_STRUCT or DATA_TYPE_UNION, false otherwise.
 */
bool datatype_is_struct_or_union(struct datatype* dtype)
{
    return dtype->type == DATA_TYPE_STRUCT || dtype->type == DATA_TYPE_UNION;
}

/**
 * @brief Compute the size of a datatype when accessed as an array element.
 *
 * Special handling:
 * - If the type is a struct or union and is represented as a single-level pointer,
 *   return the size of the struct/union instead of a pointer size.
 * - Otherwise, fallback to datatype_size().
 *
 * @param dtype The datatype to compute size for.
 * @return The size in bytes of the datatype when accessed as an array element.
 */
size_t datatype_size_for_array_access(struct datatype* dtype)
{
    if (datatype_is_struct_or_union(dtype) && dtype->flags & DATATYPE_FLAG_IS_POINTER && dtype->pointer_depth == 1)
    {
        return dtype->size;
    }   
    return datatype_size(dtype);
}

/**
 * @brief Get the size of one element of the datatype.
 *
 * Rules:
 * - If it's a pointer type, the element size is always a DWORD (usually 4 bytes).
 * - Otherwise, return the underlying datatype size.
 *
 * @param dtype The datatype to compute element size for.
 * @return The element size in bytes.
 */
size_t datatype_element_size(struct datatype* dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_POINTER)
    {
        return DATA_SIZE_DWORD;
    }
    return dtype->size;
}

/**
 * @brief Get the size of the datatype, ignoring pointer indirection.
 *
 * - For arrays, this returns the full array size.
 * - For other types, return the base size.
 *
 * @param dtype The datatype to compute size for.
 * @return The size in bytes of the datatype excluding pointer handling.
 */
size_t datatype_size_no_ptr(struct datatype* dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_ARRAY)
    {
        return dtype->array.size;
    }
    return dtype->size;
}

/**
 * @brief Get the effective size of a datatype.
 *
 * Rules:
 * - If it's a pointer (pointer_depth > 0), return DWORD size (pointer size).
 * - If it's an array, return the full array size.
 * - Otherwise, return the base type size.
 *
 * @param dtype The datatype to compute size for.
 * @return The size in bytes of the datatype.
 */
size_t datatype_size(struct datatype* dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_POINTER && dtype->pointer_depth > 0)
    {
        return DATA_SIZE_DWORD;
    }
    if (dtype->flags & DATATYPE_FLAG_IS_ARRAY)
    {
        return dtype->array.size;
    }
    return dtype->size;
}

/**
 * @brief Determine if a datatype is primitive (i.e., not a struct or union).
 *
 * @param dtype The datatype to check.
 * @return true if the type is not a struct or union, false otherwise.
 */
bool datatype_is_primitive(struct datatype* dtype)
{
    return !datatype_is_struct_or_union(dtype);
}
