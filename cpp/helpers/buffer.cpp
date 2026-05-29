#include "buffer.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

// Allocates and initialises a new, empty buffer.
// The backing char array is pre-allocated to BUFFER_REALLOC_AMOUNT bytes so
// that small writes do not immediately trigger a realloc.  len (bytes written)
// and rindex (read cursor) start at 0.
// Returns a heap-allocated buffer that the caller owns.
struct buffer *buffer_create()
{
    struct buffer *buf = static_cast<struct buffer *>(calloc(sizeof(struct buffer), 1));
    buf->data = static_cast<char *>(calloc(BUFFER_REALLOC_AMOUNT, 1));
    buf->len = 0;
    buf->msize = BUFFER_REALLOC_AMOUNT;
    return buf;
}

// Grows the backing array by size bytes via realloc() and updates msize.
// Does not change len or rindex.  Called by buffer_need() when a write would
// overflow the current allocation, and can also be called directly when you
// know in advance how much extra space is required.
//   size  — number of additional bytes to append to the allocation
void buffer_extend(struct buffer *buffer, size_t size)
{
    buffer->data = static_cast<char *>(realloc(buffer->data, static_cast<size_t>(buffer->msize) + size));
    buffer->msize += static_cast<int>(size);
}

// Ensures the buffer has at least size bytes of free space beyond the current
// write position (len).  If the available space (msize - len) is insufficient,
// the buffer is extended by size + BUFFER_REALLOC_AMOUNT bytes so that future
// small writes also have room without immediately triggering another realloc.
//   size  — minimum number of additional bytes that must be available
static void buffer_need(struct buffer *buffer, size_t size)
{
    if (static_cast<size_t>(buffer->msize) <= static_cast<size_t>(buffer->len) + size)
    {
        size += BUFFER_REALLOC_AMOUNT;
        buffer_extend(buffer, size);
    }
}

// Formats a string using printf-style fmt and appends it to the buffer,
// including the null terminator written by vsnprintf.  The buffer is extended
// by 2048 bytes before formatting to guarantee enough room for the formatted
// output; len is advanced by the number of bytes written (including the '\0').
// Use buffer_printf_no_terminator() if you want to chain multiple printf
// calls into a single contiguous string without embedded terminators.
//   fmt  — printf-style format string
//   ...  — format arguments
void buffer_printf(struct buffer *buffer, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int index = buffer->len;
    int len = 2048;
    buffer_extend(buffer, static_cast<size_t>(len));
    int actual_len = vsnprintf(&buffer->data[index], static_cast<size_t>(len), fmt, args);
    buffer->len += actual_len;
    va_end(args);
}

// Same as buffer_printf() but does NOT count the null terminator in len.
// This means the '\0' written by vsnprintf is overwritten by the next write,
// making it suitable for building up a string from multiple formatted pieces
// without gaps or embedded terminators between them.
//   fmt  — printf-style format string
//   ...  — format arguments
void buffer_printf_no_terminator(struct buffer *buffer, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int index = buffer->len;
    int len = 2048;
    buffer_extend(buffer, static_cast<size_t>(len));
    int actual_len = vsnprintf(&buffer->data[index], static_cast<size_t>(len), fmt, args);
    buffer->len += actual_len - 1;
    va_end(args);
}

// Appends a single byte c to the buffer, growing the backing array if needed.
// len is incremented by one.  This is the primary way the lexer accumulates
// characters while scanning a token.
//   c  — the byte to append
void buffer_write(struct buffer *buffer, char c)
{
    buffer_need(buffer, sizeof(char));
    buffer->data[buffer->len] = c;
    buffer->len++;
}

// Returns a void* pointer to the start of the buffer's data array.
// The pointer is valid until the next buffer_extend() or buffer_write() that
// triggers a realloc.  Cast to char* to access individual bytes.
void *buffer_ptr(struct buffer *buffer)
{
    return buffer->data;
}

// Reads and returns the next byte at the current read cursor (rindex) and
// advances rindex by one.  Returns (char)-1 (i.e. 0xFF) when the read cursor
// has reached or passed len, signalling end-of-buffer.
// The write side (len / buffer_write) and the read side (rindex / buffer_read)
// are independent — reading does not affect the bytes stored in the buffer.
char buffer_read(struct buffer *buffer)
{
    if (buffer->rindex >= buffer->len)
        return static_cast<char>(-1);
    char c = buffer->data[buffer->rindex];
    buffer->rindex++;
    return c;
}

// Returns the byte at the current read cursor (rindex) WITHOUT advancing it.
// Returns (char)-1 (i.e. 0xFF) when the read cursor is at or past len.
// Use buffer_read() to both peek and consume, or call buffer_read() after
// inspecting with this function.
char buffer_peek(struct buffer *buffer)
{
    if (buffer->rindex >= buffer->len)
        return static_cast<char>(-1);
    return buffer->data[buffer->rindex];
}

// Frees the backing data array and the buffer struct itself.
// After this call the pointer is invalid; do not access any buffer fields.
void buffer_free(struct buffer *buffer)
{
    free(buffer->data);
    free(buffer);
}
