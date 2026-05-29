#include "compiler.hpp"
#include <cstddef>

// Returns true if name is the keyword "struct" or "union".
// Used during parsing to detect the start of a struct/union type specifier
// before a full datatype object is available.
bool datatype_is_struct_or_union_for_name(const char *name)
{
    return S_EQ(name, "union") || S_EQ(name, "struct");
}

// Returns true if dtype represents a struct or union type.
bool datatype_is_struct_or_union(struct datatype *dtype)
{
    return dtype->type == DATA_TYPE_STRUCT || dtype->type == DATA_TYPE_UNION;
}

// Returns the element stride to use when computing array-access offsets.
// For a struct/union pointer with exactly one level of indirection, returns
// the full struct size (sizeof *ptr) rather than the pointer size, because
// pointer arithmetic on a struct pointer steps by the struct's size. For
// all other types, delegates to datatype_size().
size_t datatype_size_for_array_access(struct datatype *dtype)
{
    if (datatype_is_struct_or_union(dtype) &&
        (dtype->flags & DATATYPE_FLAG_IS_POINTER) &&
        dtype->pointer_depth == 1)
    {
        return dtype->size;
    }
    return datatype_size(dtype);
}

// Returns the size of a single element of dtype, ignoring array dimensions.
// If dtype is a pointer at any depth, returns DATA_SIZE_DWORD (the machine
// pointer size). Otherwise returns the raw dtype->size field.
size_t datatype_element_size(struct datatype *dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_POINTER)
        return DATA_SIZE_DWORD;
    return dtype->size;
}

// Returns the byte size of dtype, ignoring pointer indirection but respecting arrays.
// If dtype is an array, returns the precomputed total array size. Otherwise
// returns dtype->size. Unlike datatype_size(), pointer depth is not considered,
// so a pointer-to-array still reports the array's storage size.
size_t datatype_size_no_ptr(struct datatype *dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_ARRAY)
        return dtype->array.size;
    return dtype->size;
}

// Returns the effective byte size of dtype as seen by the code generator.
// Pointers at any non-zero depth always return DATA_SIZE_DWORD regardless of
// the pointed-to type. Arrays return the precomputed total array size.
// Plain scalar types return dtype->size.
size_t datatype_size(struct datatype *dtype)
{
    if ((dtype->flags & DATATYPE_FLAG_IS_POINTER) && dtype->pointer_depth > 0)
        return DATA_SIZE_DWORD;
    if (dtype->flags & DATATYPE_FLAG_IS_ARRAY)
        return dtype->array.size;
    return dtype->size;
}

// Returns true if dtype is not a struct or union (i.e. it is a primitive type).
bool datatype_is_primitive(struct datatype *dtype)
{
    return !datatype_is_struct_or_union(dtype);
}
