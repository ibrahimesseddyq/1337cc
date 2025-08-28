 #ifndef COMPILER_H
 #define COMPILER_H 

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define S_EQ(str, str2)\
    ( str && str2 && (strcmp(str, str2) == 0))

struct pos
{
    int         line;
    int         col;
    const char  *filename;
};

#define NUMERIC_CASE                        \
    case '0':                               \
    case '1':                               \
    case '2':                               \
    case '3':                               \
    case '4':                               \
    case '5':                               \
    case '6':                               \
    case '7':                               \
    case '8':                               \
    case '9'

#define OPERATOR_CASE_EXCLUDING_DIVISION    \
    case '+':                               \
    case '-':                               \
    case '*':                               \
    case '>':                               \
    case '<':                               \
    case '%':                               \
    case '!':                               \
    case '=':                               \
    case '~':                               \
    case '|':                               \
    case '&':                               \
    case '(':                               \
    case '[':                               \
    case ',':                               \
    case '.':                               \
    case '?'

#define SYMBOL_CASE                         \
    case '{':                               \
    case '}':                               \
    case ':':                               \
    case ';':                               \
    case '#':                               \
    case '\\':                              \
    case ')':                               \
    case ']'

enum 
{
    LEXICAL_ANALYSIS_ALL_OK,
    LEXICAL_ANALYSIS_INPUT_ERROR
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

struct       lex_process;
typedef char (*LEX_PROCESS_NEXT_CHAR)(struct lex_process* process);
typedef char (*LEX_PROCESS_PEEK_CHAR)(struct lex_process* process);
typedef char (*LEX_PROCESS_PUSH_CHAR)(struct lex_process* process, char c);


struct lex_process_function
{
    LEX_PROCESS_NEXT_CHAR           next_char;
    LEX_PROCESS_PEEK_CHAR           peek_char;
    LEX_PROCESS_PUSH_CHAR           push_char;
};
struct lex_process
{
    struct pos                      pos;
    struct vector*                  token_vec;
    struct compile_process*         compiler;

    int                             current_expression_count;// how many bracket 
    struct buffer*                  parentheses_buffer;
    struct lex_process_function*    function;

    void*                           private;
};

struct token 
{
    int                             type;
    int                             flags; 
    struct pos                      pos;
    union 
    {
        char                cval;
        const char          *sval;
        unsigned int        inum;
        unsigned long       lnum;
        unsigned long long  llnum;
        void*               any;
    };

    struct token_number 
    {
        int                         type;
    } num;
    // True if their whitespace is between the token and the next one
    bool whitespace;

    // for debugging issue
    const char *between_brackets;
};

enum
{
    NUMBER_TYPE_NORMAL,
    NUMBER_TYPE_LONG,
    NUMBER_TYPE_FLOAT,
    NUMBER_TYPE_DOUBLE
};

enum 
{
    COMPILER_FILE_COMPLETED_OK,
    COMPILER_FAILED_WITH_ERRORS
};
enum 
{
    NODE_TYPE_EXPRESSION,
    NODE_TYPE_EXPRESSION_PARENTHESIS,
    NODE_TYPE_NUMBER,
    NODE_TYPE_IDENTIFIER,
    NODE_TYPE_STRING,
    NODE_TYPE_VARIABLE,
    NODE_TYPE_VARIABLE_LIST,
    NODE_TYPE_FUNCTION,
    NODE_TYPE_BODY,
    NODE_TYPE_STATEMENT_RETURN,
    NODE_TYPE_STATEMENT_IF,
    NODE_TYPE_STATEMENT_ELSE,
    NODE_TYPE_STATEMENT_WHILE,
    NODE_TYPE_STATEMENT_DO_WHILE,
    NODE_TYPE_STATEMENT_FOR,
    NODE_TYPE_STATEMENT_BREAK,
    NODE_TYPE_STATEMENT_CONTINUE,
    NODE_TYPE_STATEMENT_SWITCH,
    NODE_TYPE_STATEMENT_CASE,
    NODE_TYPE_STATEMENT_DEFAULT,
    NODE_TYPE_STATEMENT_GOTO,
    NODE_TYPE_UNARY,
    NODE_TYPE_TERNARY,
    NODE_TYPE_STRUCT,
    NODE_TYPE_UNION,
    NODE_TYPE_BRACKET,
    NODE_TYPE_CAST,
    NODE_TYPE_BLANK,
};
enum 
{
    PARSE_ALL_OK,
    PARSE_GENERAL_ERROR
};
struct node 
{
    int type;
    int flags;

    struct pos;
    struct node_binded
    {
        struct node* owner;
        struct node* function;

    } binded;


    union 
    {
        char cval;
        const char *sval;
        unsigned int inum;
        unsigned long lnum;
        unsigned long long llnum;
    };
};
struct compile_process
{
    int                             flags;
    struct pos                      pos;
    FILE*                           ofile;

    struct vector*   token_vec;
    struct vector*   node_vec;
    struct vector*  node_tree_vec;
    struct compile_process_input_file
    {
        FILE*       fp;
        const char  *abs_path;
    } cfile;

};
int parse(struct compile_process* process);
char                compile_process_next_char(struct  lex_process* lex_process);
char                compile_process_peek_char(struct  lex_process* lex_process);
char                compile_process_push_char(struct  lex_process* lex_process, char c);
struct lex_process* lex_process_create(struct compile_process* compiler, struct lex_process_function functions, void *private);
void                lex_process_free(struct lex_process* process);
void                *lex_process_private(struct lex_process* process);
void                *lex_process_tokens(struct lex_process* process);
int                 lex(struct lex_process* process);
void                compiler_node_error(struct node* node, const char* msg, ...);
void                compiler_error(struct compile_process* compiler, const char* msg, ...);
struct token*       read_next_token();
void                compiler_warning(struct compile_process* compiler, const char* msg, ...);
bool                token_is_keyword(struct token* token, const char* value);
struct lex_process* tokens_build_for_string(struct compile_process* compiler, const char* str);

#endif