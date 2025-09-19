/**
 * @file symresolver.c
 * @brief Simple symbol resolver for the compiler frontend.
 *
 * This module implements a basic symbol table stack used by the parser to
 * register and lookup symbols (structs, unions, functions, nodes, native functions, etc).
 *
 * Design notes:
 *  - `process->symbols.table` points to the active symbol table (a vector of struct symbol*).
 *  - `process->symbols.tables` is a stack of symbol-table vectors used to support nested scopes
 *    or temporary symbol table overrides (created with symresolver_new_table / symresolver_end_table).
 *  - Symbols are stored as pointers to heap-allocated `struct symbol`. Ownership and cleanup
 *    policy is outside the scope of this module (caller should free symbols if required).
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include <memory.h>
 #include <stdlib.h>
 #include <assert.h>
 
 /**
  * @brief Push a symbol pointer into the active symbol table vector.
  *
  * This helper centralizes pushing to the table; it takes the process and a
  * pointer to a preallocated struct symbol and appends it to the active table.
  *
  * @param process Compile process owning the symbol table.
  * @param sym     Pointer to the symbol to push.
  */
 static void symresolver_push_symbol(struct compile_process* process, struct symbol* sym)
 {
     vector_push(process->symbols.table, &sym);
 }
 
 /**
  * @brief Initialize the symbol resolver state for a compile process.
  *
  * Creates the vector that will hold symbol table vectors (the table-of-tables).
  *
  * @param process Compile process to initialize.
  */
 void symresolver_initialize(struct compile_process* process)
 {
     process->symbols.tables = vector_create(sizeof(struct vector*));
 }
 
 /**
  * @brief Create a new active symbol table and push the current table onto the table stack.
  *
  * This allows temporary replacement of the active table (useful for nested
  * compilation scopes). The previous active table is saved to process->symbols.tables.
  *
  * @param process Compile process whose active table will be replaced.
  */
 void symresolver_new_table(struct compile_process* process)
 {
     /* Save the current table */
     vector_push(process->symbols.tables, &process->symbols.table);
 
     /* Overwrite the active table with a freshly created table vector. */
     process->symbols.table = vector_create(sizeof(struct symbol*));
 }
 
 /**
  * @brief Restore the previous active symbol table by popping the table stack.
  *
  * The top-of-stack table pointer is restored to process->symbols.table and removed
  * from the tables vector.
  *
  * @param process Compile process whose table stack will be popped.
  */
 void symresolver_end_table(struct compile_process* process)
 {
     struct vector* last_table = vector_back_ptr(process->symbols.tables);
     process->symbols.table = last_table;
     vector_pop(process->symbols.tables);
 }
 
 /**
  * @brief Lookup a symbol by name in the active symbol table.
  *
  * Linear search is used over the active table vector. The active vector's peek
  * pointer is reset to the beginning before searching to ensure deterministic iteration.
  *
  * @param process Compile process containing the active symbol table.
  * @param name    Null-terminated symbol name to lookup.
  * @return Pointer to the found symbol or NULL if not found.
  */
 struct symbol* symresolver_get_symbol(struct compile_process* process, const char* name)
 {
     vector_set_peek_pointer(process->symbols.table, 0);
     struct symbol* symbol = vector_peek_ptr(process->symbols.table);
     while(symbol)
     {
         if (S_EQ(symbol->name, name))
         {
             break;
         }
 
         symbol = vector_peek_ptr(process->symbols.table);
     }
 
     return symbol;
 }
 
 /**
  * @brief Lookup a symbol and ensure it represents a native function.
  *
  * Convenience wrapper over symresolver_get_symbol that additionally checks
  * the symbol's type is SYMBOL_TYPE_NATIVE_FUNCTION.
  *
  * @param process Compile process.
  * @param name    Name of the symbol (function).
  * @return Pointer to the symbol if found and is a native function; otherwise NULL.
  */
 struct symbol* symresolver_get_symbol_for_native_function(struct compile_process* process, const char* name)
 {
     struct symbol* sym = symresolver_get_symbol(process, name);
     if (!sym)
     {
         return NULL;
     }
 
     if (sym->type != SYMBOL_TYPE_NATIVE_FUNCTION)
     {
         return NULL;
     }
 
     return sym;
 }
 
 /**
  * @brief Register a new symbol in the active table.
  *
  * If a symbol with the same name already exists in the active table, registration
  * fails and NULL is returned. Otherwise a new struct symbol is allocated, initialized,
  * pushed into the active table and returned.
  *
  * @param process  Compile process owning the symbol table.
  * @param sym_name Name of the symbol to register (pointer is stored, string ownership assumed to be external).
  * @param type     Symbol type (one of SYMBOL_TYPE_*).
  * @param data     User data pointer associated with the symbol (e.g., node pointer).
  * @return Pointer to the newly created struct symbol or NULL on duplicate.
  */
 struct symbol* symresolver_register_symbol(struct compile_process* process, const char* sym_name, int type, void* data)
 {
     if (symresolver_get_symbol(process, sym_name))
     {
         return NULL;
     }
 
     struct symbol* sym = calloc(1, sizeof(struct symbol));
     sym->name = sym_name;
     sym->type = type;
     sym->data = data;
     symresolver_push_symbol(process, sym);
     return sym;
 }
 
 /**
  * @brief Return the node stored in a symbol if the symbol holds a node.
  *
  * @param sym Symbol to inspect.
  * @return Node pointer if symbol->type == SYMBOL_TYPE_NODE, otherwise NULL.
  */
 struct node* symresolver_node(struct symbol* sym)
 {
     if (sym->type != SYMBOL_TYPE_NODE)
     {
         return NULL;
     }
 
     return sym->data;
 }
 
 /**
  * @brief Build symbol table entries for a variable node.
  *
  * Currently unimplemented: emits an error placeholder. Intended to register
  * variable symbols with appropriate names and scopes.
  *
  * @param process Compile process.
  * @param node    Variable node to build symbol entry for.
  */
 void symresolver_build_for_variable_node(struct compile_process* process, struct node* node)
 {
     compiler_error(process, "Variables not yet supported\n");
 }
 
 /**
  * @brief Build symbol table entries for a function node.
  *
  * Currently unimplemented: emits an error placeholder. Intended to register
  * function symbols and their signatures.
  *
  * @param process Compile process.
  * @param node    Function node to build symbol entry for.
  */
 void symresolver_build_for_function_node(struct compile_process* process, struct node* node)
 {
     compiler_error(process, "Functions are not yet supported\n");
 }
 
 /**
  * @brief Register a parsed structure (struct) node as a symbol.
  *
  * Forward declarations are not registered. For a completed struct definition
  * the function registers a SYMBOL_TYPE_NODE symbol with the struct node as data.
  *
  * @param process Compile process.
  * @param node    Struct node to register.
  */
 void symresolver_build_for_structure_node(struct compile_process* process, struct node* node)
 {
     if (node->flags & NODE_FLAG_IS_FORWARD_DECLARATION)
     {
         /* We do not register forward declarations. */
         return;
     }
 
     symresolver_register_symbol(process, node->_struct.name, SYMBOL_TYPE_NODE, node);
 }
 
 /**
  * @brief Register a parsed union node as a symbol.
  *
  * Behaves similarly to symresolver_build_for_structure_node (skips forward-decls).
  *
  * @param process Compile process.
  * @param node    Union node to register.
  */
 void symresolver_build_for_union_node(struct compile_process* process, struct node* node)
 {
     if (node->flags & NODE_FLAG_IS_FORWARD_DECLARATION)
     {
         /* We do not register forward declarations. */
         return;
     }
 
     symresolver_register_symbol(process, node->_union.name, SYMBOL_TYPE_NODE, node);
 }
 
 /**
  * @brief Build symbols for the given AST node if it represents a symbolic entity.
  *
  * Dispatches registration logic based on the node type. Currently only struct
  * and union registration are implemented; variable/function registration are
  * placeholders that currently emit errors.
  *
  * @param process Compile process.
  * @param node    Node to examine and possibly register.
  */
 void symresolver_build_for_node(struct compile_process* process, struct node* node)
 {
     switch(node->type)
     {
         case NODE_TYPE_VARIABLE:
             symresolver_build_for_variable_node(process, node);
             break;
 
         case NODE_TYPE_FUNCTION:
             symresolver_build_for_function_node(process, node);
             break;
 
         case NODE_TYPE_STRUCT:
             symresolver_build_for_structure_node(process, node);
             break;
 
         case NODE_TYPE_UNION:
             symresolver_build_for_union_node(process, node);
             break;
 
         /* Ignore all other node types, because they can't become symbols. */
     }
 }
 