/**
 * @file c1337.h
 * @brief Main header file for the C1337 C compiler implementation
 * 
 * This header defines the core data structures, enumerations, and function 
 * declarations for a complete C compiler system including lexical analysis,
 * parsing, symbol resolution, scope management, and code generation.
 */

 #ifndef C1337_H
 #define C1337_H
 
 #include <stdio.h>
 #include <stdbool.h>
 #include <string.h>
 
 /**
  * @brief Safe string comparison macro that handles null pointers
  * @param str First string to compare
  * @param str2 Second string to compare
  * @return true if both strings are non-null and equal, false otherwise
  */
 #define S_EQ(str, str2) \
     (str && str2 && (strcmp(str, str2) == 0))
 
 /**
  * @brief Source code position tracking structure
  * Used for error reporting and debugging information
  */
 struct pos
 {
     int line;               // Line number in source file
     int col;                // Column number in source file
     const char *filename;   // Name of the source file
 };
 
 /**
  * @brief Macro defining cases for numeric characters (0-9)
  * Used in switch statements for character classification
  */
 #define NUMERIC_CASE \
     case '0':        \
     case '1':        \
     case '2':        \
     case '3':        \
     case '4':        \
     case '5':        \
     case '6':        \
     case '7':        \
     case '8':        \
     case '9'
 
 /**
  * @brief Macro defining cases for operators (excluding division)
  * Used in lexical analysis for operator recognition
  */
 #define OPERATOR_CASE_EXCLUDING_DIVISION \
     case '+':                            \
     case '-':                            \
     case '*':                            \
     case '>':                            \
     case '<':                            \
     case '^':                            \
     case '%':                            \
     case '!':                            \
     case '=':                            \
     case '~':                            \
     case '|':                            \
     case '&':                            \
     case '(':                            \
     case '[':                            \
     case ',':                            \
     case '.':                            \
     case '?'
 
 /**
  * @brief Macro defining cases for language symbols
  * Used in lexical analysis for symbol recognition
  */
 #define SYMBOL_CASE \
     case '{':       \
     case '}':       \
     case ':':       \
     case ';':       \
     case '#':       \
     case '\\':      \
     case ')':       \
     case ']'
 
 /**
  * @brief Lexical analysis result codes
  */
 enum
 {
     LEXICAL_ANALYSIS_ALL_OK,     // Lexical analysis completed successfully
     LEXICAL_ANALYSIS_INPUT_ERROR // Error occurred during lexical analysis
 };
 
 /**
  * @brief Token type enumeration
  * Defines the different types of tokens recognized by the lexer
  */
 enum
 {
     TOKEN_TYPE_IDENTIFIER,  // Variable names, function names, etc.
     TOKEN_TYPE_KEYWORD,     // Language keywords (if, while, int, etc.)
     TOKEN_TYPE_OPERATOR,    // Mathematical and logical operators
     TOKEN_TYPE_SYMBOL,      // Punctuation and delimiters
     TOKEN_TYPE_NUMBER,      // Numeric literals
     TOKEN_TYPE_STRING,      // String literals
     TOKEN_TYPE_COMMENT,     // Code comments
     TOKEN_TYPE_NEWLINE      // Line breaks and newlines
 };
 
 /**
  * @brief Number type classification
  * Used to distinguish between different numeric literal types
  */
 enum
 {
     NUMBER_TYPE_NORMAL, // Regular integer (int)
     NUMBER_TYPE_LONG,   // Long integer (long)
     NUMBER_TYPE_FLOAT,  // Floating point (float)
     NUMBER_TYPE_DOUBLE  // Double precision (double)
 };
 
 /**
  * @brief Token structure
  * Represents a single lexical token with its type, value, and metadata
  */
 struct token
 {
     int type;           // Token type from TOKEN_TYPE enum
     int flags;          // Additional token flags
     struct pos pos;     // Source code position where token was found
     
     // Union for storing different token value types
     union
     {
         char cval;               // Character value for single characters
         const char *sval;        // String value for identifiers, keywords, strings
         unsigned int inum;       // Integer value for numeric tokens
         unsigned long lnum;      // Long integer value
         unsigned long long llnum;// Long long integer value
         void *any;               // Generic pointer for special cases
     };
 
     // Additional information for numeric tokens
     struct token_number
     {
         int type;   // Number type from NUMBER_TYPE enum
     } num;
 
     // True if whitespace follows this token
     // Used for preserving formatting and parsing context
     bool whitespace;
 
     // Content between brackets for expressions like (5+10+20)
     // Stores the bracket content as a string
     const char *between_brackets;
 };
 
 // Forward declaration for lexical process
 struct lex_process;
 
 // Function pointer types for lexical process operations
 typedef char (*LEX_PROCESS_NEXT_CHAR)(struct lex_process *process);
 typedef char (*LEX_PROCESS_PEEK_CHAR)(struct lex_process *process);
 typedef void (*LEX_PROCESS_PUSH_CHAR)(struct lex_process *process, char c);
 
 /**
  * @brief Function pointers for lexical process operations
  * Allows flexible input handling (files, strings, etc.)
  */
 struct lex_process_functions
 {
     LEX_PROCESS_NEXT_CHAR next_char;  // Get next character from input
     LEX_PROCESS_PEEK_CHAR peek_char;  // Look at next character without consuming
     LEX_PROCESS_PUSH_CHAR push_char;  // Push character back to input stream
 };
 
 /**
  * @brief Lexical analysis process state
  * Contains all state needed during lexical analysis
  */
 struct lex_process
 {
     struct pos pos;                    // Current position in source
     struct vector *token_vec;          // Vector of generated tokens
     struct compile_process *compiler;  // Reference to parent compiler process
 
     // Expression nesting depth tracking
     // Used for handling nested expressions like ((50))
     int current_expression_count;
     
     struct buffer *parentheses_buffer; // Buffer for matching parentheses
     struct lex_process_functions *function; // Input function pointers
 
     // Private data that the lexer doesn't understand
     // but the caller does - allows for custom extensions
     void *private;
 };
 
 /**
  * @brief Compilation result codes
  */
 enum
 {
     COMPILER_FILE_COMPILED_OK,  // File compiled successfully
     COMPILER_FAILED_WITH_ERRORS // Compilation failed with errors
 };
 
 /**
  * @brief Scope structure for variable and function scoping
  * Represents a lexical scope (global, function, block, etc.)
  */
 struct scope
 {
     int flags;                  // Scope-specific flags
     struct vector *entities;    // Vector of entities (variables, functions) in scope
     size_t size;               // Total memory size used by scope (aligned to 16 bytes)
     struct scope *parent;      // Parent scope (NULL for global scope)
 };
 
 /**
  * @brief Symbol type enumeration
  * Classifies different kinds of symbols in the symbol table
  */
 enum
 {
     SYMBOL_TYPE_NODE,            // Regular language constructs (variables, functions)
     SYMBOL_TYPE_NATIVE_FUNCTION, // Built-in or library functions
     SYMBOL_TYPE_UNKNOWN          // Unresolved or unknown symbols
 };
 
 /**
  * @brief Symbol table entry
  * Represents a symbol in the compiler's symbol table
  */
 struct symbol
 {
     const char *name;   // Symbol name (identifier)
     int type;          // Symbol type from SYMBOL_TYPE enum
     void *data;        // Pointer to associated data (AST node, etc.)
 };
 
 /**
  * @brief Main compilation process structure
  * Contains all state and data structures needed for compilation
  */
 struct compile_process
 {
     int flags;      // Compilation flags and options
     struct pos pos; // Current position in source being compiled
 
     // Input file information
     struct compile_process_input_file
     {
         FILE *fp;               // File pointer to source file
         const char *abs_path;   // Absolute path to source file
     } cfile;
 
     // Compilation data structures
     struct vector *token_vec;      // Vector of tokens from lexical analysis
     struct vector *node_vec;       // Vector of AST nodes
     struct vector *node_tree_vec;  // Vector of complete AST trees
     FILE *ofile;                   // Output file for generated code
 
     // Scope management
     struct
     {
         struct scope *root;     // Global/root scope
         struct scope *current;  // Currently active scope
     } scope;
 
     // Symbol table management
     struct
     {
         struct vector *table;   // Current active symbol table
         struct vector *tables;  // Stack of symbol tables for nested scopes
     } symbols;
 };
 
 /**
  * @brief Parsing result codes
  */
 enum
 {
     PARSE_ALL_OK,       // Parsing completed successfully
     PARSE_GENERAL_ERROR // General parsing error occurred
 };
 
 /**
  * @brief AST node type enumeration
  * Defines all possible types of nodes in the Abstract Syntax Tree
  */
 enum
 {
     // Expression nodes
     NODE_TYPE_EXPRESSION,           // Binary/unary expressions
     NODE_TYPE_EXPRESSION_PARENTHESES, // Parenthesized expressions
     NODE_TYPE_NUMBER,              // Numeric literals
     NODE_TYPE_IDENTIFIER,          // Variable/function identifiers
     NODE_TYPE_STRING,              // String literals
     
     // Variable and function declarations
     NODE_TYPE_VARIABLE,            // Variable declaration
     NODE_TYPE_VARIABLE_LIST,       // List of variable declarations
     NODE_TYPE_FUNCTION,            // Function definition
     NODE_TYPE_BODY,                // Code block body
     
     // Control flow statements
     NODE_TYPE_STATEMENT_RETURN,    // return statement
     NODE_TYPE_STATEMENT_IF,        // if statement
     NODE_TYPE_STATEMENT_ELSE,      // else statement
     NODE_TYPE_STATEMENT_WHILE,     // while loop
     NODE_TYPE_STATEMENT_DO_WHILE,  // do-while loop
     NODE_TYPE_STATEMENT_FOR,       // for loop
     NODE_TYPE_STATEMENT_BREAK,     // break statement
     NODE_TYPE_STATEMENT_CONTINUE,  // continue statement
     NODE_TYPE_STATEMENT_SWITCH,    // switch statement
     NODE_TYPE_STATEMENT_CASE,      // case label
     NODE_TYPE_STATEMENT_DEFAULT,   // default label
     NODE_TYPE_STATEMENT_GOTO,      // goto statement
 
     // Advanced language constructs
     NODE_TYPE_UNARY,               // Unary expressions (!, -, etc.)
     NODE_TYPE_TENARY,              // Ternary operator (? :)
     NODE_TYPE_LABEL,               // Statement labels
     NODE_TYPE_STRUCT,              // Structure definitions
     NODE_TYPE_UNION,               // Union definitions
     NODE_TYPE_BRACKET,             // Array/function brackets []
     NODE_TYPE_CAST,                // Type casting
     NODE_TYPE_BLANK                // Empty/placeholder node
 };
 
 /**
  * @brief AST node flags
  * Bit flags for marking special properties of AST nodes
  */
 enum
 {
     NODE_FLAG_INSIDE_EXPRESSION = 0b00000001,      // Node is inside an expression
     NODE_FLAG_IS_FORWARD_DECLARATION = 0b00000010, // Forward declaration
     NODE_FLAG_HAS_VARIABLE_COMBINED = 0b00000100   // Has combined variable declaration
 };
 
 /**
  * @brief Array bracket information
  * Stores information about array dimensions and indexing
  */
 struct array_brackets
 {
     struct vector *n_brackets;  // Vector of bracket nodes for multi-dimensional arrays
 };
 
 // Forward declaration for mutual recursion
 struct node;
 
 /**
  * @brief Data type representation
  * Complete representation of C data types including qualifiers and arrays
  */
 struct datatype
 {
     int flags;                  // Type qualifiers and attributes (const, static, etc.)
     int type;                   // Basic type (int, float, struct, etc.)
     struct datatype *secondary; // For compound types like "long int"
     const char *type_str;       // String representation of type
     size_t size;               // Size in bytes
     int pointer_depth;         // Number of pointer indirections (*, **, etc.)
 
     // Union for structure/union type information
     union
     {
         struct node *struct_node;   // AST node for struct definition
         struct node *union_node;    // AST node for union definition
     };
 
     // Array type information
     struct array
     {
         struct array_brackets *brackets; // Array dimension information
         size_t size;                     // Total array size in bytes
     } array;
 };
 
 /**
  * @brief Parsed switch case information
  * Used during switch statement parsing
  */
 struct parsed_switch_case
 {
     int index;  // Index of the parsed case in the switch
 };
 
 /**
  * @brief AST Node structure
  * The main AST node that can represent any language construct
  * Uses unions to efficiently store different node types
  */
 struct node
 {
     int type;           // Node type from NODE_TYPE enum
     int flags;          // Node flags from NODE_FLAG enum
     struct pos pos;     // Source position where this construct appeared
 
     // Binding information - connects nodes to their context
     struct node_binded
     {
         struct node *owner;     // Parent/owner node
         struct node *function;  // Function this node belongs to
     } binded;
 
     // Main union containing different node structures
     union
     {
         // Expression node (binary operations like +, -, *, etc.)
         struct exp
         {
             struct node *left;   // Left operand
             struct node *right;  // Right operand
             const char *op;      // Operator string ("+", "-", etc.)
         } exp;
 
         // Parenthesized expression node
         struct parenthesis
         {
             struct node *exp;    // Expression inside parentheses
         } parenthesis;
 
         // Variable declaration node
         struct var
         {
             struct datatype type;   // Variable's data type
             int padding;           // Memory alignment padding
             int aoffset;          // Aligned memory offset
             const char *name;     // Variable name
             struct node *val;     // Initial value (if any)
         } var;
 
         // Ternary operator node (condition ? true_val : false_val)
         struct node_tenary
         {
             struct node *true_node;  // Value if condition is true
             struct node *false_node; // Value if condition is false
         } tenary;
 
         // Variable list node (for multiple declarations)
         struct varlist
         {
             struct vector *list;     // Vector of variable nodes
         } var_list;
 
         // Array/function bracket node
         struct bracket
         {
             struct node *inner;      // Expression inside brackets
         } bracket;
 
         // Structure definition node
         struct _struct
         {
             const char *name;        // Structure name
             struct node *body_n;     // Structure body (member declarations)
             struct node *var;        // Variable declared with struct (if any)
         } _struct;
 
         // Union definition node
         struct _union
         {
             const char *name;        // Union name
             struct node *body_n;     // Union body (member declarations)
             struct node *var;        // Variable declared with union (if any)
         } _union;
 
         // Code block body node
         struct body
         {
             struct vector *statements;      // Vector of statement nodes
             size_t size;                   // Combined size of variables in body
             bool padded;                   // True if padding was added
             struct node *largest_var_node; // Largest variable for alignment
         } body;
 
         // Function definition node
         struct function
         {
             int flags;                      // Function-specific flags
             struct datatype rtype;          // Return type
             const char *name;              // Function name
 
             // Function arguments information
             struct function_arguments
             {
                 struct vector *vector;      // Vector of parameter nodes
                 size_t stack_addition;     // Stack space needed for parameters
             } args;
 
             struct node *body_n;           // Function body node
             size_t stack_size;            // Total stack space needed
         } func;
 
         // Statement nodes (control flow, etc.)
         struct statement
         {
             // Return statement
             struct return_stmt
             {
                 struct node *exp;          // Return value expression
             } return_stmt;
 
             // If statement
             struct if_stmt
             {
                 struct node *cond_node;    // Condition expression
                 struct node *body_node;    // If body
                 struct node *next;         // Else clause (if any)
             } if_stmt;
 
             // Else statement
             struct else_stmt
             {
                 struct node *body_node;    // Else body
             } else_stmt;
 
             // For loop statement
             struct for_stmt
             {
                 struct node *init_node;    // Initialization expression
                 struct node *cond_node;    // Loop condition
                 struct node *loop_node;    // Loop increment/update
                 struct node *body_node;    // Loop body
             } for_stmt;
 
             // While loop statement
             struct while_stmt
             {
                 struct node *exp_node;     // Loop condition
                 struct node *body_node;    // Loop body
             } while_stmt;
 
             // Do-while loop statement
             struct do_while_stmt
             {
                 struct node *exp_node;     // Loop condition
                 struct node *body_node;    // Loop body
             } do_while_stmt;
 
             // Switch statement
             struct switch_stmt
             {
                 struct node *exp;          // Switch expression
                 struct node *body;         // Switch body
                 struct vector *cases;      // Vector of case statements
                 bool has_default_case;     // True if default case exists
             } switch_stmt;
 
             // Case statement
             struct _case_stmt
             {
                 struct node *exp;          // Case value expression
             } _case;
 
             // Goto statement
             struct _goto_stmt
             {
                 struct node *label;        // Target label
             } _goto;
         } stmt;
 
         // Label node
         struct node_label
         {
             struct node *name;             // Label name
         } label;
 
         // Type cast node
         struct cast
         {
             struct datatype dtype;         // Target data type
             struct node *operand;          // Expression being cast
         } cast;
     };
 
     // Value storage for literal nodes
     union
     {
         char cval;                         // Character literal value
         const char *sval;                  // String literal value
         unsigned int inum;                 // Integer literal value
         unsigned long lnum;                // Long literal value
         unsigned long long llnum;          // Long long literal value
     };
 };
 
 /**
  * @brief Data type flags
  * Bit flags for data type qualifiers and attributes
  */
 enum
 {
     DATATYPE_FLAG_IS_SIGNED = 0b00000001,        // signed/unsigned qualifier
     DATATYPE_FLAG_IS_STATIC = 0b00000010,        // static storage class
     DATATYPE_FLAG_IS_CONST = 0b00000100,         // const qualifier
     DATATYPE_FLAG_IS_POINTER = 0b00001000,       // pointer type
     DATATYPE_FLAG_IS_ARRAY = 0b00010000,         // array type
     DATATYPE_FLAG_IS_EXTERN = 0b00100000,        // extern storage class
     DATATYPE_FLAG_IS_RESTRICT = 0b01000000,      // restrict qualifier
     DATATYPE_FLAG_IGNORE_TYPE_CHECKING = 0b10000000, // skip type checking
     DATATYPE_FLAG_IS_SECONDARY = 0b100000000,    // secondary type in compound
     DATATYPE_FLAG_STRUCT_UNION_NO_NAME = 0b1000000000, // anonymous struct/union
     DATATYPE_FLAG_IS_LITERAL = 0b10000000000,    // literal constant
 };
 
 /**
  * @brief Basic data type enumeration
  * Defines the fundamental data types in C
  */
 enum
 {
     DATA_TYPE_VOID,     // void type
     DATA_TYPE_CHAR,     // char type
     DATA_TYPE_SHORT,    // short type
     DATA_TYPE_INTEGER,  // int type
     DATA_TYPE_LONG,     // long type
     DATA_TYPE_FLOAT,    // float type
     DATA_TYPE_DOUBLE,   // double type
     DATA_TYPE_STRUCT,   // struct type
     DATA_TYPE_UNION,    // union type
     DATA_TYPE_UNKNOWN   // unknown/unresolved type
 };
 
 /**
  * @brief Data type expectation flags
  * Used during parsing to specify expected type categories
  */
 enum
 {
     DATA_TYPE_EXPECT_PRIMITIVE, // Expect primitive type (int, char, etc.)
     DATA_TYPE_EXPECT_UNION,     // Expect union type
     DATA_TYPE_EXPECT_STRUCT     // Expect struct type
 };
 
 /**
  * @brief Data size constants
  * Standard sizes for different data types
  */
 enum
 {
     DATA_SIZE_ZERO = 0,     // Empty/void size
     DATA_SIZE_BYTE = 1,     // 1 byte (char)
     DATA_SIZE_WORD = 2,     // 2 bytes (short)
     DATA_SIZE_DWORD = 4,    // 4 bytes (int, float)
     DATA_SIZE_DDWORD = 8    // 8 bytes (long long, double)
 };
 
 /**
  * @brief Function node flags
  * Special flags for function nodes
  */
 enum
 {
     FUNCTION_NODE_FLAG_IS_NATIVE = 0b00000001,  // Native/built-in function
 };
 
 // ============================================================================
 // FUNCTION DECLARATIONS
 // ============================================================================
 
 /**
  * @brief Compile a source file to output file
  * @param filename Input source file path
  * @param out_filename Output file path
  * @param flags Compilation flags
  * @return Compilation result code
  */
 int compile_file(const char *filename, const char *out_filename, int flags);
 
 /**
  * @brief Create a new compilation process
  * @param filename Input source file
  * @param filename_out Output file
  * @param flags Compilation flags
  * @return Pointer to compile process structure
  */
 struct compile_process *compile_process_create(const char *filename, const char *filename_out, int flags);
 
 // Character input functions for compilation process
 char compile_process_next_char(struct lex_process *lex_process);
 char compile_process_peek_char(struct lex_process *lex_process);
 void compile_process_push_char(struct lex_process *lex_process, char c);
 
 // Error and warning reporting functions
 void compiler_error(struct compile_process *compiler, const char *msg, ...);
 void compiler_warning(struct compile_process *compiler, const char *msg, ...);
 
 // ============================================================================
 // LEXICAL ANALYSIS FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Create a new lexical process
  * @param compiler Parent compilation process
  * @param functions Function pointers for input operations
  * @param private Private data pointer
  * @return Pointer to lexical process structure
  */
 struct lex_process *lex_process_create(struct compile_process *compiler, struct lex_process_functions *functions, void *private);
 
 /**
  * @brief Free lexical process resources
  * @param process Lexical process to free
  */
 void lex_process_free(struct lex_process *process);
 
 /**
  * @brief Get private data from lexical process
  * @param process Lexical process
  * @return Private data pointer
  */
 void *lex_process_private(struct lex_process *process);
 
 /**
  * @brief Get tokens vector from lexical process
  * @param process Lexical process
  * @return Vector of tokens
  */
 struct vector *lex_process_tokens(struct lex_process *process);
 
 /**
  * @brief Perform lexical analysis
  * @param process Lexical process to run
  * @return Analysis result code
  */
 int lex(struct lex_process *process);
 
 /**
  * @brief Perform parsing
  * @param process Compilation process
  * @return Parsing result code
  */
 int parse(struct compile_process *process);
 
 /**
  * @brief Build tokens for input string
  * @param compiler Compilation process
  * @param str Input string to tokenize
  * @return Lexical process with tokens
  */
 struct lex_process *tokens_build_for_string(struct compile_process *compiler, const char *str);
 
 // ============================================================================
 // TOKEN UTILITY FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Check if token is a specific keyword
  * @param token Token to check
  * @param value Keyword string to match
  * @return true if token matches keyword
  */
 bool token_is_keyword(struct token *token, const char *value);
 
 /**
  * @brief Check if token is an identifier
  * @param token Token to check
  * @return true if token is an identifier
  */
 bool token_is_identifier(struct token *token);
 
 /**
  * @brief Check if token is a specific symbol
  * @param token Token to check
  * @param c Symbol character to match
  * @return true if token matches symbol
  */
 bool token_is_symbol(struct token *token, char c);
 
 /**
  * @brief Check if token is newline, comment, or separator
  * @param token Token to check
  * @return true if token is a separator type
  */
 bool token_is_nl_or_comment_or_newline_seperator(struct token *token);
 
 /**
  * @brief Check if string represents a data type keyword
  * @param str String to check
  * @return true if string is a data type
  */
 bool keyword_is_datatype(const char *str);
 
 /**
  * @brief Check if token is a primitive keyword
  * @param token Token to check
  * @return true if token is primitive type keyword
  */
 bool token_is_primitive_keyword(struct token *token);
 
 /**
  * @brief Check if name represents a struct or union
  * @param name Type name to check
  * @return true if name is struct or union type
  */
 bool datatype_is_struct_or_union_for_name(const char *name);
 
 // Data type size calculation functions
 size_t datatype_size_for_array_access(struct datatype *dtype);
 size_t datatype_element_size(struct datatype *dtype);
 size_t datatype_size_no_ptr(struct datatype *dtype);
 size_t datatype_size(struct datatype *dtype);
 
 /**
  * @brief Check if data type is primitive
  * @param dtype Data type to check
  * @return true if data type is primitive
  */
 bool datatype_is_primitive(struct datatype *dtype);
 
 /**
  * @brief Check if token is a specific operator
  * @param token Token to check
  * @param val Operator string to match
  * @return true if token matches operator
  */
 bool token_is_operator(struct token *token, const char *val);
 
 // ============================================================================
 // AST NODE FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Create a new AST node
  * @param _node Template node to copy (can be NULL)
  * @return Pointer to new node
  */
 struct node *node_create(struct node *_node);
 
 /**
  * @brief Create node from symbol
  * @param sym Symbol to create node from
  * @return Pointer to new node
  */
 struct node *node_from_sym(struct symbol *sym);
 
 /**
  * @brief Create node from symbol name lookup
  * @param current_process Compilation process
  * @param name Symbol name to lookup
  * @return Pointer to node or NULL if not found
  */
 struct node *node_from_symbol(struct compile_process *current_process, const char *name);
 
 // Node type checking functions
 bool node_is_expression_or_parentheses(struct node *node);
 bool node_is_value_type(struct node *node);
 bool node_is_expression(struct node *node, const char *op);
 bool is_array_node(struct node *node);
 bool is_node_assignment(struct node *node);
 
 /**
  * @brief Find struct node by name
  * @param current_process Compilation process
  * @param name Struct name to find
  * @return Pointer to struct node or NULL
  */
 struct node *struct_node_for_name(struct compile_process *current_process, const char *name);
 
 /**
  * @brief Find union node by name
  * @param current_process Compilation process
  * @param name Union name to find
  * @return Pointer to union node or NULL
  */
 struct node* union_node_for_name(struct compile_process* current_process, const char* name);
 
 // ============================================================================
 // NODE CREATION FUNCTIONS
 // ============================================================================
 
 // These functions create and configure specific types of AST nodes
 void make_tenary_node(struct node *true_node, struct node *false_node);
 void make_case_node(struct node *exp_node);
 void make_goto_node(struct node *label_node);
 void make_label_node(struct node *name_node);
 void make_continue_node();
 void make_break_node();
 void make_cast_node(struct datatype *dtype, struct node *operand_node);
 void make_exp_node(struct node *left_node, struct node *right_node, const char *op);
 void make_exp_parentheses_node(struct node *exp_node);
 void make_bracket_node(struct node *node);
 void make_body_node(struct vector *body_vec, size_t size, bool padded, struct node *largest_var_node);
 void make_struct_node(const char *name, struct node *body_node);
 void make_union_node(const char* name, struct node* body_node);
 void make_switch_node(struct node *exp_node, struct node *body_node, struct vector *cases, bool has_default_case);
 void make_function_node(struct datatype *ret_type, const char *name, struct vector *arguments, struct node *body_node);
 void make_while_node(struct node *exp_node, struct node *body_node);
 void make_do_while_node(struct node *body_node, struct node *exp_node);
 void make_for_node(struct node *init_node, struct node *cond_node, struct node *loop_node, struct node *body_node);
 void make_return_node(struct node *exp_node);
 void make_if_node(struct node *cond_node, struct node *body_node, struct node *next_node);
 void make_else_node(struct node *body_node);
 
 // ============================================================================
 // NODE STACK OPERATIONS
 // ============================================================================
 
 /**
  * @brief Pop node from parser stack
  * @return Pointer to popped node
  */
 struct node *node_pop();
 
 /**
  * @brief Peek at top node on parser stack
  * @return Pointer to top node
  */
 struct node *node_peek();
 
 /**
  * @brief Peek at top node, return NULL if empty
  * @return Pointer to top node or NULL
  */
 struct node *node_peek_or_null();
 
 /**
  * @brief Push node onto parser stack
  * @param node Node to push
  */
 void node_push(struct node *node);
 
 /**
  * @brief Set node vectors for parsing
  * @param vec Node vector
  * @param root_vec Root vector
  */
 void node_set_vector(struct vector *vec, struct vector *root_vec);
 
 /**
  * @brief Check if node can be part of an expression
  * @param node Node to check
  * @return true if node is expressionable
  */
 bool node_is_expressionable(struct node *node);
 
 /**
  * @brief Peek at expressionable node or return NULL
  * @return Pointer to expressionable node or NULL
  */
 struct node *node_peek_expressionable_or_null();
 
 /**
  * @brief Check if node is a struct or union variable
  * @param node Node to check
  * @return true if node is struct/union variable
  */
 bool node_is_struct_or_union_variable(struct node *node);
 
 // ============================================================================
 // ARRAY BRACKET FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Create new array brackets structure
  * @return Pointer to array brackets
  */
 struct array_brackets *array_brackets_new();
 
 /**
  * @brief Free array brackets structure
  * @param brackets Array brackets to free
  */
 void array_brackets_free(struct array_brackets *brackets);
 
 /**
  * @brief Add bracket node to array brackets
  * @param brackets Array brackets structure
  * @param bracket_node Bracket node to add
  */
 void array_brackets_add(struct array_brackets *brackets, struct node *bracket_node);
 
 /**
  * @brief Get vector of bracket nodes
  * @param brackets Array brackets structure
  * @return Vector of bracket nodes
  */
 struct vector *array_brackets_node_vector(struct array_brackets *brackets);
 
 // Array size calculation functions
 size_t array_brackets_calculate_size_from_index(struct datatype *dtype, struct array_brackets *brackets, int index);
 size_t array_brackets_calculate_size(struct datatype *dtype, struct array_brackets *brackets);
 int array_total_indexes(struct datatype *dtype);
 
 /**
  * @brief Check if data type is struct or union
  * @param dtype Data type to check
  * @return true if struct or union
  */
 bool datatype_is_struct_or_union(struct datatype *dtype);
 
 /**
  * @brief Get body node of struct or union variable
  * @param node Variable node
  * @return Body node or NULL
  */
 struct node *variable_struct_or_union_body_node(struct node *node);
 
 /**
  * @brief Get variable node from variable or list
  * @param node Node to check
  * @return Variable node
  */
 struct node *variable_node_or_list(struct node *node);
 
 /**
  * @brief Get size of variable from variable node
  * @param var_node Variable node
  * @return Size in bytes
  */
 size_t variable_size(struct node *var_node);
 
 /**
  * @brief Sum variable sizes in variable list
  * @param var_list_node Variable list node
  * @return Total size of all variables
  */
 size_t variable_size_for_list(struct node *var_list_node);
 
 /**
  * @brief Get variable node from any node type
  * @param node Input node
  * @return Variable node or NULL
  */
 struct node *variable_node(struct node *node);
 
 /**
  * @brief Check if variable node has primitive type
  * @param node Variable node to check
  * @return true if primitive type
  */
 bool variable_node_is_primitive(struct node *node);
 
 // ============================================================================
 // MEMORY ALIGNMENT FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Calculate padding needed for alignment
  * @param val Current value
  * @param to Alignment boundary
  * @return Padding needed
  */
 int padding(int val, int to);
 
 /**
  * @brief Align value to boundary
  * @param val Value to align
  * @param to Alignment boundary
  * @return Aligned value
  */
 int align_value(int val, int to);
 
 /**
  * @brief Align value treating as positive
  * @param val Value to align
  * @param to Alignment boundary
  * @return Aligned value
  */
 int align_value_treat_positive(int val, int to);
 
 /**
  * @brief Compute sum of padding for vector of items
  * @param vec Vector of items
  * @return Total padding needed
  */
 int compute_sum_padding(struct vector *vec);
 
 // ============================================================================
 // SCOPE MANAGEMENT FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Create new scope
  * @param process Compilation process
  * @param flags Scope flags
  * @return Pointer to new scope
  */
 struct scope *scope_new(struct compile_process *process, int flags);
 
 /**
  * @brief Create root/global scope
  * @param process Compilation process
  * @return Pointer to root scope
  */
 struct scope *scope_create_root(struct compile_process *process);
 
 /**
  * @brief Free root scope and all children
  * @param process Compilation process
  */
 void scope_free_root(struct compile_process *process);
 
 // Scope iteration functions
 void scope_iteration_start(struct scope *scope);
 void scope_iteration_end(struct scope *scope);
 void *scope_iterate_back(struct scope *scope);
 
 /**
  * @brief Get last entity at specific scope level
  * @param scope Target scope
  * @return Last entity or NULL
  */
 void *scope_last_entity_at_scope(struct scope *scope);
 
 /**
  * @brief Get last entity searching up to stop scope
  * @param scope Starting scope
  * @param stop_scope Scope to stop at
  * @return Last entity found or NULL
  */
 void *scope_last_entity_from_scope_stop_at(struct scope *scope, struct scope *stop_scope);
 
 /**
  * @brief Get last entity stopping at specific scope
  * @param process Compilation process
  * @param stop_scope Scope to stop searching at
  * @return Last entity or NULL
  */
 void *scope_last_entity_stop_at(struct compile_process *process, struct scope *stop_scope);
 
 /**
  * @brief Get last entity in current scope chain
  * @param process Compilation process
  * @return Last entity or NULL
  */
 void *scope_last_entity(struct compile_process *process);
 
 /**
  * @brief Push entity onto current scope
  * @param process Compilation process
  * @param ptr Pointer to entity
  * @param elem_size Size of entity
  */
 void scope_push(struct compile_process *process, void *ptr, size_t elem_size);
 
 /**
  * @brief Finish current scope
  * @param process Compilation process
  */
 void scope_finish(struct compile_process *process);
 
 /**
  * @brief Get current active scope
  * @param process Compilation process
  * @return Current scope
  */
 struct scope *scope_current(struct compile_process *process);
 
 // ============================================================================
 // SYMBOL RESOLUTION FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Initialize symbol resolver
  * @param process Compilation process
  */
 void symresolver_initialize(struct compile_process *process);
 
 /**
  * @brief Create new symbol table
  * @param process Compilation process
  */
 void symresolver_new_table(struct compile_process *process);
 
 /**
  * @brief End current symbol table
  * @param process Compilation process
  */
 void symresolver_end_table(struct compile_process *process);
 
 /**
  * @brief Build symbols for AST node
  * @param process Compilation process
  * @param node AST node to process
  */
 void symresolver_build_for_node(struct compile_process *process, struct node *node);
 
 /**
  * @brief Get symbol by name
  * @param process Compilation process
  * @param name Symbol name
  * @return Symbol pointer or NULL
  */
 struct symbol *symresolver_get_symbol(struct compile_process *process, const char *name);
 
 /**
  * @brief Get native function symbol
  * @param process Compilation process
  * @param name Function name
  * @return Symbol pointer or NULL
  */
 struct symbol *symresolver_get_symbol_for_native_function(struct compile_process *process, const char *name);
 
 /**
  * @brief Calculate stack space for function arguments
  * @param node Function node
  * @return Stack space needed
  */
 size_t function_node_argument_stack_addition(struct node *node);
 
 // ============================================================================
 // OPERATOR PRECEDENCE SYSTEM
 // ============================================================================
 
 #define TOTAL_OPERATOR_GROUPS 14    // Total number of precedence groups
 #define MAX_OPERATORS_IN_GROUP 12   // Maximum operators per group
 
 /**
  * @brief Operator associativity
  */
 enum
 {
     ASSOCIATIVITY_LEFT_TO_RIGHT,  // Left-to-right associativity (most operators)
     ASSOCIATIVITY_RIGHT_TO_LEFT   // Right-to-left associativity (assignment, unary)
 };
 
 /**
  * @brief Operator precedence group
  * Defines operators with same precedence and their associativity
  */
 struct expressionable_op_precedence_group
 {
     char *operators[MAX_OPERATORS_IN_GROUP]; // Array of operator strings
     int associtivity;                        // Associativity for this group
 };
 
 // ============================================================================
 // FIXUP SYSTEM FOR CODE GENERATION
 // ============================================================================
 
 struct fixup;  // Forward declaration
 
 /**
  * @brief Function pointer for fixing a fixup
  * @param fixup Fixup to resolve
  * @return true if fixup was successful
  */
 typedef bool (*FIXUP_FIX)(struct fixup *fixup);
 
 /**
  * @brief Function pointer for cleanup when fixup is removed
  * @param fixup Fixup being freed
  */
 typedef void (*FIXUP_END)(struct fixup *fixup);
 
 /**
  * @brief Fixup configuration
  * Contains function pointers and private data for a fixup
  */
 struct fixup_config
 {
     FIXUP_FIX fix;      // Function to apply the fixup
     FIXUP_END end;      // Function to clean up the fixup
     void *private;      // Private data for the fixup
 };
 
 /**
  * @brief Fixup system
  * Manages all fixups for forward references and code patching
  */
 struct fixup_system
 {
     struct vector *fixups;  // Vector of all registered fixups
 };
 
 /**
  * @brief Fixup flags
  */
 enum
 {
     FIXUP_FLAG_RESOLVED = 0b00000001  // Fixup has been resolved
 };
 
 /**
  * @brief Individual fixup structure
  * Represents a single fixup that needs to be resolved
  */
 struct fixup
 {
     int flags;                      // Fixup flags
     struct fixup_system *system;    // Parent fixup system
     struct fixup_config config;     // Configuration and callbacks
 };
 
 // ============================================================================
 // FIXUP SYSTEM FUNCTIONS
 // ============================================================================
 
 /**
  * @brief Create new fixup system
  * @return Pointer to new fixup system
  */
 struct fixup_system *fixup_sys_new();
 
 /**
  * @brief Get configuration from fixup
  * @param fixup Fixup to get config from
  * @return Pointer to fixup configuration
  */
 struct fixup_config *fixup_config(struct fixup *fixup);
 
 /**
  * @brief Free individual fixup
  * @param fixup Fixup to free
  */
 void fixup_free(struct fixup *fixup);
 
 /**
  * @brief Start iterating through fixups
  * @param system Fixup system
  */
 void fixup_start_iteration(struct fixup_system *system);
 
 /**
  * @brief Get next fixup in iteration
  * @param system Fixup system
  * @return Next fixup or NULL
  */
 struct fixup *fixup_next(struct fixup_system *system);
 
 /**
  * @brief Free all fixups in system
  * @param system Fixup system
  */
 void fixup_sys_fixups_free(struct fixup_system *system);
 
 /**
  * @brief Free entire fixup system
  * @param system Fixup system to free
  */
 void fixup_sys_free(struct fixup_system *system);
 
 /**
  * @brief Count unresolved fixups
  * @param system Fixup system
  * @return Number of unresolved fixups
  */
 int fixup_sys_unresolved_fixups_count(struct fixup_system *system);
 
 /**
  * @brief Register new fixup in system
  * @param system Fixup system
  * @param config Fixup configuration
  * @return Pointer to registered fixup
  */
 struct fixup *fixup_register(struct fixup_system *system, struct fixup_config *config);
 
 /**
  * @brief Attempt to resolve a fixup
  * @param fixup Fixup to resolve
  * @return true if resolution successful
  */
 bool fixup_resolve(struct fixup *fixup);
 
 /**
  * @brief Get private data from fixup
  * @param fixup Fixup to get data from
  * @return Private data pointer
  */
 void *fixup_private(struct fixup *fixup);
 
 /**
  * @brief Resolve all fixups in system
  * @param system Fixup system
  * @return true if all fixups resolved successfully
  */
 bool fixups_resolve(struct fixup_system *system);
 
 #endif