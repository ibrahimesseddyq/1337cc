#include "compiler.h"
#include <stdarg.h>
#include <stdlib.h>

/**
 * @brief Lexical process function bindings for the compiler.
 *
 * These function pointers are provided to the lexer so it can
 * read input characters, peek ahead, and push characters back.
 */
struct lex_process_functions compiler_lex_functions = {
    .next_char = compile_process_next_char,
    .peek_char = compile_process_peek_char,
    .push_char = compile_process_push_char
};

/**
 * @brief Print a fatal compiler error message and exit.
 *
 * This function prints a formatted error message along with the
 * current source code position (line, column, file), then terminates
 * the program with exit code -1.
 *
 * @param compiler The current compilation process.
 * @param msg      The error message format string.
 * @param ...      Arguments for the format string.
 */
void compiler_error(struct compile_process* compiler, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    fprintf(stderr, " on line %i, col %i in file %s\n",
            compiler->pos.line,
            compiler->pos.col,
            compiler->pos.filename);

    exit(-1);
}

/**
 * @brief Print a compiler warning message (non-fatal).
 *
 * Similar to compiler_error(), but does not exit the program.
 * Used for non-critical issues where compilation can continue.
 *
 * @param compiler The current compilation process.
 * @param msg      The warning message format string.
 * @param ...      Arguments for the format string.
 */
void compiler_warning(struct compile_process* compiler, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    fprintf(stderr, " on line %i, col %i in file %s\n",
            compiler->pos.line,
            compiler->pos.col,
            compiler->pos.filename);
}

/**
 * @brief Compile a source file into an output file.
 *
 * This is the main entrypoint for the compiler. It performs the following steps:
 *  1. Create a compilation process with input and output files.
 *  2. Run lexical analysis on the source file to generate tokens.
 *  3. Parse the token stream into an AST (Abstract Syntax Tree).
 *  4. (Placeholder) Run code generation.
 *
 * @param filename     Path to the input source file.
 * @param out_filename Path to the output file (or NULL if unused).
 * @param flags        Compilation flags (implementation-defined).
 *
 * @return
 *  - COMPILER_FILE_COMPILED_OK on success
 *  - COMPILER_FAILED_WITH_ERRORS on any failure
 */
int compile_file(const char* filename, const char* out_filename, int flags)
{
    struct compile_process* process = compile_process_create(filename, out_filename, flags);
    if (!process)
        return COMPILER_FAILED_WITH_ERRORS;

    // Perform lexical analysis
    struct lex_process* lex_process = lex_process_create(process, &compiler_lex_functions, NULL);
    if (!lex_process)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }

    if (lex(lex_process) != LEXICAL_ANALYSIS_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }

    process->token_vec = lex_process->token_vec;

    // Perform parsing
    if (parse(process) != PARSE_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }
    
    if (codegen(process) != CODEGEN_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }
    // Perform code generation (not yet implemented)

    return COMPILER_FILE_COMPILED_OK;
}
