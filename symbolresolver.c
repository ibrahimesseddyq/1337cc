#include "compiler.h"
#include "helpers/vector.h"
#include <stdlib.h>

static void symbolresolver_push_symbol(struct compile_process* process, struct symbol* sym)
{
    vector_push(process->symbols.table, &sym);
}
void symbolresolver_initialize(struct compile_process* process)
{
    process->symbols.tables = vector_create(sizeof(struct vector*));

}

void symbolresolver_new_table(struct compile_process* process)
{
    vector_push(process->symbols.tables, &process->symbols.table);

    process->symbols.table = vector_create(sizeof(struct symbol*));

}

void symbolresolver_end_table(struct compile_process* process)
{
    struct vector* last_table = vector_back_ptr(process->symbols.tables);

    process->symbols.table = last_table;
    vector_pop(process->symbols.tables);
}

struct symbol* symbolresolver_get_symbol(struct compile_process* process, const char *name)
{
    vector_set_peek_pointer(process->symbols.table, 0);
    struct symbol* symbol = vector_peek_ptr(process->symbols.table);
    while (symbol)
    {
        if (S_EQ(symbol->name, name))
        {
            break;
        }
        symbol = vector_peek_ptr(process->symbols.table);
    }
    return symbol;
}

struct symbol* symbolresolver_get_symbol_for_native_function(struct compile_process* process, const char * name)
{
    struct symbol* sym = symbolresolver_get_symbol(process, name);
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

struct symbol* symbolresolver_register_symbol(struct compile_process* process, const char* sym_name, int type, void* data)
{
    if (symbolresolver_get_symbol(process, sym_name))
    {
        return NULL;
    }

     struct symbol* sym = calloc(1, sizeof(struct symbol));

     sym->name = sym_name;
     sym->type = type;
     sym->data = data;
     symbolresolver_push_symbol(process, sym);
     return sym;
}

struct node* symbolresolver_node(struct symbol* sym)
{
    if (sym->type != SYMBOL_TYPE_NODE)
    {
        return NULL;
    }

    return sym->data;
}

void symbolresolver_build_for_variable_node(struct compile_process* process, struct node* node)
{
    compiler_error(process, "Variables not yet supported");

}

void symbolresolver_build_for_functions(struct compile_process* process, struct node* node)
{
    compiler_error(process, "Functions not yet supported");

}

void symbolresolver_build_for_structure_node(struct compile_process* process, struct node* node)
{
    if (node->flags & NODE_FLAG_IS_FORWARD_DECLARATION)
    {
        return;
    }
    symbolresolver_register_symbol(process, node->_struct.name, SYMBOL_TYPE_NODE, node);
}

void symbolresolver_build_for_union_node(struct compile_process* process, struct node* node)
{
    compiler_error(process, "Structures not yet supported");

}

void symbolresolver_build_for_node(struct compile_process* process, struct node* node)
{
    switch (node->type)
    {
        case NODE_TYPE_VARIABLE:
            symbolresolver_build_for_variable_node(process, node);
            break;
        case NODE_TYPE_FUNCTION:
            symbolresolver_build_for_functions(process, node);
            break;
        case NODE_TYPE_STRUCT:
            symbolresolver_build_for_structure_node(process, node);
            break;
        case NODE_TYPE_UNION:
            symbolresolver_build_for_union_node(process, node);
            break;
    }
}
