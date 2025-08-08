#include "compiler.h"
#include "helpers/vector.h"
#include <stdarg.h>
static struct lex_process* lex_process;
static char peekc()
{
    return lex_process->function->peek_char(lex_process );
}
static char pushc()
{
    return lex_process->function->push_char(lex_process);
}
void compiler_node_error(struct node* node, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    fprintf(stderr, " on line %i, col %i in file %s\n", node->pos.line, node->pos.col, node->pos.filename);
    exit(-1);
}

void compiler_error(struct compile_process* compiler, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fprintf(stderr, " on line %i, col %i in file %s\n", compiler->pos.line, compiler->pos.col, compiler->pos.filename);
    exit(-1);
}

void compiler_warning(struct compile_process* compiler, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fprintf(stderr, " on line %i, col %i in file %s\n", compiler->pos.line, compiler->pos.col, compiler->pos.filename);
}
struct token* read_next_token()
{
    struct token*  token = NULL;
    char c = peekc();
    switch (c)
    {
        case EOF:
            break;
        default:
            compiler_error(lex_process->compiler, "Unexpected token");
    }
    return token;

}
int lex(struct lex_process* process)
{
    process->current_expression_count = 0;
    process->parentheses_buffer = NULL;
    lex_process = process; 
    process->pos.filename = process->compiler->cfile.abs_path;
    

    struct token* token = read_next_token();
    while (token)
    {
        vector_push(process->token_vec, token );
        token = read_next_token();
    }
    return LEXICAL_ANALYSIS_ALL_OK;

}