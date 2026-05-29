#include <cstdio>
#include <cstdlib>
#include "compiler.hpp"
#include "helpers/vector.hpp"

// Opens `filename` for reading and, when `filename_out` is non-null, opens
// `filename_out` for writing.  Allocates and zero-initialises a
// compile_process, wires up the file handles, allocates the node and
// node-tree vectors, stores `flags`, and initialises the symbol-resolver with
// a fresh symbol table.
//
// filename     — path to the C source file to be compiled (opened read-only).
// filename_out — path for the compiler output file (opened for writing), or
//                nullptr if no output file is needed.
// flags        — bitmask of compiler option flags stored verbatim on the
//                returned process.
//
// Returns a pointer to the newly allocated compile_process on success.
// Returns nullptr if `filename` cannot be opened, or if `filename_out` is
// non-null and cannot be opened (in which case the source file is closed
// before returning).
struct compile_process *compile_process_create(const char *filename, const char *filename_out, int flags)
{
    FILE *file = fopen(filename, "r");
    if (!file)
        return nullptr;

    FILE *out_file = nullptr;
    if (filename_out)
    {
        out_file = fopen(filename_out, "w");
        if (!out_file)
        {
            fclose(file);
            return nullptr;
        }
    }

    struct compile_process *process =
        static_cast<struct compile_process *>(calloc(1, sizeof(struct compile_process)));
    process->node_vec      = vector_create(sizeof(struct node *));
    process->node_tree_vec = vector_create(sizeof(struct node *));

    process->flags       = flags;
    process->cfile.fp    = file;
    process->cfile.abs_path = filename;
    process->ofile       = out_file;

    symresolver_initialize(process);
    symresolver_new_table(process);

    return process;
}

// Reads and consumes the next character from the source file associated with
// `lex_proc`.  Updates the compiler's column counter on every call, and when
// a newline is encountered resets the column to 1 and increments the line
// counter so that position tracking in `compiler->pos` stays accurate.
//
// lex_proc — the active lex_process; its embedded compiler pointer is used to
//            reach the source file handle and the position state.
//
// Returns the character read, or EOF (cast to char) when the source file is
// exhausted.
char compile_process_next_char(struct lex_process *lex_proc)
{
    struct compile_process *compiler = lex_proc->compiler;
    compiler->pos.col += 1;
    char c = static_cast<char>(getc(compiler->cfile.fp));
    if (c == '\n')
    {
        compiler->pos.line += 1;
        compiler->pos.col = 1;
    }
    return c;
}

// Returns the next character from the source file without consuming it, so the
// subsequent call to compile_process_next_char will return the same character.
// The compiler position state is not updated.
//
// lex_proc — the active lex_process; its embedded compiler pointer is used to
//            reach the source file handle.
//
// Returns the peeked character, or EOF (cast to char) when the source file is
// exhausted.
char compile_process_peek_char(struct lex_process *lex_proc)
{
    struct compile_process *compiler = lex_proc->compiler;
    char c = static_cast<char>(getc(compiler->cfile.fp));
    ungetc(c, compiler->cfile.fp);
    return c;
}

// Pushes `c` back onto the source file stream so it will be returned as the
// next character read by compile_process_next_char or
// compile_process_peek_char.  The compiler position state is not adjusted,
// so the caller is responsible for any position bookkeeping if needed.
//
// lex_proc — the active lex_process; its embedded compiler pointer is used to
//            reach the source file handle.
// c        — the character to push back onto the stream.
void compile_process_push_char(struct lex_process *lex_proc, char c)
{
    struct compile_process *compiler = lex_proc->compiler;
    ungetc(c, compiler->cfile.fp);
}
