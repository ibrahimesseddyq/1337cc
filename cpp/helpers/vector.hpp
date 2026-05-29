#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

#define VECTOR_ELEMENT_INCREMENT 20

enum
{
    VECTOR_FLAG_PEEK_DECREMENT = 0b00000001
};

struct vector
{
    void *data;
    int pindex;
    int rindex;
    int mindex;
    int count;
    int flags;
    size_t esize;
    struct vector *saves;
};

struct vector *vector_create(size_t esize);
void vector_free(struct vector *vector);
void *vector_at(struct vector *vector, int index);
void *vector_peek_ptr_at(struct vector *vector, int index);
void *vector_peek_no_increment(struct vector *vector);
void *vector_peek(struct vector *vector);
void *vector_peek_at(struct vector *vector, int index);
void vector_set_flag(struct vector *vector, int flag);
void vector_unset_flag(struct vector *vector, int flag);
void vector_pop_last_peek(struct vector *vector);
void *vector_peek_ptr(struct vector *vector);
void vector_set_peek_pointer(struct vector *vector, int index);
void vector_set_peek_pointer_end(struct vector *vector);
void vector_push(struct vector *vector, void *elem);
void vector_push_at(struct vector *vector, int index, void *ptr);
void vector_pop(struct vector *vector);
void vector_peek_pop(struct vector *vector);
void *vector_back(struct vector *vector);
void *vector_back_or_null(struct vector *vector);
void *vector_back_ptr(struct vector *vector);
void *vector_back_ptr_or_null(struct vector *vector);
const char *vector_string(struct vector *vec);
bool vector_empty(struct vector *vector);
void vector_clear(struct vector *vector);
int vector_count(struct vector *vector);
int vector_fread(struct vector *vector, int amount, FILE *fp);
void *vector_data_ptr(struct vector *vector);
int vector_insert(struct vector *vector_dst, struct vector *vector_src, int dst_index);
int vector_pop_at_data_address(struct vector *vector, void *address);
int vector_pop_value(struct vector *vector, void *val);
void vector_pop_at(struct vector *vector, int index);
void vector_peek_back(struct vector *vector);
int vector_current_index(struct vector *vector);
void vector_save(struct vector *vector);
void vector_restore(struct vector *vector);
void vector_save_purge(struct vector *vector);
size_t vector_element_size(struct vector *vector);
struct vector *vector_clone(struct vector *vector);

// C++ typed helpers to avoid scattered static_cast<> casts at call sites
template <typename T>
inline T *vector_peek_ptr_typed(struct vector *v)
{
    return static_cast<T *>(vector_peek_ptr(v));
}

template <typename T>
inline T *vector_back_ptr_typed(struct vector *v)
{
    return static_cast<T *>(vector_back_ptr(v));
}

template <typename T>
inline T *vector_back_ptr_or_null_typed(struct vector *v)
{
    return static_cast<T *>(vector_back_ptr_or_null(v));
}

template <typename T>
inline T *vector_back_typed(struct vector *v)
{
    return static_cast<T *>(vector_back(v));
}

template <typename T>
inline T *vector_peek_typed(struct vector *v)
{
    return static_cast<T *>(vector_peek(v));
}

template <typename T>
inline T *vector_peek_no_increment_typed(struct vector *v)
{
    return static_cast<T *>(vector_peek_no_increment(v));
}

template <typename T>
inline T *vector_at_typed(struct vector *v, int index)
{
    return static_cast<T *>(vector_at(v, index));
}
