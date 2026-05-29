#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdlib>

// Allocates and initialises a lex_process — the working state used by the
// lexer for a single tokenisation pass.  The token output vector is created
// empty, the source position is set to line 1 / column 1, and the supplied
// I/O function table and private data pointer are stored verbatim.
//
// compiler     — the parent compile_process that owns the source file;
//                stored on the lex_process so the I/O callbacks can reach it.
// functions    — table of character-level I/O callbacks (next_char,
//                peek_char, push_char) that the lexer will call to read the
//                source stream.
// private_data — arbitrary pointer the caller can use to attach extra state;
//                pass nullptr if not needed.
//
// Returns a pointer to the newly allocated lex_process.  Never returns
// nullptr in the current implementation (calloc failure is not handled).
struct lex_process *lex_process_create(struct compile_process *compiler,
                                       struct lex_process_functions *functions,
                                       void *private_data)
{
    struct lex_process *process =
        static_cast<struct lex_process *>(calloc(1, sizeof(struct lex_process)));
    process->function     = functions;
    process->token_vec    = vector_create(sizeof(struct token));
    process->compiler     = compiler;
    process->private_data = private_data;
    process->pos.line     = 1;
    process->pos.col      = 1;
    return process;
}

// Releases all memory owned by `process`: frees the token vector and then
// frees the lex_process struct itself.  Safe to call with a null pointer —
// the function returns immediately without doing anything in that case.
//
// process — the lex_process to destroy; may be nullptr.
void lex_process_free(struct lex_process *process)
{
    if (!process) return;
    vector_free(process->token_vec);
    free(process);
}

// Returns the private data pointer that was passed to lex_process_create.
// The lexer and its callers can use this to retrieve any extra state that was
// attached at creation time without casting through the lex_process struct
// directly.
//
// process — the lex_process to query.
//
// Returns the stored private_data pointer, which may be nullptr if none was
// provided at creation.
void *lex_process_private(struct lex_process *process)
{
    return process->private_data;
}

// Returns the token vector that the lexer writes its output tokens into.
// Callers use this to inspect or transfer the token stream after lexing is
// complete (e.g. compile_file copies the pointer to compile_process->token_vec).
//
// process — the lex_process to query.
//
// Returns a pointer to the internal token vector; the caller must not free it
// directly — use lex_process_free to tear down the whole lex_process.
struct vector *lex_process_tokens(struct lex_process *process)
{
    return process->token_vec;
}
