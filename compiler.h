 #ifndef COMPILER_H
 #define COMPILER_H 

#include <stdio.h>
#include <stdbool.h>

struct pos
{
    int line;
    int col;
    const char *filename;
};

enum 
{
    TOKEN_TYPE_IDENTIFIER,
    TOKEN_TYPE_KEYWORD,
    TOKEN_TYPE_OPERATOR,
    TOKEN_TYPE_SYMBOL,
    TOKEN_TYPE_NUMBER,
    TOKEN_TYPE_STRING,
    TOKEN_TYPE_COMMENT,
    TOKEN_TYPE_NEWLINE
     
};
struct lex_process;
typedef char (*LEX_PROCESS_NEXT_CHAR)(struct lex_process* process);
struct lex_process_function
{

};
struct lex_process
{
    struct pos pos;
    struct vector* token_vec;
    struct compile_process* compiler;

    int current_expression_count;// how many bracket 
    struct buffer* parentheses_buffer;
    struct lex_process_function* function;

    void* private;
};
struct token 
{
    int type;
    int flags; 

    union 
    {
        char cval;
        const char *sval;
        unsigned int inum;
        unsigned long lnum;
        unsigned long long llnum;
        void* any;
    };
    // True if their whitespace is between the token and the next one
    bool whitespace;

    // for debugging issue
    const char *between_brackets;
};


enum 
{
    COMPILER_FILE_COMPLETED_OK,
    COMPILER
}
 #endif