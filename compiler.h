 #ifndef COMPILER_H
 #define COMPILER_H 

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define S_EQ(str, str2)\
    ( str && str2 && (strcmp(str, str2) == 0))

#define TOTAL_OPERATOR_GROUPS 14
#define MAX_OPERATOR_IN_GROUP 12

enum
{
    ASSOCIATIVITY_LEFT_TO_RIGHT,
    ASSOCIATIVITY_RIGHT_TO_LEFT
};

struct expressionable_op_precedence_group
{
    char* operators[MAX_OPERATOR_IN_GROUP];
    int associativity;
};
struct scope 
{
    int flags;
    struct vector* entities;
    
    //  the total number of bytes this scope uses, Aligned to 16 bytes.
    size_t size;
    struct scope* parent;
};

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

struct history_cases
{
    struct vector* cases;
    bool has_default_case; 
};
struct history
{
    int flags;
    struct parser_history_switch
    {
        struct history_cases case_data;
    } _switch;
};
struct lex_process_functions
{
    LEX_PROCESS_NEXT_CHAR next_char;
    LEX_PROCESS_PEEK_CHAR peek_char;
    LEX_PROCESS_PUSH_CHAR push_char;
};

struct lex_process
{
    struct pos                      pos;
    struct vector*                  token_vec;
    struct compile_process*         compiler;

    int                             current_expression_count;// how many bracket 
    struct buffer*                  parentheses_buffer;
    struct lex_process_functions*    function;

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
    NODE_TYPE_LABEL,

    NODE_TYPE_BLANK,
};
enum 
{
    PARSE_ALL_OK,
    PARSE_GENERAL_ERROR
};
enum 
{
    NODE_FLAG_INSIDE_EXPRESSION =       0b00000001,
    NODE_FLAG_IS_FORWARD_DECLARATION=   0b00000010,
    NODE_FLAG_HAS_VARIABLE_COMBINED=    0b00000100

};
struct node ;
struct array_brackets
{
    struct vector* n_brackets;

};

struct datatype 
{
    int                 flags;
    int                 type;

    struct datatype*    secondary;

    const char*         type_str;
    size_t              size;  
    int                 pointer_depth;

    union 
    {
        struct node* struct_node;
        struct node* union_node;
    };
    struct array 
    {
        struct array_brackets* brackets;
        size_t size;
    } array;
};
struct parsed_switch_case
{
    int index;

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
        struct exp 
        {
            struct node* left;
            struct node* right;
            const char * op;
        } exp;

        struct parenthesis 
        {
            struct node* exp;

        } parenthesis;
        struct var
        {
            struct datatype type;
            const char      *name;
            // aligned offset 
            int             aoffset;
            struct node*    val;
            int padding;
        } var;
        struct varlist 
        {
            struct vector* list;
        } var_list;

        struct bracket 
        {
            struct node* inner;
        } bracket; 

        struct _struct 
        {
            const char* name;
            struct node* body_n;
            struct node* var;
        } _struct;
        struct body 
        {
            struct vector* statements;
            size_t size;
            bool padded;

            struct node* largest_var_node;
        } body;
        struct function 
        {
            int flags;
            struct datatype* rtype;
            const char* name;
            struct function_arguments
            {
                struct vector* vector;
                size_t stack_addition;
            } args;

            struct node* body_n;

            size_t stack_size; 
        } func;

        union statement 
        {
            struct return_stmt
            {
                struct node* exp;  
            }return_stmt;
            struct if_stmt
            {
                struct node *cond_node;
                struct node* body_node;

                struct node* next;
            } if_stmt;

            struct else_stmt
            {
                struct node* body_node;
            } else_stmt;

            struct for_stmt
            {
                struct node* init_node;
                struct node* cond_node;
                struct node* loop_node;
                struct node* body_node;

            } for_stmt;
            struct while_stmt
            {
                struct node* exp_node;
                struct node* body_node;
            } while_stmt;
            struct do_while_stmt
            {
                struct node* exp_node;
                struct node* body_node;
            } do_while_stmt;

            struct switch_stmt
            {
                struct node* exp;
                struct node* body;
                struct vector* cases;
                bool has_default_case;
            } switch_stmt;

            struct _goto_stmt
            {
                struct node* label;
            } _goto;
        } stmt;
        struct node_label
        {
            struct node* name;
        } label;
    };

    union 
    {
        char cval;
        const char *sval;
        unsigned int inum;
        unsigned long lnum;
        unsigned long long llnum;
    };
};
enum
{
    SYMBOL_TYPE_NODE,
    SYMBOL_TYPE_NATIVE_FUNCTION,
    SYMBOL_TYPE_UNKOWN,
};

struct symbol 
{
    const char *name;
    int type;
    void *data;
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

    struct 
    {
        struct scope* root;
        struct scope* current;
    } scope;
    struct
    {
        struct vector* table;
        struct vector* tables;
    } symbols;
};

enum 
{
    DATATYPE_FLAG_IS_SIGNED =                        0b00000001,
    DATATYPE_FLAG_IS_STATIC =                        0b00000010,
    DATATYPE_FLAG_IS_CONST =                         0b00000100,
    DATATYPE_FLAG_IS_POINTER =                       0b00001000,
    DATATYPE_FLAG_IS_ARRAY =                         0b00010000,
    DATATYPE_FLAG_IS_EXTERN =                        0b00100000,
    DATATYPE_FLAG_IS_RESTRICT=                       0b01000000,
    DATATYPE_FLAG_IS_IGNORE_TYPE_CHECKING=           0b10000000,
    DATATYPE_FLAG_IS_SECONDARY=                      0b100000000,
    DATATYPE_FLAG_IS_STRUCT_UNION_NO_NAME=           0b1000000000,
    DATATYPE_FLAG_IS_LITERAL=                        0b10000000000,




};
enum 
{
    DATA_TYPE_VOID,
    DATA_TYPE_CHAR,
    DATA_TYPE_SHORT,
    DATA_TYPE_INTEGER,
    DATA_TYPE_LONG,
    DATA_TYPE_FLOAT,
    DATA_TYPE_DOUBLE,
    DATA_TYPE_STRUCT,
    DATA_TYPE_UNION,
    DATA_TYPE_UNKOWN


};

enum 
{
    DATA_SIZE_ZERO,
    DATA_SIZE_BYTE,
    DATA_SIZE_WORD,
    DATA_SIZE_DWORD,
    DATA_SIZE_DDWORD
};

enum 
{
    FUNCTION_NODE_FLAG_IS_NATIVE = 0b00000001,

};
enum 
{
    DATA_TYPE_EXPECT_PRIMITIVE, 
    DATA_TYPE_EXPECT_UNION, 
    DATA_TYPE_EXPECT_STRUCT 

};
int                 parse(struct compile_process* process);
char                compile_process_next_char(struct  lex_process* lex_process);
char                compile_process_peek_char(struct  lex_process* lex_process);
char                compile_process_push_char(struct  lex_process* lex_process, char c);
struct lex_process* lex_process_create(struct compile_process* compiler, struct lex_process_functions *functions, void *private);
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
bool                token_is_symbol(struct token* token, char c);
bool                token_is_nl_or_comment_or_newline_separator(struct token* token);
struct node*        node_create(struct node* _node);
struct node*        node_pop();
struct node*        node_peek();
struct node*        node_peek_or_null();
void                node_push(struct node* node);
void                node_set_vector(struct vector* vec, struct vector* root_vec);
void make_return_node(struct node* exp_node);
struct compile_process* compile_process_create(const char* filename, const char* filename_out, int flags);
bool                    node_is_expressionable(struct node* node);
struct node*            node_peek_expressionable_or_null();
int                     parse_expressionable_single(struct history* history);
void                    make_exp_node(struct node* left_node, struct node* right_node, const char *op);
void                    parse_expressionable(struct history* history);
bool                    keyword_is_datatype(const char* str);
bool                    token_is_primitive_keyword(struct token* token);
void make_label_node(struct node* name_node);
bool                    datatype_is_struct_or_union_for_name(const char* name);
bool token_is_operator(struct token* token, const char* val);
void parser_datatype_init_type_and_size_for_primitive(struct token* datatype_token, struct token* datatype_secondary_token, struct datatype* datatype_out);
void parser_ignore_int(struct datatype* dtype);
void make_for_node(struct node *init_node, struct node *cond_node, struct node *loop_node, struct node *body_node);
void make_exp_parenthesis_node(struct node* exp_node);
struct array_brackets* array_brackets_new();
void make_do_while_node(struct node *body_node, struct node *exp_node);
struct node* variable_node_or_list(struct node* node);
void array_brackets_free(struct array_brackets* brackets);
void make_switch_node(struct node* exp_node, struct node* body_node, struct vector* cases, bool has_default_case);
void array_brackets_add(struct array_brackets* brackets, struct node* bracket_node);
struct vector* array_brackets_node_vector(struct array_brackets* brackets);
void make_else_node(struct node* body_node);
size_t array_brackets_calculate_size_from_index(struct datatype* dtype, struct array_brackets* brackets, int index);
size_t array_brackets_calculate_size(struct datatype* dtype, struct array_brackets* brackets);
void make_if_node(struct node* cond_node, struct node* body_node, struct node* next_node);
int array_total_indexes(struct datatype* dtype);
void make_bracket_node(struct node* node);
bool datatype_is_struct_or_union(struct datatype* dtype);
struct scope* scope_new(struct compile_process* process, int flags);
struct scope* scope_alloc();
void scope_dealloc(struct scope* scope);
struct scope* scope_create_root(struct compile_process* process);
void scope_free_root(struct compile_process* process);
void scope_iteration_start(struct scope* scope);
void scope_iteration_end(struct scope* scope);
void* scope_iteration_back(struct scope* scope);
void* scope_last_entity_at_scope(struct scope* scope);
void *scope_last_entity_from_scope_stop_at(struct scope* scope, struct scope* stop_scope);
void * scope_last_entity_stop_at(struct compile_process* process, struct scope* stop_scope);
void* scope_last_entity(struct compile_process* process);
void scope_push(struct compile_process* process, void *ptr, size_t elem_size);
void scope_finish(struct compile_process* process);
struct scope* scope_current(struct compile_process* process);
void make_body_node(struct vector* body_vec, size_t size, bool padded, struct node* largest_var_node);
bool variable_node_is_primitive(struct node* node);
bool datatype_is_primitive(struct datatype* dtype);
struct node* variable_node(struct node* node);
void make_while_node(struct node *exp_node, struct node *body_node);
size_t datatype_size_for_array_access(struct datatype* dtype);
size_t datatype_element_size(struct datatype* dtype);
size_t datatype_size_no_ptr(struct datatype* dtype);
size_t datatype_size(struct datatype* dtype);
size_t variable_size(struct node* var_node);
void make_struct_node(const char* name, struct node* body_node);
size_t variable_size_for_list(struct node* var_list_node);
void make_continue_node();
void make_break_node();
bool node_is_struct_or_union_variable(struct node* node);
int padding(int val, int to);

int align_value(int val, int to);

int align_value_treat_positive(int val, int to);
int compute_sum_padding(struct vector* vec);
void symbolresolver_build_for_node(struct compile_process* process, struct node* node);
struct symbol* symbolresolver_get_symbol(struct compile_process* process, const char *name);
struct node* node_from_symbol(struct symbol* sym);
struct node* node_from_symbol(struct compile_process* current_process, const char* name);
struct node* struct_node_for_name(struct compile_process* current_process, const char* name);
void parse_function(struct datatype* ret_type, struct token* name_token, struct history* history);
void symbolresolver_new_table(struct compile_process* process);
bool token_is_identifier(struct token* token);
void symbolresolver_end_table(struct compile_process* process);
void make_function_node(struct datatype* ret_type, const char* name, struct vector* arguments, struct node* body_node);
struct symbol* symbolresolver_get_symbol_for_native_function(struct compile_process* process, const char * name);
size_t function_node_argument_stack_addition(struct node* node);
bool node_is_expression_or_parentheses(struct node* node);
bool node_is_value_type(struct node* node);
void make_goto_node(struct node* label_node);
void parse_expressionable_root(struct history* history);
#endif