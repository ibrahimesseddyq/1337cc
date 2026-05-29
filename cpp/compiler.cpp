#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdarg>
#include <cstdlib>

static struct lex_process_functions compiler_lex_functions = {
    compile_process_next_char,
    compile_process_peek_char,
    compile_process_push_char
};

// Prints a formatted error message to stderr that includes the current source
// position (line, column, filename) from `compiler->pos`, then terminates the
// process with exit code -1.  Use this for unrecoverable compilation errors.
//
// compiler — the active compile_process whose position info is appended to the
//            message.
// msg      — a printf-style format string describing the error.
// ...      — variadic arguments consumed by the format string.
void compiler_error(struct compile_process *compiler, const char *msg, ...)
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

// Prints a formatted warning message to stderr that includes the current source
// position (line, column, filename) from `compiler->pos`.  Unlike
// compiler_error, this does not abort — execution continues after the call.
//
// compiler — the active compile_process whose position info is appended to the
//            message.
// msg      — a printf-style format string describing the warning.
// ...      — variadic arguments consumed by the format string.
void compiler_warning(struct compile_process *compiler, const char *msg, ...)
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

// Top-level driver that runs the full compilation pipeline for a single source
// file.  The pipeline is:
//   1. compile_process_create  — open the source/output files and allocate
//                                the central compiler state.
//   2. lex_process_create / lex — tokenise the source file; the resulting
//                                 token vector is transferred to the
//                                 compile_process.
//   3. parse                   — build an AST from the token stream.
//   4. codegen                 — emit output from the AST.
//
// filename     — path to the C source file to compile.
// out_filename — path where the compiler should write its output; may be
//                nullptr if no output file is required.
// flags        — bitmask of compiler flags forwarded to compile_process_create.
//
// Returns COMPILER_FILE_COMPILED_OK on success, or
// COMPILER_FAILED_WITH_ERRORS if any stage fails.
int compile_file(const char *filename, const char *out_filename, int flags)
{
    struct compile_process *process = compile_process_create(filename, out_filename, flags);
    if (!process)
        return COMPILER_FAILED_WITH_ERRORS;

    struct lex_process *lex_proc = lex_process_create(process, &compiler_lex_functions, nullptr);
    if (!lex_proc)
        return COMPILER_FAILED_WITH_ERRORS;

    if (lex(lex_proc) != LEXICAL_ANALYSIS_ALL_OK)
        return COMPILER_FAILED_WITH_ERRORS;

    process->token_vec = lex_proc->token_vec;

    if (parse(process) != PARSE_ALL_OK)
        return COMPILER_FAILED_WITH_ERRORS;

    if (codegen(process) != CODEGEN_ALL_OK)
        return COMPILER_FAILED_WITH_ERRORS;

    return COMPILER_FILE_COMPILED_OK;
}
