/**
 * @file ast_printer.c
 * @brief AST visualization utilities for debugging and understanding the parse tree
 *
 * This module provides functions to print the Abstract Syntax Tree in a 
 * human-readable vertical tree format with proper indentation and node details.
 */

#include "compiler.h"
#include "helpers/vector.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Print indentation for tree visualization
 * @param depth The current depth level
 * @param is_last Array indicating if this is the last child at each level
 */
static void print_tree_indent(int depth, bool *is_last)
{
    for (int i = 0; i < depth - 1; i++)
    {
        if (is_last[i])
            printf("    ");
        else
            printf("│   ");
    }
    
    if (depth > 0)
    {
        if (is_last[depth - 1])
            printf("└── ");
        else
            printf("├── ");
    }
}

/**
 * @brief Get string representation of node type
 */
static const char* node_type_to_string(int type)
{
    switch (type)
    {
        case NODE_TYPE_EXPRESSION:           return "EXPRESSION";
        case NODE_TYPE_EXPRESSION_PARENTHESES: return "EXPRESSION_PARENTHESES";
        case NODE_TYPE_NUMBER:              return "NUMBER";
        case NODE_TYPE_IDENTIFIER:          return "IDENTIFIER";
        case NODE_TYPE_STRING:              return "STRING";
        case NODE_TYPE_VARIABLE:            return "VARIABLE";
        case NODE_TYPE_VARIABLE_LIST:       return "VARIABLE_LIST";
        case NODE_TYPE_FUNCTION:            return "FUNCTION";
        case NODE_TYPE_BODY:                return "BODY";
        case NODE_TYPE_STATEMENT_RETURN:    return "STATEMENT_RETURN";
        case NODE_TYPE_STATEMENT_IF:        return "STATEMENT_IF";
        case NODE_TYPE_STATEMENT_ELSE:      return "STATEMENT_ELSE";
        case NODE_TYPE_STATEMENT_WHILE:     return "STATEMENT_WHILE";
        case NODE_TYPE_STATEMENT_DO_WHILE:  return "STATEMENT_DO_WHILE";
        case NODE_TYPE_STATEMENT_FOR:       return "STATEMENT_FOR";
        case NODE_TYPE_STATEMENT_BREAK:     return "STATEMENT_BREAK";
        case NODE_TYPE_STATEMENT_CONTINUE:  return "STATEMENT_CONTINUE";
        case NODE_TYPE_STATEMENT_SWITCH:    return "STATEMENT_SWITCH";
        case NODE_TYPE_STATEMENT_CASE:      return "STATEMENT_CASE";
        case NODE_TYPE_STATEMENT_DEFAULT:   return "STATEMENT_DEFAULT";
        case NODE_TYPE_STATEMENT_GOTO:      return "STATEMENT_GOTO";
        case NODE_TYPE_UNARY:               return "UNARY";
        case NODE_TYPE_TENARY:              return "TENARY";
        case NODE_TYPE_LABEL:               return "LABEL";
        case NODE_TYPE_STRUCT:              return "STRUCT";
        case NODE_TYPE_UNION:               return "UNION";
        case NODE_TYPE_BRACKET:             return "BRACKET";
        case NODE_TYPE_CAST:                return "CAST";
        case NODE_TYPE_BLANK:               return "BLANK";
        default:                            
        {
            // Debug: print actual type value
            static char unknown_buf[50];
            snprintf(unknown_buf, sizeof(unknown_buf), "UNKNOWN(%d)", type);
            return unknown_buf;
        }
    }
}

/**
 * @brief Print detailed information about a node
 */
static void print_node_details(struct node *node)
{
    if (!node)
    {
        printf("(NULL)\n");
        return;
    }

    printf("%s", node_type_to_string(node->type));

    // Add specific details based on node type
    switch (node->type)
    {
        case NODE_TYPE_IDENTIFIER:
            if (node->sval)
                printf(" '%s'", node->sval);
            break;
            
        case NODE_TYPE_NUMBER:
            printf(" %lld", node->llnum);
            break;
            
        case NODE_TYPE_STRING:
            if (node->sval)
                printf(" \"%s\"", node->sval);
            break;
            
        case NODE_TYPE_EXPRESSION:
            if (node->exp.op)
                printf(" '%s'", node->exp.op);
            break;
            
        case NODE_TYPE_VARIABLE:
            if (node->var.name)
                printf(" '%s'", node->var.name);
            break;
            
        case NODE_TYPE_FUNCTION:
            if (node->func.name)
                printf(" '%s'", node->func.name);
            break;
            
        case NODE_TYPE_STRUCT:
            if (node->_struct.name)
                printf(" '%s'", node->_struct.name);
            break;
            
        case NODE_TYPE_UNION:
            if (node->_union.name)
                printf(" '%s'", node->_union.name);
            break;

        case NODE_TYPE_STATEMENT_RETURN:
            printf(" (return statement)");
            break;

        default:
            // For unknown types, show some additional debug info
            if (node->sval)
                printf(" sval='%s'", node->sval);
            break;
    }

    // Add position information (only if it seems valid)
    if (node->pos.line > 0 || node->pos.col > 0)
        printf(" [%d:%d]", node->pos.line, node->pos.col);
    else
        printf(" [no position]");
    
    printf("\n");
}

/**
 * @brief Recursively print AST node and its children
 */
static void print_ast_node_recursive(struct node *node, int depth, bool *is_last)
{
    if (!node)
        return;

    print_tree_indent(depth, is_last);
    print_node_details(node);

    // Count children to determine which is the last
    struct node *children[10] = {0}; // Assuming max 10 children per node
    int child_count = 0;

    // Collect children based on node type
    switch (node->type)
    {
        case NODE_TYPE_EXPRESSION:
            if (node->exp.left) children[child_count++] = node->exp.left;
            if (node->exp.right) children[child_count++] = node->exp.right;
            break;
            
        case NODE_TYPE_EXPRESSION_PARENTHESES:
            if (node->parenthesis.exp) children[child_count++] = node->parenthesis.exp;
            break;
            
        case NODE_TYPE_VARIABLE:
            if (node->var.val) children[child_count++] = node->var.val;
            break;
            
        case NODE_TYPE_TENARY:
            if (node->tenary.true_node) children[child_count++] = node->tenary.true_node;
            if (node->tenary.false_node) children[child_count++] = node->tenary.false_node;
            break;
            
        case NODE_TYPE_BRACKET:
            if (node->bracket.inner) children[child_count++] = node->bracket.inner;
            break;
            
        case NODE_TYPE_STRUCT:
            if (node->_struct.body_n) children[child_count++] = node->_struct.body_n;
            if (node->_struct.var) children[child_count++] = node->_struct.var;
            break;
            
        case NODE_TYPE_UNION:
            if (node->_union.body_n) children[child_count++] = node->_union.body_n;
            if (node->_union.var) children[child_count++] = node->_union.var;
            break;
            
        case NODE_TYPE_FUNCTION:
            if (node->func.args.vector)
            {
                // Add function arguments as children
                for (int i = 0; i < node->func.args.vector->count && child_count < 10; i++)
                {
                    struct node **arg_ptr = vector_at(node->func.args.vector, i);
                    if (arg_ptr && *arg_ptr) children[child_count++] = *arg_ptr;
                }
            }
            if (node->func.body_n) children[child_count++] = node->func.body_n;
            break;
            
        case NODE_TYPE_BODY:
            if (node->body.statements)
            {
                // Add all statements as children
                for (int i = 0; i < node->body.statements->count && child_count < 10; i++)
                {
                    struct node **stmt_ptr = vector_at(node->body.statements, i);
                    if (stmt_ptr && *stmt_ptr) children[child_count++] = *stmt_ptr;
                }
            }
            break;
            
        case NODE_TYPE_VARIABLE_LIST:
            if (node->var_list.list)
            {
                // Add all variables as children
                for (int i = 0; i < node->var_list.list->count && child_count < 10; i++)
                {
                    struct node **var_ptr = vector_at(node->var_list.list, i);
                    if (var_ptr && *var_ptr) children[child_count++] = *var_ptr;
                }
            }
            break;
            
        case NODE_TYPE_STATEMENT_RETURN:
            if (node->stmt.return_stmt.exp) children[child_count++] = node->stmt.return_stmt.exp;
            break;
    }

    // Print children
    for (int i = 0; i < child_count; i++)
    {
        bool *new_is_last = malloc((depth + 2) * sizeof(bool));
        if (new_is_last)
        {
            memcpy(new_is_last, is_last, (depth + 1) * sizeof(bool));
            new_is_last[depth] = (i == child_count - 1);
            
            print_ast_node_recursive(children[i], depth + 1, new_is_last);
            
            free(new_is_last);
        }
    }
}

/**
 * @brief Print the entire AST tree for a compilation process
 */
void print_ast_tree(struct compile_process *process)
{
    if (!process)
    {
        printf("No compilation process provided\n");
        return;
    }

    if (!process->node_tree_vec || process->node_tree_vec->count == 0)
    {
        printf("No AST nodes to display\n");
        return;
    }

    printf("\n=== AST Tree Structure ===\n");
    
    bool is_last_initial = true;
    
    for (int i = 0; i < process->node_tree_vec->count; i++)
    {
        struct node **root_ptr = vector_at(process->node_tree_vec, i);
        if (root_ptr && *root_ptr)
        {
            printf("\nTree %d:\n", i + 1);
            print_ast_node_recursive(*root_ptr, 0, &is_last_initial);
        }
    }
    
    printf("\n=== End AST Tree ===\n\n");
}