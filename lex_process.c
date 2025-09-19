/**
 * @file lex_process.c
 * @brief Creation and management utilities for lexer processes.
 *
 * This module provides a thin wrapper around the `struct lex_process` lifecycle:
 *  - creating a lex_process instance bound to a character source function table,
 *  - freeing the lex_process (and its token vector),
 *  - accessing the lex_process private pointer and token vector.
 *
 * The lex process owns a token vector (vector of struct token) which the lexer
 * (lexer.c) populates during lexical analysis. The caller remains responsible
 * for freeing any heap data referenced by tokens (strings, buffers) if required.
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include <stdlib.h>
 
 /**
  * @brief Allocate and initialize a new lex_process.
  *
  * Initializes the function table pointer, token vector, private backend pointer,
  * and sets the initial file position (line/col starts at 1).
  *
  * The created lex_process should be passed to `lex()` to perform tokenization.
  * After use, call `lex_process_free()` to release the token vector and the
  * lex_process object itself.
  *
  * @param compiler  Compile process pointer used for error reporting and context.
  * @param functions Pointer to a struct providing peek/next/push character callbacks.
  * @param private   Backend-specific private pointer (e.g., a buffer) accessible via lex_process_private().
  * @return Pointer to a newly allocated lex_process, or NULL on allocation failure.
  */
 struct lex_process* lex_process_create(struct compile_process* compiler, struct lex_process_functions* functions, void* private)
 {
     struct lex_process* process = calloc(1, sizeof(struct lex_process));
     process->function = functions;
     process->token_vec = vector_create(sizeof(struct token));
     process->compiler = compiler;
     process->private = private;
     process->pos.line = 1;
     process->pos.col = 1;
     return process;
 }
 
 /**
  * @brief Free a lex_process and its owned token vector.
  *
  * This frees the `token_vec` (the vector container) and the lex_process struct.
  * It does not free individual token payloads (e.g., strings pointed to by token->sval)
  * — the ownership policy for those buffers depends on the rest of the compiler.
  *
  * @param process Lex process to free (may be NULL).
  */
 void lex_process_free(struct lex_process* process)
 {
     if (!process) return;
     vector_free(process->token_vec);
     free(process);
 }
 
 /**
  * @brief Retrieve the backend-private pointer associated with the lex process.
  *
  * This pointer was supplied when creating the lex_process and is typically used
  * by the lexing backend (e.g., a buffer-based implementation) to store state.
  *
  * @param process Lex process.
  * @return The private pointer supplied at creation (may be NULL).
  */
 void* lex_process_private(struct lex_process* process)
 {
     return process->private;
 }
 
 /**
  * @brief Return the vector that will contain tokens produced by the lexer.
  *
  * The vector holds elements of type `struct token` (as configured in lex_process_create).
  * Callers can inspect this vector after calling `lex()` to examine produced tokens.
  *
  * @param process Lex process.
  * @return Pointer to the token vector owned by the lex process.
  */
 struct vector* lex_process_tokens(struct lex_process* process)
 {
     return process->token_vec;
 }
 