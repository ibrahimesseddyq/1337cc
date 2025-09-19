/**
 * @file node.c
 * @brief AST node creation and utility helpers for the compiler frontend.
 *
 * This file provides functions to create AST nodes, manage the node stack/vector,
 * and query node properties (expressionability, variable/struct handling, etc).
 *
 * The implementation assumes an external `parser_current_body` and `parser_current_function`
 * are maintained while parsing so newly created nodes can record ownership.
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include <assert.h>
 
 /** Pointer to the current working node vector (stack). */
 struct vector *node_vector = NULL;
 /** Pointer to the root node vector (top-level nodes collection). */
 struct vector *node_vector_root = NULL;
 
 /** The current body node being parsed (owner for newly created nodes). */
 struct node *parser_current_body = NULL;
 /** The current function node being parsed (owner for newly created nodes). */
 struct node *parser_current_function = NULL;
 
 /**
  * @brief Set the node vectors used for push/peek/pop operations.
  *
  * @param vec      Primary node vector (acts like a stack for temporarily created nodes).
  * @param root_vec Root-level node vector (tracks top-level nodes for the compilation unit).
  */
 void node_set_vector(struct vector *vec, struct vector *root_vec)
 {
     node_vector = vec;
     node_vector_root = root_vec;
 }
 
 /**
  * @brief Push a node pointer onto the active node vector.
  *
  * @param node Pointer to the node to push.
  */
 void node_push(struct node *node)
 {
     vector_push(node_vector, &node);
 }
 
 /**
  * @brief Get the last node pointer on the stack or NULL if empty.
  *
  * @return pointer to the last node, or NULL when stack is empty.
  */
 struct node *node_peek_or_null()
 {
     return vector_back_ptr_or_null(node_vector);
 }
 
 /**
  * @brief Peek the last node on the stack and return it (must not be empty).
  *
  * @return The last node pointer.
  */
 struct node *node_peek()
 {
     return *(struct node **)(vector_back(node_vector));
 }
 
 /**
  * @brief Pop the last node from the node vector and possibly update root vector.
  *
  * Pops the last element from node_vector and, if that element equals the
  * last element of node_vector_root, also pops node_vector_root to keep them consistent.
  *
  * @return Pointer to the popped node.
  */
 struct node *node_pop()
 {
     struct node *last_node = vector_back_ptr(node_vector);
     struct node *last_node_root = vector_empty(node_vector) ? NULL : vector_back_ptr_or_null(node_vector_root);
 
     vector_pop(node_vector);
 
     if (last_node == last_node_root)
     {
         vector_pop(node_vector_root);
     }
 
     return last_node;
 }
 
 /**
  * @brief Check whether a node is "expressionable" (usable in expressions).
  *
  * Expressionable node types include expressions, parentheses expressions, unary,
  * identifier, number, and string nodes.
  *
  * @param node Node to test.
  * @return true if node is expressionable, false otherwise.
  */
 bool node_is_expressionable(struct node *node)
 {
     return node->type == NODE_TYPE_EXPRESSION ||
            node->type == NODE_TYPE_EXPRESSION_PARENTHESES ||
            node->type == NODE_TYPE_UNARY ||
            node->type == NODE_TYPE_IDENTIFIER ||
            node->type == NODE_TYPE_NUMBER ||
            node->type == NODE_TYPE_STRING;
 }
 
 /**
  * @brief Peek the last node if it is expressionable; otherwise return NULL.
  *
  * @return pointer to the last node if expressionable, or NULL.
  */
 struct node *node_peek_expressionable_or_null()
 {
     struct node *last_node = node_peek_or_null();
     return node_is_expressionable(last_node) ? last_node : NULL;
 }
 
 /**
  * @brief Create and push a CAST node.
  *
  * @param dtype        Target datatype for the cast.
  * @param operand_node Operand node being cast.
  */
 void make_cast_node(struct datatype *dtype, struct node *operand_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_CAST, .cast.dtype = *dtype, .cast.operand = operand_node});
 }
 
 /**
  * @brief Create and push a TENARY node (ternary operator result container).
  *
  * @param true_node  Node for the 'true' branch expression.
  * @param false_node Node for the 'false' branch expression.
  */
 void make_tenary_node(struct node *true_node, struct node *false_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_TENARY, .tenary.true_node = true_node, .tenary.false_node = false_node});
 }
 
 /**
  * @brief Create and push a CASE statement node.
  *
  * @param exp_node Expression node for the case label value.
  */
 void make_case_node(struct node *exp_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_CASE, .stmt._case.exp = exp_node});
 }
 
 /**
  * @brief Create and push a GOTO statement node.
  *
  * @param label_node Node representing the target label identifier.
  */
 void make_goto_node(struct node *label_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_GOTO, .stmt._goto.label = label_node});
 }
 
 /**
  * @brief Create and push a LABEL node.
  *
  * @param name_node Identifier node naming the label.
  */
 void make_label_node(struct node *name_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_LABEL, .label.name = name_node});
 }
 
 /**
  * @brief Create and push a CONTINUE statement node.
  */
 void make_continue_node()
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_CONTINUE});
 }
 
 /**
  * @brief Create and push a BREAK statement node.
  */
 void make_break_node()
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_BREAK});
 }
 
 /**
  * @brief Create and push a binary expression node.
  *
  * Asserts that both operands are non-NULL.
  *
  * @param left_node  Left operand node.
  * @param right_node Right operand node.
  * @param op         Operator string (pointer must remain valid).
  */
 void make_exp_node(struct node *left_node, struct node *right_node, const char *op)
 {
     assert(left_node);
     assert(right_node);
     node_create(&(struct node){.type = NODE_TYPE_EXPRESSION, .exp.left = left_node, .exp.right = right_node, .exp.op = op});
 }
 
 /**
  * @brief Create and push a parenthesized expression node.
  *
  * @param exp_node Inner expression node.
  */
 void make_exp_parentheses_node(struct node *exp_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_EXPRESSION_PARENTHESES, .parenthesis.exp = exp_node});
 }
 
 /**
  * @brief Create and push a bracket node (used for array dimensions / indexing).
  *
  * @param node Inner node contained by the bracket node.
  */
 void make_bracket_node(struct node *node)
 {
     node_create(&(struct node){.type = NODE_TYPE_BRACKET, .bracket.inner = node});
 }
 
 /**
  * @brief Create and push a BODY node that holds a sequence of statements and sizing metadata.
  *
  * @param body_vec        Vector of statement nodes (may be NULL for empty bodies).
  * @param size            Computed size for variables in the body.
  * @param padded          Whether padding was applied.
  * @param largest_var_node Node pointer to the largest alignment-eligible variable (may be NULL).
  */
 void make_body_node(struct vector *body_vec, size_t size, bool padded, struct node *largest_var_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_BODY, .body.statements = body_vec, .body.size = size, .body.padded = padded, .body.largest_var_node = largest_var_node});
 }
 
 /**
  * @brief Create and push a STRUCT node.
  *
  * If body_node is NULL, this is a forward declaration and the node will be flagged accordingly.
  *
  * @param name      Name of the struct (may be NULL for anonymous struct).
  * @param body_node Body node containing struct members (may be NULL for forward decl).
  */
 void make_struct_node(const char *name, struct node *body_node)
 {
     int flags = 0;
     if (!body_node)
     {
         flags |= NODE_FLAG_IS_FORWARD_DECLARATION;
     }
 
     node_create(&(struct node){.type = NODE_TYPE_STRUCT, ._struct.body_n = body_node, ._struct.name = name, .flags = flags});
 }
 
 /**
  * @brief Create and push a UNION node.
  *
  * If body_node is NULL, this is a forward declaration and the node will be flagged accordingly.
  *
  * @param name      Name of the union (may be NULL for anonymous union).
  * @param body_node Body node containing union members (may be NULL for forward decl).
  */
 void make_union_node(const char *name, struct node *body_node)
 {
     int flags = 0;
     if (!body_node)
     {
         flags |= NODE_FLAG_IS_FORWARD_DECLARATION;
     }
 
     node_create(&(struct node){.type = NODE_TYPE_UNION, ._union.body_n = body_node, ._union.name = name, .flags = flags});
 }
 
 /**
  * @brief Create and push a FUNCTION node.
  *
  * NOTE: This function sets a default `args.stack_addition` and contains a warning
  * that frame elements should still be built elsewhere.
  *
  * @param ret_type  Return datatype for the function.
  * @param name      Function name string.
  * @param arguments Vector of argument nodes (may be NULL).
  * @param body_node Function body node (may be NULL for prototype).
  */
 void make_function_node(struct datatype *ret_type, const char *name, struct vector *arguments, struct node *body_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_FUNCTION, .func.name = name, .func.args.vector = arguments, .func.body_n = body_node, .func.rtype = *ret_type, .func.args.stack_addition = DATA_SIZE_DDWORD});
 #warning "Dont forget to build the frame elements"
 }
 
 /**
  * @brief Create and push a SWITCH statement node.
  *
  * @param exp_node        Switch controlling expression node.
  * @param body_node       Body node for the switch.
  * @param cases           Vector of CASE nodes collected for this switch.
  * @param has_default_case Whether a default case exists.
  */
 void make_switch_node(struct node *exp_node, struct node *body_node, struct vector *cases, bool has_default_case)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_SWITCH, .stmt.switch_stmt.exp = exp_node, .stmt.switch_stmt.body = body_node, .stmt.switch_stmt.cases = cases, .stmt.switch_stmt.has_default_case = has_default_case});
 }
 
 /**
  * @brief Create and push a DO-WHILE statement node.
  *
  * @param body_node Body of the do-while.
  * @param exp_node  Condition expression node used by the while.
  */
 void make_do_while_node(struct node *body_node, struct node *exp_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_DO_WHILE, .stmt.do_while_stmt.body_node = body_node, .stmt.do_while_stmt.exp_node = exp_node});
 }
 
 /**
  * @brief Create and push a WHILE statement node.
  *
  * @param exp_node  Condition expression node.
  * @param body_node Loop body node.
  */
 void make_while_node(struct node *exp_node, struct node *body_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_WHILE, .stmt.while_stmt.exp_node = exp_node, .stmt.while_stmt.body_node = body_node});
 }
 
 /**
  * @brief Create and push a FOR statement node.
  *
  * @param init_node Initialization node (may be NULL).
  * @param cond_node Condition node (may be NULL).
  * @param loop_node Loop expression node (may be NULL).
  * @param body_node Loop body node.
  */
 void make_for_node(struct node *init_node, struct node *cond_node, struct node *loop_node, struct node *body_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_FOR, .stmt.for_stmt.init_node = init_node, .stmt.for_stmt.cond_node = cond_node, .stmt.for_stmt.loop_node = loop_node, .stmt.for_stmt.body_node = body_node});
 }
 
 /**
  * @brief Create and push a RETURN statement node.
  *
  * @param exp_node Expression node to return (NULL for void return).
  */
 void make_return_node(struct node *exp_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_RETURN, .stmt.return_stmt.exp = exp_node});
 }
 
 /**
  * @brief Create and push an ELSE statement node wrapper.
  *
  * @param body_node Body node for the else branch.
  */
 void make_else_node(struct node *body_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_ELSE, .stmt.else_stmt.body_node = body_node});
 }
 
 /**
  * @brief Create and push an IF statement node.
  *
  * @param cond_node Condition expression node.
  * @param body_node IF body node.
  * @param next_node Pointer to the next node (ELSE/ELSE-IF chain) or NULL.
  */
 void make_if_node(struct node *cond_node, struct node *body_node, struct node *next_node)
 {
     node_create(&(struct node){.type = NODE_TYPE_STATEMENT_IF, .stmt.if_stmt.cond_node = cond_node, .stmt.if_stmt.body_node = body_node, .stmt.if_stmt.next = next_node});
 }
 
 /**
  * @brief Convert a symbol to its node if the symbol holds a node.
  *
  * @param sym Symbol to inspect.
  * @return Pointer to the node stored in the symbol, or NULL if the symbol is not a node.
  */
 struct node *node_from_sym(struct symbol *sym)
 {
     if (sym->type != SYMBOL_TYPE_NODE)
     {
         return NULL;
     }
 
     return sym->data;
 }
 
 /**
  * @brief Lookup a symbol by name in the process and return its node if it exists.
  *
  * @param current_process Compile process (symbol table owner).
  * @param name            Symbol name to look up.
  * @return Node pointer stored in the symbol, or NULL if not found or not a node.
  */
 struct node *node_from_symbol(struct compile_process *current_process, const char *name)
 {
     struct symbol *sym = symresolver_get_symbol(current_process, name);
     if (!sym)
     {
         return NULL;
     }
     return node_from_sym(sym);
 }
 
 /**
  * @brief Retrieve a struct node by name from the process symbol table.
  *
  * @param current_process Compile process.
  * @param name            Struct name.
  * @return Pointer to struct node or NULL if not found / not a struct.
  */
 struct node *struct_node_for_name(struct compile_process *current_process, const char *name)
 {
     struct node *node = node_from_symbol(current_process, name);
     if (!node)
         return NULL;
 
     if (node->type != NODE_TYPE_STRUCT)
         return NULL;
 
     return node;
 }
 
 /**
  * @brief Retrieve a union node by name from the process symbol table.
  *
  * @param current_process Compile process.
  * @param name            Union name.
  * @return Pointer to union node or NULL if not found / not a union.
  */
 struct node *union_node_for_name(struct compile_process *current_process, const char *name)
 {
     struct node *node = node_from_symbol(current_process, name);
     if (!node)
         return NULL;
 
     if (node->type != NODE_TYPE_UNION)
         return NULL;
 
     return node;
 }
 
 /**
  * @brief Allocate, initialize, and push a copy of the provided node template.
  *
  * Copies the contents of `_node` into a freshly allocated node, sets the
  * binded owner/function from the parser current context, pushes it onto the node stack,
  * and returns the allocated node pointer.
  *
  * @param _node Pointer to a struct node template to copy from.
  * @return Pointer to the newly allocated node.
  */
 struct node *node_create(struct node *_node)
 {
     struct node *node = malloc(sizeof(struct node));
     memcpy(node, _node, sizeof(struct node));
     node->binded.owner = parser_current_body;
     node->binded.function = parser_current_function;
     node_push(node);
     return node;
 }
 
 /**
  * @brief Test whether a node (or node-like) represents a struct/union variable.
  *
  * Returns true when node is of type VARIABLE and its datatype is struct or union.
  *
  * @param node Node to test.
  * @return true if node is a struct/union variable, false otherwise.
  */
 bool node_is_struct_or_union_variable(struct node *node)
 {
     if (node->type != NODE_TYPE_VARIABLE)
     {
         return false;
     }
 
     return datatype_is_struct_or_union(&node->var.type);
 }
 
 /**
  * @brief Return the variable node for a variable, struct, or union node.
  *
  * For:
  *  - VARIABLE nodes -> returns the node itself.
  *  - STRUCT nodes -> returns the combined variable node stored in the struct node (if present).
  *  - UNION nodes  -> returns the combined variable node stored in the union node (if present).
  *
  * @param node Node which may represent a variable, struct, or union.
  * @return Pointer to the variable node or NULL if not applicable.
  */
 struct node *variable_node(struct node *node)
 {
     struct node *var_node = NULL;
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
 
 /**
  * @brief Test whether a variable node is of a primitive datatype.
  *
  * Asserts the node is a VARIABLE node, then uses datatype helper to test primitivity.
  *
  * @param node Variable node to test.
  * @return true if variable's datatype is primitive, false otherwise.
  */
 bool variable_node_is_primitive(struct node *node)
 {
     assert(node->type == NODE_TYPE_VARIABLE);
     return datatype_is_primitive(&node->var.type);
 }
 
 /**
  * @brief Return a variable node or the variable-list node itself.
  *
  * If the provided node is a VARIABLE_LIST, it is returned as-is; otherwise the
  * variable_node(...) helper is used to obtain the variable node.
  *
  * @param node Node that may be a variable or a variable list.
  * @return Variable node or variable-list node.
  */
 struct node *variable_node_or_list(struct node *node)
 {
     if (node->type == NODE_TYPE_VARIABLE_LIST)
     {
         return node;
     }
 
     return variable_node(node);
 }
 
 /**
  * @brief Return how much stack addition a function node requires for arguments.
  *
  * @param node Function node (asserted).
  * @return size in bytes reserved for argument stack addition.
  */
 size_t function_node_argument_stack_addition(struct node *node)
 {
     assert(node->type == NODE_TYPE_FUNCTION);
     return node->func.args.stack_addition;
 }
 
 /**
  * @brief Test whether a node is an expression or a parenthesized expression node.
  *
  * @param node Node to test.
  * @return true if node is a parenthesis expression or expression node.
  */
 bool node_is_expression_or_parentheses(struct node *node)
 {
     return node->type == NODE_TYPE_EXPRESSION_PARENTHESES || node->type == NODE_TYPE_EXPRESSION;
 }
 
 /**
  * @brief Test whether a node is a "value" type that can appear as an rvalue.
  *
  * Includes expressions, identifiers, numbers, unary, ternary and string nodes.
  *
  * @param node Node to test.
  * @return true if node represents a value, false otherwise.
  */
 bool node_is_value_type(struct node *node)
 {
     return node_is_expression_or_parentheses(node) ||
            node->type == NODE_TYPE_IDENTIFIER ||
            node->type == NODE_TYPE_NUMBER ||
            node->type == NODE_TYPE_UNARY ||
            node->type == NODE_TYPE_TENARY ||
            node->type == NODE_TYPE_STRING;
 }
 
 /**
  * @brief Check whether a node is an expression using the given operator.
  *
  * @param node Node to test.
  * @param op   Operator string to compare.
  * @return true if node is an expression whose op equals `op`.
  */
 bool node_is_expression(struct node *node, const char *op)
 {
     return node->type == NODE_TYPE_EXPRESSION && S_EQ(node->exp.op, op);
 }
 
 /**
  * @brief Check whether the node represents an array access/expression (op "[]").
  *
  * @param node Node to test.
  * @return true if node is an expression with operator "[]".
  */
 bool is_array_node(struct node *node)
 {
     return node_is_expression(node, "[]");
 }
 
 /**
  * @brief Test whether a node is an assignment expression (including compound assignments).
  *
  * Supported assignment operators: =, +=, -=, /=, *=
  *
  * @param node Node to test.
  * @return true if node is an assignment expression, false otherwise.
  */
 bool is_node_assignment(struct node *node)
 {
     if (node->type != NODE_TYPE_EXPRESSION)
         return false;
 
     return S_EQ(node->exp.op, "=") ||
            S_EQ(node->exp.op, "+=") ||
            S_EQ(node->exp.op, "-=") ||
            S_EQ(node->exp.op, "/=") ||
            S_EQ(node->exp.op, "*=");
 }
 