#include "compiler.h"
#include <stddef.h>


bool datatype_is_struct_or_union_for_name(const char* name)
{
    return S_EQ(name, "union") || S_EQ(name, "struct");
}
size_t datatype_size(struct datatype* dtype);
bool datatype_is_struct_or_union(struct datatype* dtype)
{
    return dtype->type == DATA_TYPE_STRUCT || dtype->type == DATA_TYPE_UNION;
}
size_t datatype_size_for_array_access(struct datatype* dtype)
{
    if (datatype_is_struct_or_union(dtype) && dtype->flags & DATATYPE_FLAG_IS_POINTER && dtype->pointer_depth == 1)
    {
        return dtype->size;
    }   
    return datatype_size(dtype);
}
size_t datatype_element_size(struct datatype* dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_POINTER)
    {
        return DATA_SIZE_DWORD;
    }
    return dtype->size;
}
size_t datatype_size_no_ptr(struct datatype* dtype)
{
    if (dtype->flags & DATATYPE_FLAG_IS_ARRAY)
    {
        return dtype->array.size;
    }
    return dtype->size;
}
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