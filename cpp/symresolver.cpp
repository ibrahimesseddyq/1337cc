#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdlib>
#include <cassert>

// Appends a symbol pointer to the active symbol table of the compile process.
// This is the only place symbols are written into the table vector; all other
// registration helpers funnel through here.
// process  — the compile process whose active table receives the symbol.
// sym      — the symbol to append.
static void symresolver_push_symbol(struct compile_process *process, struct symbol *sym)
{
    vector_push(process->symbols.table, &sym);
}

// Sets up the symbol resolver subsystem for a compile process by creating the
// outer "tables" vector that holds a stack of per-scope symbol table vectors.
// Must be called once before any other symresolver function.
// process  — the compile process to initialise.
void symresolver_initialize(struct compile_process *process)
{
    process->symbols.tables = vector_create(sizeof(struct vector *));
}

// Saves the current active symbol table onto the tables stack and opens a fresh
// empty table as the new active table.  Used when entering a new symbol scope
// (e.g. a function body) so that symbols defined inside do not pollute the
// outer scope.
// process  — the compile process that gets a new symbol table layer.
void symresolver_new_table(struct compile_process *process)
{
    vector_push(process->symbols.tables, &process->symbols.table);
    process->symbols.table = vector_create(sizeof(struct symbol *));
}

// Closes the current active symbol table and restores the previously saved
// table from the top of the tables stack.  Symmetric counterpart to
// symresolver_new_table(); must only be called after a matching
// symresolver_new_table() call.
// process  — the compile process whose symbol table stack is popped.
void symresolver_end_table(struct compile_process *process)
{
    struct vector *last_table =
        vector_back_ptr_typed<struct vector>(process->symbols.tables);
    process->symbols.table = last_table;
    vector_pop(process->symbols.tables);
}

// Searches the active symbol table for a symbol whose name matches name using
// S_EQ (case-sensitive string comparison).  Iterates the table from the start
// on each call, so lookup is O(n).
// Returns the matching symbol pointer, or nullptr if no symbol with that name
// exists in the current table.
// process  — the compile process whose active symbol table is searched.
// name     — the null-terminated name string to look up.
struct symbol *symresolver_get_symbol(struct compile_process *process, const char *name)
{
    vector_set_peek_pointer(process->symbols.table, 0);
    struct symbol *symbol = vector_peek_ptr_typed<struct symbol>(process->symbols.table);
    while (symbol)
    {
        if (S_EQ(symbol->name, name))
            break;
        symbol = vector_peek_ptr_typed<struct symbol>(process->symbols.table);
    }
    return symbol;
}

// Looks up name in the active symbol table and, if found, verifies that the
// symbol's type is SYMBOL_TYPE_NATIVE_FUNCTION.
// Returns the symbol if it exists and is a native function; returns nullptr if
// the symbol is not found or has a different type.
// process  — the compile process whose active symbol table is searched.
// name     — the null-terminated name of the native function to look up.
struct symbol *symresolver_get_symbol_for_native_function(struct compile_process *process,
                                                          const char *name)
{
    struct symbol *sym = symresolver_get_symbol(process, name);
    if (!sym)
        return nullptr;
    if (sym->type != SYMBOL_TYPE_NATIVE_FUNCTION)
        return nullptr;
    return sym;
}

// Registers a new symbol in the active table if no symbol with sym_name already
// exists.  If a duplicate is detected the function returns nullptr rather than
// inserting a second entry.  On success it allocates a symbol struct, fills in
// name, type, and data, pushes it into the table, and returns the new symbol.
// process   — the compile process whose active table receives the symbol.
// sym_name  — the name string the symbol will be keyed on.
// type      — one of the SYMBOL_TYPE_* constants describing what data points to.
// data      — the payload the symbol wraps (e.g. a struct node *).
static struct symbol *symresolver_register_symbol(struct compile_process *process,
                                                   const char *sym_name, int type, void *data)
{
    if (symresolver_get_symbol(process, sym_name))
        return nullptr;

    struct symbol *sym = static_cast<struct symbol *>(calloc(1, sizeof(struct symbol)));
    sym->name = sym_name;
    sym->type = type;
    sym->data = data;
    symresolver_push_symbol(process, sym);
    return sym;
}

// Stub handler invoked when symresolver_build_for_node() encounters a variable
// node.  Variable symbols are not yet implemented; this always calls
// compiler_error() to halt compilation with an informative message.
// process  — the active compile process (used to report the error).
// node     — the variable node that triggered this call (currently unused).
static void symresolver_build_for_variable_node(struct compile_process *process,
                                                struct node * /*node*/)
{
    compiler_error(process, "Variables not yet supported\n");
}

// Stub handler invoked when symresolver_build_for_node() encounters a function
// node.  Function symbols are not yet implemented; this always calls
// compiler_error() to halt compilation with an informative message.
// process  — the active compile process (used to report the error).
// node     — the function node that triggered this call (currently unused).
static void symresolver_build_for_function_node(struct compile_process *process,
                                                struct node * /*node*/)
{
    compiler_error(process, "Functions are not yet supported\n");
}

// Registers a struct definition node in the active symbol table under the
// struct's name with type SYMBOL_TYPE_NODE.  Forward declarations are skipped
// because they carry no body and should not shadow a later full definition.
// process  — the compile process whose symbol table is updated.
// node     — a NODE_TYPE_STRUCT node to register.
static void symresolver_build_for_structure_node(struct compile_process *process,
                                                 struct node *node)
{
    if (node->flags & NODE_FLAG_IS_FORWARD_DECLARATION)
        return;
    symresolver_register_symbol(process, node->_struct.name, SYMBOL_TYPE_NODE, node);
}

// Registers a union definition node in the active symbol table under the
// union's name with type SYMBOL_TYPE_NODE.  Forward declarations are skipped
// for the same reason as in symresolver_build_for_structure_node().
// process  — the compile process whose symbol table is updated.
// node     — a NODE_TYPE_UNION node to register.
static void symresolver_build_for_union_node(struct compile_process *process,
                                             struct node *node)
{
    if (node->flags & NODE_FLAG_IS_FORWARD_DECLARATION)
        return;
    symresolver_register_symbol(process, node->_union.name, SYMBOL_TYPE_NODE, node);
}

// Dispatches an AST node to the appropriate type-specific symbol-registration
// handler.  Called once per top-level node after parsing to populate the symbol
// table.  Node types that do not yet have a handler (or are irrelevant to name
// resolution) fall through the default branch silently.
// process  — the active compile process.
// node     — the AST node to register; its type field selects the handler.
void symresolver_build_for_node(struct compile_process *process, struct node *node)
{
    switch (node->type)
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
    default:
        break;
    }
}
