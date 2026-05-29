#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Node stack
//
// The node stack is a global LIFO stack implemented as a vector (node_vector).
// Whenever node_create() is called it pushes the freshly allocated node onto
// this stack.  The parser calls node_pop() to retrieve completed sub-trees and
// assemble them into composite nodes.  node_vector_root is a parallel vector
// that tracks which nodes are "root-level" (i.e. directly children of the
// translation unit); a node is mirrored into node_vector_root automatically by
// node_pop() when it detects the popped node matches the current root entry.
// ============================================================================

struct vector *node_vector      = nullptr;
struct vector *node_vector_root = nullptr;

struct node *parser_current_body     = nullptr;
struct node *parser_current_function = nullptr;

// Sets the two node vectors used by all subsequent node stack operations.
// vec      - the primary node stack; node_create() pushes here and node_pop()
//            pulls from here.
// root_vec - the root-level node vector; used to track top-level declarations
//            that will later feed the code generator's node_tree_vec.
void node_set_vector(struct vector *vec, struct vector *root_vec)
{
    node_vector      = vec;
    node_vector_root = root_vec;
}

// Pushes a node pointer onto the primary node stack (node_vector).
// node - the node to push; only the pointer is stored, not a copy of the node.
// Side effect: modifies node_vector.
void node_push(struct node *node)
{
    vector_push(node_vector, &node);
}

// Returns the node at the top of the primary node stack without removing it,
// or nullptr if the stack is empty.
struct node *node_peek_or_null()
{
    return vector_back_ptr_or_null_typed<struct node>(node_vector);
}

// Returns the node at the top of the primary node stack without removing it.
// The stack must not be empty when this is called; behaviour is undefined
// (likely a crash) if it is.
struct node *node_peek()
{
    return *vector_back_typed<struct node *>(node_vector);
}

// Removes and returns the node at the top of the primary node stack.
// If the popped node is also the current back of node_vector_root, it is
// removed from node_vector_root as well, keeping the two vectors in sync.
// Returns a pointer to the removed node. The node's memory is not freed.
struct node *node_pop()
{
    struct node *last_node      = vector_back_ptr_typed<struct node>(node_vector);
    struct node *last_node_root =
        vector_empty(node_vector) ? nullptr
                                  : vector_back_ptr_or_null_typed<struct node>(node_vector_root);

    vector_pop(node_vector);

    if (last_node == last_node_root)
        vector_pop(node_vector_root);

    return last_node;
}

// Returns true if node can appear as an operand inside a larger expression,
// i.e. if it is one of: expression, parenthesised expression, unary expression,
// identifier, number literal, or string literal.
// node - the node to test; must not be nullptr.
bool node_is_expressionable(struct node *node)
{
    return node->type == NODE_TYPE_EXPRESSION           ||
           node->type == NODE_TYPE_EXPRESSION_PARENTHESES ||
           node->type == NODE_TYPE_UNARY               ||
           node->type == NODE_TYPE_IDENTIFIER          ||
           node->type == NODE_TYPE_NUMBER               ||
           node->type == NODE_TYPE_STRING;
}

// Returns the top-of-stack node if it is expressionable (see node_is_expressionable),
// or nullptr if the stack is empty or the top node is not expressionable.
struct node *node_peek_expressionable_or_null()
{
    struct node *last_node = node_peek_or_null();
    return node_is_expressionable(last_node) ? last_node : nullptr;
}

// ============================================================================
// Node factory helpers — replace C99 compound literals
// ============================================================================

// Allocates a new struct node on the heap, copies the initialised fields from
// _node into it, binds the node to the current parser body and function, then
// pushes the new node onto the node stack.
// _node   - a stack-local node whose fields have been set by a make_*_node
//           helper. The entire struct is shallow-copied into the heap allocation.
// Returns a pointer to the newly allocated, stack-pushed node.
// Side effect: calls node_push(), which modifies node_vector.
struct node *node_create(struct node *_node)
{
    struct node *node = static_cast<struct node *>(malloc(sizeof(struct node)));
    memcpy(node, _node, sizeof(struct node));
    node->binded.owner    = parser_current_body;
    node->binded.function = parser_current_function;
    node_push(node);
    return node;
}

// Creates a type-cast expression node and pushes it onto the node stack.
// dtype        - the target data type to cast to; copied by value into the node.
// operand_node - the expression being cast; must already exist on the heap.
// Pushes a NODE_TYPE_CAST node.
void make_cast_node(struct datatype *dtype, struct node *operand_node)
{
    struct node n{};
    n.type = NODE_TYPE_CAST;
    n.cast.dtype = *dtype;
    n.cast.operand = operand_node;
    node_create(&n);
}

// Creates a ternary conditional expression node (cond ? true_node : false_node)
// and pushes it onto the node stack.
// true_node  - the expression evaluated when the condition is non-zero.
// false_node - the expression evaluated when the condition is zero.
// Pushes a NODE_TYPE_TENARY node.
void make_tenary_node(struct node *true_node, struct node *false_node)
{
    struct node n{};
    n.type = NODE_TYPE_TENARY;
    n.tenary.true_node  = true_node;
    n.tenary.false_node = false_node;
    node_create(&n);
}

// Creates a switch-case label node and pushes it onto the node stack.
// exp_node - the constant expression that labels this case arm.
// Pushes a NODE_TYPE_STATEMENT_CASE node.
void make_case_node(struct node *exp_node)
{
    struct node n{};
    n.type = NODE_TYPE_STATEMENT_CASE;
    n.stmt._case.exp = exp_node;
    node_create(&n);
}

// Creates a goto statement node and pushes it onto the node stack.
// label_node - the identifier node naming the target label.
// Pushes a NODE_TYPE_STATEMENT_GOTO node.
void make_goto_node(struct node *label_node)
{
    struct node n{};
    n.type = NODE_TYPE_STATEMENT_GOTO;
    n.stmt._goto.label = label_node;
    node_create(&n);
}

// Creates a label definition node and pushes it onto the node stack.
// name_node - the identifier node containing the label's name.
// Pushes a NODE_TYPE_LABEL node.
void make_label_node(struct node *name_node)
{
    struct node n{};
    n.type = NODE_TYPE_LABEL;
    n.label.name = name_node;
    node_create(&n);
}

// Creates a continue statement node and pushes it onto the node stack.
// Pushes a NODE_TYPE_STATEMENT_CONTINUE node. Takes no arguments.
void make_continue_node()
{
    struct node n{};
    n.type = NODE_TYPE_STATEMENT_CONTINUE;
    node_create(&n);
}

// Creates a break statement node and pushes it onto the node stack.
// Pushes a NODE_TYPE_STATEMENT_BREAK node. Takes no arguments.
void make_break_node()
{
    struct node n{};
    n.type = NODE_TYPE_STATEMENT_BREAK;
    node_create(&n);
}

// Creates a binary expression node (left_node OP right_node) and pushes it
// onto the node stack.
// left_node  - the left-hand operand; must already exist and must not be nullptr.
// right_node - the right-hand operand; must already exist and must not be nullptr.
// op         - the operator string (e.g. "+", "==", "[]"). The pointer must
//              remain valid for the lifetime of the node; it is stored by
//              reference, not copied.
// Pushes a NODE_TYPE_EXPRESSION node.
void make_exp_node(struct node *left_node, struct node *right_node, const char *op)
{
    assert(left_node);
    assert(right_node);
    struct node n{};
    n.type     = NODE_TYPE_EXPRESSION;
    n.exp.left  = left_node;
    n.exp.right = right_node;
    n.exp.op    = op;
    node_create(&n);
}

// Creates a parenthesised expression node wrapping exp_node and pushes it onto
// the node stack.
// exp_node - the inner expression that was enclosed in parentheses.
// Pushes a NODE_TYPE_EXPRESSION_PARENTHESES node.
void make_exp_parentheses_node(struct node *exp_node)
{
    struct node n{};
    n.type             = NODE_TYPE_EXPRESSION_PARENTHESES;
    n.parenthesis.exp  = exp_node;
    node_create(&n);
}

// Creates a bracket node (e.g. an array subscript or initialiser list element)
// holding inner as its content, and pushes it onto the node stack.
// inner - the expression node inside the brackets.
// Pushes a NODE_TYPE_BRACKET node.
void make_bracket_node(struct node *inner)
{
    struct node n{};
    n.type         = NODE_TYPE_BRACKET;
    n.bracket.inner = inner;
    node_create(&n);
}

// Creates a compound-statement (block) body node and pushes it onto the node stack.
// body_vec          - vector of statement nodes that form the body.
// size              - total stack space (in bytes) required by all locals in the body.
// padded            - true if size has been rounded up for alignment purposes.
// largest_var_node  - pointer to the variable node with the largest size in this
//                     body, used by the code generator for stack frame planning.
//                     May be nullptr if the body has no locals.
// Pushes a NODE_TYPE_BODY node.
void make_body_node(struct vector *body_vec, size_t size, bool padded, struct node *largest_var_node)
{
    struct node n{};
    n.type              = NODE_TYPE_BODY;
    n.body.statements   = body_vec;
    n.body.size         = size;
    n.body.padded       = padded;
    n.body.largest_var_node = largest_var_node;
    node_create(&n);
}

// Creates a struct declaration node and pushes it onto the node stack.
// If body_node is nullptr the struct is a forward declaration and
// NODE_FLAG_IS_FORWARD_DECLARATION is set on the node's flags.
// name      - the struct tag name; stored by reference, must remain valid.
// body_node - the body node produced by make_body_node(), or nullptr for a
//             forward declaration.
// Pushes a NODE_TYPE_STRUCT node.
void make_struct_node(const char *name, struct node *body_node)
{
    int flags = 0;
    if (!body_node)
        flags |= NODE_FLAG_IS_FORWARD_DECLARATION;
    struct node n{};
    n.type          = NODE_TYPE_STRUCT;
    n.flags         = flags;
    n._struct.body_n = body_node;
    n._struct.name   = name;
    node_create(&n);
}

// Creates a union declaration node and pushes it onto the node stack.
// If body_node is nullptr the union is a forward declaration and
// NODE_FLAG_IS_FORWARD_DECLARATION is set on the node's flags.
// name      - the union tag name; stored by reference, must remain valid.
// body_node - the body node produced by make_body_node(), or nullptr for a
//             forward declaration.
// Pushes a NODE_TYPE_UNION node.
void make_union_node(const char *name, struct node *body_node)
{
    int flags = 0;
    if (!body_node)
        flags |= NODE_FLAG_IS_FORWARD_DECLARATION;
    struct node n{};
    n.type         = NODE_TYPE_UNION;
    n.flags        = flags;
    n._union.body_n = body_node;
    n._union.name   = name;
    node_create(&n);
}

// Creates a function definition node and pushes it onto the node stack.
// ret_type  - the return type of the function; copied by value into the node.
// name      - the function's identifier string; stored by reference, must remain valid.
// arguments - vector of NODE_TYPE_VARIABLE nodes representing the parameters.
// body_node - the body node for the function, or nullptr for a prototype.
// Note: func.args.stack_addition is initialised to DATA_SIZE_DDWORD (the size of
//       the saved return address on the call stack). Frame element construction
//       is not yet complete (see #warning in source).
// Pushes a NODE_TYPE_FUNCTION node.
void make_function_node(struct datatype *ret_type, const char *name,
                        struct vector *arguments, struct node *body_node)
{
    struct node n{};
    n.type              = NODE_TYPE_FUNCTION;
    n.func.name         = name;
    n.func.args.vector  = arguments;
    n.func.body_n       = body_node;
    n.func.rtype        = *ret_type;
    n.func.args.stack_addition = DATA_SIZE_DDWORD;
    node_create(&n);
#warning "Don't forget to build the frame elements"
}

// Creates a switch statement node and pushes it onto the node stack.
// exp_node         - the controlling expression of the switch.
// body_node        - the body (block) of the switch, containing case/default labels.
// cases            - vector of NODE_TYPE_STATEMENT_CASE nodes collected from the body.
// has_default_case - true if the switch body contains a default: label.
// Pushes a NODE_TYPE_STATEMENT_SWITCH node.
void make_switch_node(struct node *exp_node, struct node *body_node,
                      struct vector *cases, bool has_default_case)
{
    struct node n{};
    n.type                          = NODE_TYPE_STATEMENT_SWITCH;
    n.stmt.switch_stmt.exp          = exp_node;
    n.stmt.switch_stmt.body         = body_node;
    n.stmt.switch_stmt.cases        = cases;
    n.stmt.switch_stmt.has_default_case = has_default_case;
    node_create(&n);
}

// Creates a do-while loop node and pushes it onto the node stack.
// body_node - the loop body executed before the condition is tested.
// exp_node  - the condition expression; loop continues while this is non-zero.
// Pushes a NODE_TYPE_STATEMENT_DO_WHILE node.
void make_do_while_node(struct node *body_node, struct node *exp_node)
{
    struct node n{};
    n.type                      = NODE_TYPE_STATEMENT_DO_WHILE;
    n.stmt.do_while_stmt.body_node = body_node;
    n.stmt.do_while_stmt.exp_node  = exp_node;
    node_create(&n);
}

// Creates a while loop node and pushes it onto the node stack.
// exp_node  - the loop condition; the body is entered only while this is non-zero.
// body_node - the loop body.
// Pushes a NODE_TYPE_STATEMENT_WHILE node.
void make_while_node(struct node *exp_node, struct node *body_node)
{
    struct node n{};
    n.type                   = NODE_TYPE_STATEMENT_WHILE;
    n.stmt.while_stmt.exp_node  = exp_node;
    n.stmt.while_stmt.body_node = body_node;
    node_create(&n);
}

// Creates a for loop node and pushes it onto the node stack.
// init_node - the initialisation expression/declaration (the first clause); may be nullptr.
// cond_node - the loop condition expression (the second clause); may be nullptr.
// loop_node - the post-iteration expression (the third clause); may be nullptr.
// body_node - the loop body.
// Pushes a NODE_TYPE_STATEMENT_FOR node.
void make_for_node(struct node *init_node, struct node *cond_node,
                   struct node *loop_node, struct node *body_node)
{
    struct node n{};
    n.type                   = NODE_TYPE_STATEMENT_FOR;
    n.stmt.for_stmt.init_node = init_node;
    n.stmt.for_stmt.cond_node = cond_node;
    n.stmt.for_stmt.loop_node = loop_node;
    n.stmt.for_stmt.body_node = body_node;
    node_create(&n);
}

// Creates a return statement node and pushes it onto the node stack.
// exp_node - the expression whose value is returned, or nullptr for a bare
//            "return;" with no value.
// Pushes a NODE_TYPE_STATEMENT_RETURN node.
void make_return_node(struct node *exp_node)
{
    struct node n{};
    n.type               = NODE_TYPE_STATEMENT_RETURN;
    n.stmt.return_stmt.exp = exp_node;
    node_create(&n);
}

// Creates an else clause node and pushes it onto the node stack.
// body_node - the body (block) of the else branch.
// Pushes a NODE_TYPE_STATEMENT_ELSE node.
void make_else_node(struct node *body_node)
{
    struct node n{};
    n.type                   = NODE_TYPE_STATEMENT_ELSE;
    n.stmt.else_stmt.body_node = body_node;
    node_create(&n);
}

// Creates an if statement node and pushes it onto the node stack.
// cond_node - the condition expression; the then-branch is taken when non-zero.
// body_node - the then-branch body.
// next_node - the else or else-if node chained after this if, or nullptr if there
//             is no else branch.
// Pushes a NODE_TYPE_STATEMENT_IF node.
void make_if_node(struct node *cond_node, struct node *body_node, struct node *next_node)
{
    struct node n{};
    n.type                 = NODE_TYPE_STATEMENT_IF;
    n.stmt.if_stmt.cond_node = cond_node;
    n.stmt.if_stmt.body_node = body_node;
    n.stmt.if_stmt.next      = next_node;
    node_create(&n);
}

// ============================================================================
// Symbol / lookup utilities
// ============================================================================

// Retrieves the AST node associated with a symbol, if the symbol wraps a node.
// sym  - the symbol to unwrap; must not be nullptr.
// Returns a pointer to the struct node stored in sym->data, or nullptr if the
// symbol's type is not SYMBOL_TYPE_NODE.
struct node *node_from_sym(struct symbol *sym)
{
    if (sym->type != SYMBOL_TYPE_NODE)
        return nullptr;
    return static_cast<struct node *>(sym->data);
}

// Looks up a name in the compile process's symbol table and returns the AST node
// it refers to, if any.
// current_process - the active compile process whose symbol resolver is used.
// name            - the identifier string to look up.
// Returns the associated struct node, or nullptr if the name is not found or
// its symbol is not of SYMBOL_TYPE_NODE.
struct node *node_from_symbol(struct compile_process *current_process, const char *name)
{
    struct symbol *sym = symresolver_get_symbol(current_process, name);
    if (!sym)
        return nullptr;
    return node_from_sym(sym);
}

// Looks up a struct tag name and returns its declaration node.
// current_process - the active compile process whose symbol table is searched.
// name            - the struct tag name to look up.
// Returns the NODE_TYPE_STRUCT node for the given tag, or nullptr if the name
// is not found or resolves to a non-struct node.
struct node *struct_node_for_name(struct compile_process *current_process, const char *name)
{
    struct node *node = node_from_symbol(current_process, name);
    if (!node)
        return nullptr;
    if (node->type != NODE_TYPE_STRUCT)
        return nullptr;
    return node;
}

// Looks up a union tag name and returns its declaration node.
// current_process - the active compile process whose symbol table is searched.
// name            - the union tag name to look up.
// Returns the NODE_TYPE_UNION node for the given tag, or nullptr if the name
// is not found or resolves to a non-union node.
struct node *union_node_for_name(struct compile_process *current_process, const char *name)
{
    struct node *node = node_from_symbol(current_process, name);
    if (!node)
        return nullptr;
    if (node->type != NODE_TYPE_UNION)
        return nullptr;
    return node;
}

// ============================================================================
// Node type predicates
// ============================================================================

// Returns true if node is a variable declaration whose type is a struct or union.
// node - a NODE_TYPE_VARIABLE node to test.
bool node_is_struct_or_union_variable(struct node *node)
{
    if (node->type != NODE_TYPE_VARIABLE)
        return false;
    return datatype_is_struct_or_union(&node->var.type);
}

// Returns the variable node embedded in or directly represented by node.
// Handles three cases:
//   NODE_TYPE_VARIABLE - returns node itself.
//   NODE_TYPE_STRUCT   - returns the struct's associated variable node (_struct.var).
//   NODE_TYPE_UNION    - returns the union's associated variable node (_union.var).
// Returns nullptr if none of the above cases match.
struct node *variable_node(struct node *node)
{
    struct node *var_node = nullptr;
    switch (node->type)
    {
    case NODE_TYPE_VARIABLE:
        var_node = node;
        break;
    case NODE_TYPE_STRUCT:
        var_node = node->_struct.var;
        break;
    case NODE_TYPE_UNION:
        var_node = node->_union.var;
        break;
    }
    return var_node;
}

// Returns true if the variable described by node has a primitive data type
// (i.e. not a struct, union, or pointer aggregate).
// node - must be a NODE_TYPE_VARIABLE node; asserts in debug builds if it is not.
bool variable_node_is_primitive(struct node *node)
{
    assert(node->type == NODE_TYPE_VARIABLE);
    return datatype_is_primitive(&node->var.type);
}

// Returns node itself if it is a variable list (NODE_TYPE_VARIABLE_LIST),
// otherwise extracts and returns the underlying variable node via variable_node().
// Useful when code must handle both "int a, b;" (list) and "int a;" (single)
// uniformly.
// node - a NODE_TYPE_VARIABLE or NODE_TYPE_VARIABLE_LIST node.
struct node *variable_node_or_list(struct node *node)
{
    if (node->type == NODE_TYPE_VARIABLE_LIST)
        return node;
    return variable_node(node);
}

// Returns the per-argument stack addition value for a function node.
// This is the number of bytes that each argument occupies beyond the base
// calling-convention overhead (initialised to DATA_SIZE_DDWORD by
// make_function_node()).
// node - must be a NODE_TYPE_FUNCTION node; asserts in debug builds otherwise.
size_t function_node_argument_stack_addition(struct node *node)
{
    assert(node->type == NODE_TYPE_FUNCTION);
    return node->func.args.stack_addition;
}

// Returns true if node is either a plain expression or a parenthesised expression.
// node - the node to test; must not be nullptr.
bool node_is_expression_or_parentheses(struct node *node)
{
    return node->type == NODE_TYPE_EXPRESSION_PARENTHESES ||
           node->type == NODE_TYPE_EXPRESSION;
}

// Returns true if node represents a value-producing construct, i.e. any of:
// expression, parenthesised expression, identifier, number literal, unary
// expression, ternary expression, or string literal.
// node - the node to test; must not be nullptr.
bool node_is_value_type(struct node *node)
{
    return node_is_expression_or_parentheses(node) ||
           node->type == NODE_TYPE_IDENTIFIER ||
           node->type == NODE_TYPE_NUMBER     ||
           node->type == NODE_TYPE_UNARY      ||
           node->type == NODE_TYPE_TENARY     ||
           node->type == NODE_TYPE_STRING;
}

// Returns true if node is an expression node whose operator matches op exactly.
// node - the node to test; must not be nullptr.
// op   - the operator string to match against node->exp.op (e.g. "+" or "==").
bool node_is_expression(struct node *node, const char *op)
{
    return node->type == NODE_TYPE_EXPRESSION && S_EQ(node->exp.op, op);
}

// Returns true if node is an array-subscript expression, i.e. an expression
// node with the "[]" operator.
// node - the node to test; must not be nullptr.
bool is_array_node(struct node *node)
{
    return node_is_expression(node, "[]");
}

// Returns true if node is an assignment expression, i.e. an expression node
// whose operator is one of: =, +=, -=, /=, *=.
// node - the node to test; must not be nullptr.
bool is_node_assignment(struct node *node)
{
    if (node->type != NODE_TYPE_EXPRESSION)
        return false;
    return S_EQ(node->exp.op, "=")  || S_EQ(node->exp.op, "+=") ||
           S_EQ(node->exp.op, "-=") || S_EQ(node->exp.op, "/=") ||
           S_EQ(node->exp.op, "*=");
}
