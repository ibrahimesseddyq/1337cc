#include "vector.hpp"
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cstdio>

// ============================================================
// Peek pointer concept
// ============================================================
// Every vector carries an internal "peek pointer" (pindex) that acts as a
// cursor for non-destructive iteration.  It is separate from the write head
// (rindex) so reading elements never removes them.
//
// Typical usage:
//   vector_set_peek_pointer(v, 0);   // rewind to the first element
//   while (void *elem = vector_peek(v)) { ... }
//
// By default vector_peek() advances pindex forward (+1) after each call.
// When VECTOR_FLAG_PEEK_DECREMENT is set the cursor moves backward (-1)
// instead, which lets you iterate from back to front after seeding pindex
// with vector_set_peek_pointer_end().
//
// vector_push() and vector_pop() always operate on the tail (rindex) and
// never touch pindex.
// ============================================================

// Returns true when index is a valid position in the live element range
// [0, rindex).  Used internally to guard random-access reads.
static bool vector_in_bounds_for_at(struct vector *vector, int index)
{
    return (index >= 0 && index < vector->rindex);
}

// Returns true when index is a valid position within the allocated capacity
// [0, mindex).  Used internally to guard pop operations, which must not
// underflow the allocated block.
static bool vector_in_bounds_for_pop(struct vector *vector, int index)
{
    return (index >= 0 && index < vector->mindex);
}

// Asserts that index is within the allocated capacity.  Terminates the
// process via assert() if the check fails.  Called before any pop to catch
// double-pop or underflow bugs early.
static void vector_assert_bounds_for_pop(struct vector *vector, int index)
{
    assert(vector_in_bounds_for_pop(vector, index));
}

// Allocates a fresh vector whose elements are each esize bytes wide.
// Reserves room for VECTOR_ELEMENT_INCREMENT elements up front.
// Does NOT allocate the companion saves vector — use vector_create() for
// the full public constructor.
//   esize  — byte size of a single element (must be > 0)
// Returns a heap-allocated vector with count == 0.
static struct vector *vector_create_no_saves(size_t esize)
{
    struct vector *vec = static_cast<struct vector *>(calloc(sizeof(struct vector), 1));
    vec->data = malloc(esize * VECTOR_ELEMENT_INCREMENT);
    vec->mindex = VECTOR_ELEMENT_INCREMENT;
    vec->rindex = 0;
    vec->pindex = 0;
    vec->esize = esize;
    vec->count = 0;
    return vec;
}

// Returns the total number of bytes currently occupied by live elements
// (count * esize).  Does not include any spare capacity.
static size_t vector_total_size(struct vector *vector)
{
    return static_cast<size_t>(vector->count) * vector->esize;
}

// Returns the byte size of a single element as recorded at creation time.
// Useful when you have a vector pointer but have lost the original esize.
size_t vector_element_size(struct vector *vector)
{
    return vector->esize;
}

// Creates a deep copy of vector.  The clone gets its own heap allocation for
// the element data; the struct fields (rindex, pindex, flags, etc.) are
// copied verbatim.  The saves stack is shared by pointer — do not mutate the
// clone's saves independently of the original.
// Returns a newly heap-allocated vector that the caller owns.
struct vector *vector_clone(struct vector *vector)
{
    void *new_data_address = calloc(vector->esize, static_cast<size_t>(vector->count) + VECTOR_ELEMENT_INCREMENT);
    memcpy(new_data_address, vector->data, vector_total_size(vector));
    struct vector *new_vec = static_cast<struct vector *>(calloc(sizeof(struct vector), 1));
    memcpy(new_vec, vector, sizeof(struct vector));
    new_vec->data = new_data_address;
    return new_vec;
}

// Public constructor.  Creates a vector whose elements are each esize bytes
// wide, together with its companion saves vector used by vector_save() and
// vector_restore().
//   esize  — byte size of a single element (must be > 0)
// Returns a heap-allocated, empty vector ready for use.
struct vector *vector_create(size_t esize)
{
    struct vector *vec = vector_create_no_saves(esize);
    vec->saves = vector_create_no_saves(sizeof(struct vector));
    return vec;
}

// Frees the element data buffer and the vector struct itself.
// Does NOT free the companion saves vector — clean that up separately if
// needed.  After this call the pointer is invalid.
void vector_free(struct vector *vector)
{
    free(vector->data);
    free(vector);
}

// Returns the current write-head index (rindex), i.e. the index at which the
// next push will land.  This equals the number of elements currently in the
// vector.
int vector_current_index(struct vector *vector)
{
    return vector->rindex;
}

// Ensures the backing array can hold at least (start_index + total_elements)
// elements.  If the current capacity (mindex) is already sufficient, this is
// a no-op.  Otherwise realloc() is called to grow the buffer, and mindex is
// updated.  Aborts via assert() if realloc fails.
//   start_index     — base index from which total_elements are counted
//   total_elements  — number of additional elements that must fit
static void vector_resize_for_index(struct vector *vector, int start_index, int total_elements)
{
    if (start_index + total_elements < vector->mindex)
        return;

    vector->data = realloc(vector->data,
        static_cast<size_t>((start_index + total_elements + VECTOR_ELEMENT_INCREMENT)) * vector->esize);
    assert(vector->data);
    vector->mindex = start_index + total_elements;
}

// Ensures the vector can accommodate total_elements more items beyond the
// current write head (rindex).  Delegates to vector_resize_for_index().
static void vector_resize_for(struct vector *vector, int total_elements)
{
    vector_resize_for_index(vector, vector->rindex, total_elements);
}

// Ensures the vector has at least one free slot after the current write head.
// Called automatically by vector_push() after each insertion.
static void vector_resize(struct vector *vector)
{
    vector_resize_for(vector, 0);
}

// Returns a raw pointer to the element at the given index.
// No bounds checking is performed — the caller must guarantee index is in
// [0, count).  The returned pointer is valid until the next realloc (i.e.
// until the next push that triggers a resize).
//   index  — zero-based element index
void *vector_at(struct vector *vector, int index)
{
    return static_cast<char *>(vector->data) + (index * static_cast<int>(vector->esize));
}

// Sets the peek pointer (pindex) to index, making that element the next one
// returned by vector_peek().  Call this before starting a new iteration pass.
// There is no bounds checking here; an out-of-range index will cause the
// first vector_peek() call to return nullptr.
//   index  — desired starting position for the next peek
void vector_set_peek_pointer(struct vector *vector, int index)
{
    vector->pindex = index;
}

// Positions the peek pointer at the last live element (rindex - 1), so that
// the next vector_peek() returns the back of the vector.  Pair with
// VECTOR_FLAG_PEEK_DECREMENT to iterate backward.
void vector_set_peek_pointer_end(struct vector *vector)
{
    vector_set_peek_pointer(vector, vector->rindex - 1);
}

// Returns a pointer to the element at the given index without touching the
// peek pointer.  Returns nullptr if index is out of the live range [0, rindex).
// Useful for random access reads that should not disturb an ongoing iteration.
//   index  — zero-based element index to inspect
void *vector_peek_at(struct vector *vector, int index)
{
    if (!vector_in_bounds_for_at(vector, index))
        return nullptr;
    return vector_at(vector, index);
}

// Returns a pointer to the element at the current peek pointer position
// WITHOUT advancing pindex.  Returns nullptr if pindex is out of bounds.
// Use this to inspect the "current" element during a manual iteration loop
// without moving forward.
void *vector_peek_no_increment(struct vector *vector)
{
    if (!vector_in_bounds_for_at(vector, vector->pindex))
        return nullptr;
    return vector_at(vector, vector->pindex);
}

// Decrements the peek pointer by one without returning an element.
// Useful to step backward by one position during a forward iteration, e.g.
// to "un-consume" the last peeked element.
void vector_peek_back(struct vector *vector)
{
    vector->pindex--;
}

// Advances the peek cursor by one element and returns a pointer to that
// element's data.  Returns nullptr when the cursor has moved past the last
// element.  Use vector_set_peek_pointer() to reset the cursor before
// iterating.
//
// Direction is controlled by flags:
//   Default (no flags)            — pindex increments (+1) after each call,
//                                   iterating front-to-back.
//   VECTOR_FLAG_PEEK_DECREMENT    — pindex decrements (-1) after each call,
//                                   iterating back-to-front (seed pindex with
//                                   vector_set_peek_pointer_end() first).
void *vector_peek(struct vector *vector)
{
    void *ptr = vector_peek_no_increment(vector);
    if (!ptr)
        return nullptr;

    if (vector->flags & VECTOR_FLAG_PEEK_DECREMENT)
        vector->pindex--;
    else
        vector->pindex++;

    return ptr;
}

// Sets the given flag bit(s) in the vector's flags field.
// The only currently defined flag is VECTOR_FLAG_PEEK_DECREMENT, which
// reverses the peek direction (see vector_peek() for details).
//   flag  — bitmask of flag(s) to enable
void vector_set_flag(struct vector *vector, int flag)
{
    vector->flags |= flag;
}

// Clears the given flag bit(s) from the vector's flags field.
//   flag  — bitmask of flag(s) to disable
void vector_unset_flag(struct vector *vector, int flag)
{
    vector->flags &= ~flag;
}

// Advances the peek cursor (like vector_peek()) but interprets the element
// as a void* pointer and dereferences it one extra level.  That is, the
// vector must store void* values, and this function returns the stored
// pointer value rather than a pointer to the slot.
// Returns nullptr if the cursor is out of bounds or the stored pointer is null.
void *vector_peek_ptr(struct vector *vector)
{
    void **ptr = static_cast<void **>(vector_peek(vector));
    if (!ptr)
        return nullptr;
    return *ptr;
}

// Returns the void* value stored at the given absolute index without
// affecting the peek pointer.  The vector must store void* values.
// Returns nullptr if index is out of [0, count) or the stored pointer is null.
//   index  — zero-based slot index to read
void *vector_peek_ptr_at(struct vector *vector, int index)
{
    if (index < 0 || index > vector->count)
        return nullptr;
    void **ptr = static_cast<void **>(vector_at(vector, index));
    if (!ptr)
        return nullptr;
    return *ptr;
}

// Returns the void* value stored in the last (tail) element without removing
// it.  The vector must store void* values.
// Returns nullptr if the vector is empty or the stored pointer is null.
void *vector_back_ptr(struct vector *vector)
{
    void **ptr = static_cast<void **>(vector_back(vector));
    if (!ptr)
        return nullptr;
    return *ptr;
}

// Snapshots the current vector state (all fields except the saves pointer
// itself) onto the companion saves stack.  A subsequent vector_restore()
// will rewind to this exact state — same rindex, pindex, flags, etc.
// The element data buffer is NOT duplicated; only the struct fields are saved.
void vector_save(struct vector *vector)
{
    struct vector tmp_vec = *vector;
    tmp_vec.saves = nullptr;
    vector_push(vector->saves, &tmp_vec);
}

// Pops the most recently saved snapshot from the saves stack and restores all
// vector fields to those saved values.  The saves pointer is preserved so the
// saves stack itself is not lost.  After restoration the snapshot is removed
// from the saves stack.
// Note: because element data is not saved/restored, any push/pop operations
// performed between vector_save() and vector_restore() may leave stale data
// in the buffer even though rindex is rewound.
void vector_restore(struct vector *vector)
{
    struct vector save_vec = *static_cast<struct vector *>(vector_back(vector->saves));
    save_vec.saves = vector->saves;
    *vector = save_vec;
    vector_pop(vector->saves);
}

// Discards the most recently saved snapshot without restoring it.  Use when
// you want to commit changes made since the last vector_save() and throw
// away the ability to roll back.
void vector_save_purge(struct vector *vector)
{
    vector_pop(vector->saves);
}

// Removes the element that was most recently returned by vector_peek(), i.e.
// the element at (pindex - 1).  Asserts that pindex >= 1.
// This is useful when you want to consume (delete) the element you just
// inspected during a peek loop.
void vector_pop_last_peek(struct vector *vector)
{
    assert(vector->pindex >= 1);
    vector_pop_at(vector, vector->pindex - 1);
}

// Appends a copy of the element pointed to by elem to the tail of the vector.
// elem must point to at least esize bytes of data.  rindex and count are both
// incremented.  If the backing buffer is full after the push, it is grown
// automatically via vector_resize().
//   elem  — pointer to the source element data to copy into the vector
void vector_push(struct vector *vector, void *elem)
{
    void *ptr = vector_at(vector, vector->rindex);
    memcpy(ptr, elem, vector->esize);

    vector->rindex++;
    vector->count++;

    if (vector->rindex >= vector->mindex)
        vector_resize(vector);
}

// Reads elements from the file fp one byte at a time and pushes each byte
// into the vector.  Stops when fread returns 0 (EOF or error).
// Note: the amount parameter is currently unused; exactly one byte is read
// per iteration regardless.
// Returns 0.
//   amount  — ignored (reserved for future use)
//   fp      — open FILE* to read from
int vector_fread(struct vector *vector, int /*amount*/, FILE *fp)
{
    size_t read_amount = fread(vector->data, 1, 1, fp);
    while (read_amount)
    {
        vector_push(vector, &read_amount);
        read_amount = fread(vector->data, 1, 1, fp);
    }
    return 0;
}

// Returns the element data buffer reinterpreted as a null-terminated C
// string.  Only valid when the vector was used to accumulate character data
// and the caller has ensured a '\0' terminator is present.
const char *vector_string(struct vector *vec)
{
    return static_cast<const char *>(vec->data);
}

// Returns a pointer to the byte immediately past the last live element
// (i.e. &data[rindex * esize]).  Used internally to compute the byte span
// of a range that needs to be memmoved during pop operations.
static void *vector_data_end(struct vector *vector)
{
    return static_cast<char *>(vector->data) + vector->rindex * static_cast<int>(vector->esize);
}

// Returns the number of live elements from index to the end of the vector
// (count - index).  Used internally to calculate how many bytes to shift
// when removing an element mid-array.
//   index  — starting element index
static size_t vector_elements_until_end(struct vector *vector, int index)
{
    return static_cast<size_t>(vector->count - index);
}

// Shifts all elements from index onward to the right by amount slots,
// zeroing out the vacated slots at [index, index+amount).  Grows the
// buffer if needed.  Does NOT update rindex or count.
//   index   — first element to move rightward
//   amount  — number of slots to shift by
static void vector_shift_right_in_bounds_no_increment(struct vector *vector, int index, int amount)
{
    vector_resize_for_index(vector, index, amount);
    int eindex = (index + amount);
    size_t bytes_to_move = vector_elements_until_end(vector, index) * vector->esize;
    memcpy(vector_at(vector, eindex), vector_at(vector, index), bytes_to_move);
    memset(vector_at(vector, index), 0x00, static_cast<size_t>(amount) * vector->esize);
}

// Same as vector_shift_right_in_bounds_no_increment() but also increments
// rindex and count by amount to reflect the new logical size after the
// insertion gap has been opened.
//   index   — first element to move rightward
//   amount  — number of slots to open up
static void vector_shift_right_in_bounds(struct vector *vector, int index, int amount)
{
    vector_shift_right_in_bounds_no_increment(vector, index, amount);
    vector->rindex += amount;
    vector->count += amount;
}

// Extends the vector so that index is a valid position.  If index is already
// within the live range (< rindex) this is a no-op.  Otherwise rindex and
// count are both set to index, effectively filling the gap with whatever
// bytes happen to be in the buffer (they will be zeroed if the buffer was
// newly allocated via calloc, but not guaranteed otherwise).
//   index  — the minimum rindex the vector should have after the call
static void vector_stretch(struct vector *vector, int index)
{
    if (index < vector->rindex)
        return;
    vector_resize_for_index(vector, index, 0);
    vector->count = index;
    vector->rindex = index;
}

// Searches for the first element whose stored void* value equals val and
// removes it.  Iterates front-to-back using the peek pointer, which is saved
// and restored around the search so the caller's iteration is not disturbed.
// Returns the index at which the matching element was found (or the final
// index if no match was found).
//   val  — pointer value to search for (compared by address, not content)
int vector_pop_value(struct vector *vector, void *val)
{
    int old_pp = vector->pindex;
    vector_set_peek_pointer(vector, 0);
    void *ptr = vector_peek_ptr(vector);
    int index = 0;
    while (ptr)
    {
        if (ptr == val)
        {
            vector_pop_at(vector, index);
            break;
        }
        ptr = vector_peek_ptr(vector);
        index++;
    }
    vector_set_peek_pointer(vector, old_pp);
    return index;
}

// Removes the element whose in-buffer memory address is address.  The index
// is computed from the pointer offset: (address - data) / esize.
// Returns the zero-based index of the removed element.
//   address  — pointer to the element's first byte inside the data buffer
int vector_pop_at_data_address(struct vector *vector, void *address)
{
    int index = static_cast<int>((static_cast<char *>(address) - static_cast<char *>(vector->data))
                                 / static_cast<int>(vector->esize));
    vector_pop_at(vector, index);
    return index;
}

// Opens a gap of amount slots starting at index to make room for new elements.
// If index is within the live range the gap is opened with the full shift-and-
// increment path.  If index is beyond the live range the vector is stretched
// to reach it and the gap is opened without double-incrementing rindex/count.
//   index   — position at which the gap should start
//   amount  — number of slots to open up
static void vector_shift_right(struct vector *vector, int index, int amount)
{
    if (index < vector->rindex)
    {
        vector_shift_right_in_bounds(vector, index, amount);
        return;
    }
    vector_stretch(vector, index + amount);
    vector_shift_right_in_bounds_no_increment(vector, index, amount);
}

// Removes the element at index by shifting all subsequent elements one slot
// to the left (memmove semantics via memcpy from next element to end).
// Decrements both count and rindex.  Does NOT zero the now-vacated last slot.
//   index  — zero-based index of the element to remove
void vector_pop_at(struct vector *vector, int index)
{
    void *dst_pos = vector_at(vector, index);
    void *next_element_pos = static_cast<char *>(dst_pos) + vector->esize;
    void *end_pos = vector_data_end(vector);
    size_t total = static_cast<size_t>(static_cast<char *>(end_pos) - static_cast<char *>(next_element_pos));
    memcpy(dst_pos, next_element_pos, total);
    vector->count -= 1;
    vector->rindex -= 1;
}

// Removes the element currently pointed to by the peek pointer (pindex).
// Equivalent to vector_pop_at(vector, pindex).  Useful inside a peek loop
// when you want to delete the element you are currently visiting without
// having to track the index yourself.
void vector_peek_pop(struct vector *vector)
{
    vector_pop_at(vector, vector->pindex);
}

// Inserts total elements from the contiguous array at ptr into the vector
// starting at dst_index.  Existing elements at and after dst_index are
// shifted right to make room.
//   dst_index  — zero-based position at which the first new element lands
//   ptr        — source array; must contain at least total * esize bytes
//   total      — number of elements to insert
static void vector_push_multiple_at(struct vector *vector, int dst_index, void *ptr, int total)
{
    vector_shift_right(vector, dst_index, total);
    void *dst_ptr = vector_at(vector, dst_index);
    size_t total_bytes = static_cast<size_t>(total) * vector->esize;
    memcpy(dst_ptr, ptr, total_bytes);
}

// Inserts a single element at the given index, shifting all elements at and
// after index one slot to the right.  After the call the new element occupies
// index and the element that previously lived there is at index+1.
//   index  — zero-based position for the new element
//   ptr    — pointer to esize bytes of data to copy into the slot
void vector_push_at(struct vector *vector, int index, void *ptr)
{
    vector_shift_right(vector, index, 1);
    void *data_ptr = vector_at(vector, index);
    memcpy(data_ptr, ptr, vector->esize);
}

// Inserts all elements from vector_src into vector_dst starting at dst_index.
// Both vectors must have the same esize; returns -1 if they differ, 0 on
// success.  Elements in vector_dst at and after dst_index are shifted right
// to make room.
//   vector_dst  — destination vector (modified in place)
//   vector_src  — source vector (read-only)
//   dst_index   — position in vector_dst where the first src element lands
int vector_insert(struct vector *vector_dst, struct vector *vector_src, int dst_index)
{
    if (vector_dst->esize != vector_src->esize)
        return -1;
    vector_push_multiple_at(vector_dst, dst_index,
                             vector_at(vector_src, 0),
                             vector_count(vector_src));
    return 0;
}

// Removes the last element (tail pop) by decrementing rindex and count.
// Aborts via assert() if the resulting rindex would be out of the allocated
// capacity — this catches underflow when the vector is already empty.
void vector_pop(struct vector *vector)
{
    vector->rindex -= 1;
    vector->count -= 1;
    vector_assert_bounds_for_pop(vector, vector->rindex);
}

// Returns a raw pointer to the start of the element data buffer.
// Useful when you need to pass the entire array to a C API or do bulk
// copies.  The pointer is invalidated by any operation that triggers a resize.
void *vector_data_ptr(struct vector *vector)
{
    return vector->data;
}

// Returns true if the vector contains no elements (count == 0).
bool vector_empty(struct vector *vector)
{
    return vector_count(vector) == 0;
}

// Removes all elements by repeatedly calling vector_pop() until the vector
// is empty.  The backing buffer is NOT freed or shrunk; capacity is retained.
void vector_clear(struct vector *vector)
{
    while (vector_count(vector))
        vector_pop(vector);
}

// Returns a pointer to the last live element, or nullptr if the vector is
// empty.  Does not modify any index.
void *vector_back_or_null(struct vector *vector)
{
    if (!vector_in_bounds_for_at(vector, vector->rindex - 1))
        return nullptr;
    return vector_at(vector, vector->rindex - 1);
}

// Returns the void* value stored in the last element, or nullptr if the
// vector is empty or the stored pointer is null.  The vector must store
// void* values.
void *vector_back_ptr_or_null(struct vector *vector)
{
    void **ptr = static_cast<void **>(vector_back_or_null(vector));
    if (ptr)
        return *ptr;
    return nullptr;
}

// Returns a pointer to the last live element.  Aborts via assert() if the
// vector is empty (would be out of allocated bounds).  Use vector_back_or_null()
// if you need a safe version that returns nullptr on an empty vector.
void *vector_back(struct vector *vector)
{
    vector_assert_bounds_for_pop(vector, vector->rindex - 1);
    return vector_at(vector, vector->rindex - 1);
}

// Returns the number of live elements currently in the vector.
// This is always <= the allocated capacity (mindex).
int vector_count(struct vector *vector)
{
    return vector->count;
}
