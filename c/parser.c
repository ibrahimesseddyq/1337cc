/**
 * @file parser.c
 * @brief Parser utilities and helpers for the compiler frontend.
 *
 * This file contains parser state, helper functions for token reading,
 * parsing of datatypes and expressions, and simple scope/history helpers.
 * Documentation is provided in Doxygen-style comments to make the
 * behavior of each API clear for maintainers and documentation generation.
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include <assert.h>
 
 /* Current compile process (module-global parser state) */
 static struct compile_process *current_process;
 /* Fixup system used by the parser (module-global) */
 static struct fixup_system *parser_fixup_sys;
 /* Last token consumed/peeked by the parser */
 static struct token *parser_last_token;
 
 extern struct node *parser_current_body;
 extern struct node *parser_current_function;
 
 /* Blank node used as a placeholder for some expression parsing paths */
 // NODE_TYPE_BLANK
 struct node *parser_blank_node;
 
 extern struct expressionable_op_precedence_group op_precedence[TOTAL_OPERATOR_GROUPS];
 
 /**
  * @defgroup Parser scope entity flags
  * Flags used in parser_scope_entity.flags
  * @{ */
 enum
 {
     PARSER_SCOPE_ENTITY_ON_STACK = 0b00000001,
     PARSER_SCOPE_ENTITY_STRUCTURE_SCOPE = 0b00000010,
 };
 /** @} */
 
 /**
  * @brief Represents a scope entity (one declared variable or entity in scope)
  */
 struct parser_scope_entity
 {
     /** The entity flags (bitfield) */
     int flags;
 
     /** The stack offset for this entity (if placed on the call stack) */
     int stack_offset;
 
     /** The AST node that declared this entity */
     struct node *node;
 };
 
 /**
  * @brief Allocate and initialize a new parser scope entity.
  *
  * @param node Pointer to the AST node representing the entity.
  * @param stack_offset Stack offset for the entity (if any).
  * @param flags Bitfield of PARSER_SCOPE_ENTITY_* flags.
  * @return Pointer to newly allocated parser_scope_entity (caller owns it).
  */
 struct parser_scope_entity *parser_new_scope_entity(struct node *node, int stack_offset, int flags)
 {
     struct parser_scope_entity *entity = calloc(1, sizeof(struct parser_scope_entity));
     entity->node = node;
     entity->flags = flags;
     entity->stack_offset = stack_offset;
     return entity;
 }
 
 /**
  * @brief Return the last scope entity but stop at the global scope.
  *
  * This is a convenience wrapper around scope_last_entity_stop_at using the
  * current process and root scope.
  */
 struct parser_scope_entity *parser_scope_last_entity_stop_global_scope()
 {
     return scope_last_entity_stop_at(current_process, current_process->scope.root);
 }
 
 /**
  * @defgroup History flags
  * Flags for parser history contexts (tracking nested parsing state).
  * @{ */
 enum
 {
     HISTORY_FLAG_INSIDE_UNION = 0b00000001,
     HISTORY_FLAG_IS_UPWARD_STACK = 0b00000010,
     HISTORY_FLAG_IS_GLOBAL_SCOPE = 0b00000100,
     HISTORY_FLAG_INSIDE_STRUCTURE = 0b00001000,
     HISTORY_FLAG_INSIDE_FUNCTION_BODY = 0b00010000,
     HISTORY_FLAG_IN_SWITCH_STATEMENT = 0b00100000,
     HISTORY_FLAG_PARENTHESES_IS_NOT_A_FUNCTION_CALL = 0b01000000,
 
 };
 /** @} */
 
 /**
  * @brief Parsed switch-case bookkeeping.
  */
 struct history_cases
 {
     /** Vector of parsed_switch_case entries */
     struct vector *cases;
     /** Whether a default case was seen in that switch */
     bool has_default_case;
 };
 
 /**
  * @brief A compact history/context structure passed during parsing.
  *
  * It holds flags describing the current parsing environment and a small
  * switch-specific sub-structure used while parsing switch statements.
  */
 struct history
 {
     int flags;
     struct parser_history_switch
     {
         struct history_cases case_data;
     } _switch;
 };
 
 /**
  * @brief Create a new history object with given flags.
  *
  * @param flags Bitfield of HISTORY_FLAG_*
  * @return newly allocated history (caller owns it)
  */
 struct history *history_begin(int flags)
 {
     struct history *history = calloc(1, sizeof(struct history));
     history->flags = flags;
     return history;
 }
 
 /**
  * @brief Duplicate a history and change flags for nested parsing contexts.
  *
  * This copies the caller-provided history into a newly allocated structure,
  * then overwrites flags with the provided int. It is used to descend into a
  * nested expression or scope while keeping other history fields intact.
  *
  * @param history Pointer to the current history to copy.
  * @param flags New flags to set on the returned history.
  * @return newly allocated history (caller owns it)
  */
 struct history *history_down(struct history *history, int flags)
 {
     struct history *new_history = calloc(1, sizeof(struct history));
     memcpy(new_history, history, sizeof(struct history));
     new_history->flags = flags;
     return new_history;
 }
 
 /**
  * @brief Initialize switch-statement parsing state on the provided history.
  *
  * Allocates and initializes the vector used to remember case indices and
  * marks the history as inside a switch statement.
  *
  * @param history Pointer to history to initialize for switch parsing
  * @return parser_history_switch (a copy of the internal structure)
  */
 struct parser_history_switch parser_new_switch_statement(struct history *history)
 {
     memset(&history->_switch, 0, sizeof(&history->_switch));
     history->_switch.case_data.cases = vector_create(sizeof(struct parsed_switch_case));
     history->flags |= HISTORY_FLAG_IN_SWITCH_STATEMENT;
     return history->_switch;
 }
 
 /**
  * @brief Finalize switch statement parsing. (Placeholder)
  *
  * Currently this is a no-op and exists for symmetry with parser_new_switch_statement.
  */
 void parser_end_switch_statement(struct parser_history_switch *switch_history)
 {
     // Do nothing.
 }
 
 /**
  * @brief Register a parsed case node in the switch history.
  *
  * The function asserts that the provided history is currently inside a
  * switch statement and pushes a parsed_switch_case entry recording the
  * case's constant index.
  */
 void parser_register_case(struct history *history, struct node *case_node)
 {
     assert(history->flags & HISTORY_FLAG_IN_SWITCH_STATEMENT);
     struct parsed_switch_case scase;
     scase.index = case_node->stmt._case.exp->llnum;
     vector_push(history->_switch.case_data.cases, &scase);
 }
 
 /* Forward declarations for parsing entry points used throughout the file. */
 int parse_expressionable_single(struct history *history);
 void parse_expressionable(struct history *history);
 void parse_body(size_t *variable_size, struct history *history);
 void parse_keyword(struct history *history);
 struct vector *parse_function_arguments(struct history *history);
 void parse_expressionable_root(struct history *history);
 void parse_label(struct history *history);
 void parse_for_tenary(struct history *history);
 void parse_datatype(struct datatype *dtype);
 void parse_for_cast();
 
 /**
  * @brief Create a new scope in the current process.
  *
  * Wrapper around scope_new that uses the global current_process pointer.
  */
 void parser_scope_new()
 {
     scope_new(current_process, 0);
 }
 
 /**
  * @brief Finish current scope in the current process.
  *
  * Wrapper around scope_finish that uses the global current_process pointer.
  */
 void parser_scope_finish()
 {
     scope_finish(current_process);
 }
 
 /**
  * @brief Get the last pushed parser scope entity for the current process.
  *
  * @return pointer to parser_scope_entity or NULL if none
  */
 struct parser_scope_entity *parser_scope_last_entity()
 {
     return scope_last_entity(current_process);
 }
 
 /**
  * @brief Push a parser scope entity into the current process scope stack.
  *
  * @param entity Entity to push (caller owns the memory).
  * @param size Additional size to reserve/track for this entity.
  */
 void parser_scope_push(struct parser_scope_entity *entity, size_t size)
 {
     scope_push(current_process, entity, size);
 }
 
 /**
  * @brief Consume and ignore newline or comment tokens from the token vector.
  *
  * This function peeks tokens and, while they are newline/comment separators,
  * advances the token vector position to skip over them.
  *
  * @param token The token to start examining (often the peeked next token)
  */
 static void parser_ignore_nl_or_comment(struct token *token)
 {
     while (token && token_is_nl_or_comment_or_newline_seperator(token))
     {
         // Skip the token
         vector_peek(current_process->token_vec);
         token = vector_peek_no_increment(current_process->token_vec);
     }
 }
 
 /**
  * @brief Return and advance to the next non-ignored token.
  *
  * This function will skip comments/newlines and update current_process->pos
  * to the token's position. It also updates the module-global parser_last_token.
  *
  * @return The next token (after skipping) or NULL if end of input.
  */
 static struct token *token_next()
 {
     struct token *next_token = vector_peek_no_increment(current_process->token_vec);
     parser_ignore_nl_or_comment(next_token);
     if (next_token)
     {
         current_process->pos = next_token->pos;
     }
     parser_last_token = next_token;
     return vector_peek(current_process->token_vec);
 }
 
 /**
  * @brief Peek the next non-ignored token without advancing.
  *
  * @return Next token (after skipping comments/newlines) or NULL.
  */
 static struct token *token_peek_next()
 {
     struct token *next_token = vector_peek_no_increment(current_process->token_vec);
     parser_ignore_nl_or_comment(next_token);
     return vector_peek_no_increment(current_process->token_vec);
 }
 
 /** Helper predicates to inspect the next token without consuming it. */
 static bool token_next_is_operator(const char *op)
 {
     struct token *token = token_peek_next();
     return token_is_operator(token, op);
 }
 
 static bool token_next_is_keyword(const char *keyword)
 {
     struct token *token = token_peek_next();
     return token_is_keyword(token, keyword);
 }
 
 static bool token_next_is_symbol(char c)
 {
     struct token *token = token_peek_next();
     return token_is_symbol(token, c);
 }
 
 /**
  * @brief Expect the next token to be a specific symbol and advance.
  *
  * If the expectation fails, compiler_error() is invoked.
  */
 static void expect_sym(char c)
 {
     struct token *next_token = token_next();
     if (!next_token || next_token->type != TOKEN_TYPE_SYMBOL || next_token->cval != c)
     {
         compiler_error(current_process, "Expecting symbol %c however something else was provided\n", c);
     }
 }
 
 /**
  * @brief Expect the next token to be a specific operator and advance.
  */
 static void expect_op(const char *op)
 {
     struct token *next_token = token_next();
     if (!next_token || next_token->type != TOKEN_TYPE_OPERATOR || !S_EQ(next_token->sval, op))
     {
         compiler_error(current_process, "Expecting the operator %s but something else was provided\n", next_token->sval);
     }
 }
 
 /**
  * @brief Expect the next token to be a specific keyword and advance.
  */
 static void expect_keyword(const char *keyword)
 {
     struct token *next_token = token_next();
     if (!next_token || next_token->type != TOKEN_TYPE_KEYWORD || !S_EQ(next_token->sval, keyword))
     {
         compiler_error(current_process, "Expecting the keyword %s but something was provided\n", keyword);
     }
 }
 
 /**
  * @brief Convert a single token (number/identifier/string) into a node and push it.
  *
  * On error this will call compiler_error().
  */
 void parse_single_token_to_node()
 {
     struct token *token = token_next();
     struct node *node = NULL;
     switch (token->type)
     {
     case TOKEN_TYPE_NUMBER:
         node = node_create(&(struct node){.type = NODE_TYPE_NUMBER, .llnum = token->llnum});
         break;
     case TOKEN_TYPE_IDENTIFIER:
         node = node_create(&(struct node){.type = NODE_TYPE_IDENTIFIER, .sval = token->sval});
         break;
 
     case TOKEN_TYPE_STRING:
         node = node_create(&(struct node){.type = NODE_TYPE_STRING, .sval = token->sval});
         break;
 
     default:
         compiler_error(current_process, "This is not a single token that can be converted to a node");
     }
 }
 
 /**
  * @brief Handle an operator by parsing the right-hand expression.
  *
  * This delegates to parse_expressionable() after consuming the operator.
  */
 void parse_expressionable_for_op(struct history *history, const char *op)
 {
     parse_expressionable(history);
 }
 
 /**
  * @brief Find precedence group index for operator and returns group pointer.
  *
  * @param op Operator string (e.g. "+")
  * @param group_out Out-parameter set to the group pointer on success.
  * @return group index or -1 if operator not found.
  */
 static int parser_get_precedence_for_operator(const char *op, struct expressionable_op_precedence_group **group_out)
 {
     *group_out = NULL;
     for (int i = 0; i < TOTAL_OPERATOR_GROUPS; i++)
     {
         for (int b = 0; op_precedence[i].operators[b]; b++)
         {
             const char *_op = op_precedence[i].operators[b];
             if (S_EQ(op, _op))
             {
                 *group_out = &op_precedence[i];
                 return i;
             }
         }
     }
 
     return -1;
 }
 
 /**
  * @brief Determine whether the left operator should be evaluated before the right.
  *
  * This uses operator precedence and associativity tables to decide how to
  * reorder expression nodes.
  */
 static bool parser_left_op_has_priority(const char *op_left, const char *op_right)
 {
     struct expressionable_op_precedence_group *group_left = NULL;
     struct expressionable_op_precedence_group *group_right = NULL;
 
     if (S_EQ(op_left, op_right))
     {
         return false;
     }
 
     int precdence_left = parser_get_precedence_for_operator(op_left, &group_left);
     int precdence_right = parser_get_precedence_for_operator(op_right, &group_right);
     if (group_left->associtivity == ASSOCIATIVITY_RIGHT_TO_LEFT)
     {
         return false;
     }
 
     return precdence_left <= precdence_right;
 }
 
 /**
  * @brief Rewrites expression nodes to move the right child's left-hand side up.
  *
  * This implements part of the transformation that enforces operator precedence
  * by rotating nodes.
  */
 void parser_node_shift_children_left(struct node *node)
 {
     assert(node->type == NODE_TYPE_EXPRESSION);
     assert(node->exp.right->type == NODE_TYPE_EXPRESSION);
 
     const char *right_op = node->exp.right->exp.op;
     struct node *new_exp_left_node = node->exp.left;
     struct node *new_exp_right_node = node->exp.right->exp.left;
     make_exp_node(new_exp_left_node, new_exp_right_node, node->exp.op);
 
     // (50*20)
     struct node *new_left_operand = node_pop();
     // 120
     struct node *new_right_operand = node->exp.right->exp.right;
     node->exp.left = new_left_operand;
     node->exp.right = new_right_operand;
     node->exp.op = right_op;
 }
 
 /**
  * @brief Rotate a nested right-left expression into a left-associative form.
  *
  * This helper is used during precedence reordering when a right expression
  * contains a nested expression whose left-hand node should be moved.
  */
 void parser_node_move_right_left_to_left(struct node* node)
 {
     make_exp_node(node->exp.left, node->exp.right->exp.left, node->exp.op);
     struct node* completed_node = node_pop();
 
     // We still need to deal with the right node
     const char* new_op = node->exp.right->exp.op;
     node->exp.left = completed_node;
     node->exp.right = node->exp.right->exp.right;
     node->exp.op = new_op;
 }
 
 /**
  * @brief Reorder expression tree nodes to satisfy precedence and associativity.
  *
  * Modifies the provided node in-place; if the node is not an expression or
  * no reordering is necessary the function returns immediately.
  */
 void parser_reorder_expression(struct node **node_out)
 {
     struct node *node = *node_out;
     if (node->type != NODE_TYPE_EXPRESSION)
     {
         return;
     }
 
     // No expressions, nothing to do
     if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
         node->exp.right && node->exp.right->type != NODE_TYPE_EXPRESSION)
     {
         return;
     }
 
     // 50*E(30+20)
     // 50*EXPRESSION
     // EXPRESSION(50*EXPRESSION(30+20))
     // (50*30)+20
     if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
         node->exp.right && node->exp.right->type == NODE_TYPE_EXPRESSION)
     {
         const char *right_op = node->exp.right->exp.op;
         if (parser_left_op_has_priority(node->exp.op, right_op))
         {
             // 50*E(20+120)
             // E(50*20)+120
             parser_node_shift_children_left(node);
 
             parser_reorder_expression(&node->exp.left);
             parser_reorder_expression(&node->exp.right);
         }
     }
 
                              
     if ((is_array_node(node->exp.left) || is_node_assignment(node->exp.right)) ||
         ((node_is_expression(node->exp.left, "()")) &&
          node_is_expression(node->exp.right, ",")))
     {
         parser_node_move_right_left_to_left(node);
     }
 }
 
 /**
  * @brief Parse an "ordinary" binary operator expression.
  *
  * This function expects an operator token next, pops the left node, and then
  * parses the right-hand side to build an expression node which is then
  * reordered according to operator precedence.
  */
 void parse_exp_normal(struct history *history)
 {
     struct token *op_token = token_peek_next();
     const char *op = op_token->sval;
     struct node *node_left = node_peek_expressionable_or_null();
     if (!node_left)
     {
         return;
     }
 
     // Pop off the operator token
     token_next();
 
     // Pop off the left node
     node_pop();
     node_left->flags |= NODE_FLAG_INSIDE_EXPRESSION;
     parse_expressionable_for_op(history_down(history, history->flags), op);
     struct node *node_right = node_pop();
     node_right->flags |= NODE_FLAG_INSIDE_EXPRESSION;
 
     make_exp_node(node_left, node_right, op);
     struct node *exp_node = node_pop();
 
     // Reorder the expression
     parser_reorder_expression(&exp_node);
     node_push(exp_node);
 }
 
 /**
  * @brief If the next token is an operator, parse an expression starting there.
  *
  * Used to continue parsing chained expressions after parentheses/array access
  * etc.
  */
 void parser_deal_with_additional_expression()
 {
     if (token_peek_next()->type == TOKEN_TYPE_OPERATOR)
     {
         parse_expressionable(history_begin(0));
     }
 }
 
 /**
  * @brief Parse a parenthesized expression or a cast.
  *
  * Handles the forms: (expr), (type)expr, and call-expression like test(50+20)
  * where a value may appear before the parentheses.
  */
 void parse_for_parentheses(struct history *history)
 {
     expect_op("(");
     if (token_peek_next()->type == TOKEN_TYPE_KEYWORD)
     {
         parse_for_cast();
         return;
     }
 
     struct node *left_node = NULL;
     struct node *tmp_node = node_peek_or_null();
 
     // test(50+20)
     if (tmp_node && node_is_value_type(tmp_node))
     {
         left_node = tmp_node;
         node_pop();
     }
 
     struct node *exp_node = parser_blank_node;
     if (!token_next_is_symbol(')'))
     {
         parse_expressionable_root(history_begin(0));
         exp_node = node_pop();
     }
     expect_sym(')');
 
     make_exp_parentheses_node(exp_node);
 
     if (left_node)
     {
         struct node *parentheses_node = node_pop();
         make_exp_node(left_node, parentheses_node, "()");
     }
 
     parser_deal_with_additional_expression();
 }
 
 /**
  * @brief Parse a comma expression: left , right
  */
 void parse_for_comma(struct history *history)
 {
     // Skip the comma
     token_next();
     struct node *left_node = node_pop();
     parse_expressionable_root(history);
     struct node *right_node = node_pop();
     make_exp_node(left_node, right_node, ",");
 }
 
 /**
  * @brief Parse array indexing/creation: left [ expr ]
  */
 void parse_for_array(struct history *history)
 {
     struct node *left_node = node_peek_or_null();
     if (left_node)
     {
         node_pop();
     }
 
     expect_op("[");
     parse_expressionable_root(history);
     expect_sym(']');
 
     struct node *exp_node = node_pop();
     make_bracket_node(exp_node);
 
     if (left_node)
     {
         struct node *bracket_node = node_pop();
         make_exp_node(left_node, bracket_node, "[]");
     }
 }
 
 /**
  * @brief Parse a cast expression after a leading '('.
  *
  * Called when '(' was consumed and the following tokens denote a type
  * declaration (eg. (int) ).
  */
 void parse_for_cast()
 {
     // "(" is already parsed i.e (char) seen as char)
     struct datatype dtype = {};
     parse_datatype(&dtype);
     expect_sym(')');
 
     parse_expressionable(history_begin(0));
     struct node *operand_node = node_pop();
     make_cast_node(&dtype, operand_node);
 }
 
 /**
  * @brief Dispatch function to parse an expression depending on next punctuation.
  *
  * Handles parentheses, array brackets, ternary, comma expressions or falls
  * back to normal binary operator parsing.
  */
 int parse_exp(struct history *history)
 {
     if (S_EQ(token_peek_next()->sval, "("))
     {
         parse_for_parentheses(history);
     }
     else if (S_EQ(token_peek_next()->sval, "["))
     {
         parse_for_array(history);
     }
     else if (S_EQ(token_peek_next()->sval, "?"))
     {
         parse_for_tenary(history);
     }
     else if (S_EQ(token_peek_next()->sval, ","))
     {
         parse_for_comma(history);
     }
     else
     {
         parse_exp_normal(history);
     }
     return 0;
 }
 
 /**
  * @brief Parse an identifier token and convert it to a node.
  */
 void parse_identifier(struct history *history)
 {
     assert(token_peek_next()->type == TOKEN_TYPE_IDENTIFIER);
     parse_single_token_to_node();
 }
 
 /**
  * @brief Returns true if token value is a variable modifier keyword.
  *
  * Recognized modifiers: unsigned, signed, static, const, extern and
  * __ignore_typecheck__.
  */
 static bool is_keyword_variable_modifier(const char *val)
 {
     return S_EQ(val, "unsigned") ||
            S_EQ(val, "signed") ||
            S_EQ(val, "static") ||
            S_EQ(val, "const") ||
            S_EQ(val, "extern") ||
            S_EQ(val, "__ignore_typecheck__");
 }
 
 /**
  * @brief Parse and apply datatype modifiers (signed/unsigned/static/const).
  *
  * This consumes zero or more modifier keywords and sets flags in the
  * provided datatype structure.
  */
 void parse_datatype_modifiers(struct datatype *dtype)
 {
     struct token *token = token_peek_next();
     while (token && token->type == TOKEN_TYPE_KEYWORD)
     {
         if (!is_keyword_variable_modifier(token->sval))
         {
             break;
         }
 
         if (S_EQ(token->sval, "signed"))
         {
             dtype->flags |= DATATYPE_FLAG_IS_SIGNED;
         }
         else if (S_EQ(token->sval, "unsigned"))
         {
             dtype->flags &= ~DATATYPE_FLAG_IS_SIGNED;
         }
         else if (S_EQ(token->sval, "static"))
         {
             dtype->flags |= DATATYPE_FLAG_IS_STATIC;
         }
         else if (S_EQ(token->sval, "const"))
         {
             dtype->flags |= DATATYPE_FLAG_IS_CONST;
         }
         else if (S_EQ(token->sval, "extern"))
         {
             dtype->flags |= DATATYPE_FLAG_IS_EXTERN;
         }
         else if (S_EQ(token->sval, "__ignore_typecheck__"))
         {
             dtype->flags |= DATATYPE_FLAG_IGNORE_TYPE_CHECKING;
         }
 
         token_next();
         token = token_peek_next();
     }
 }
 
 /**
  * @brief Consume tokens to produce the main and optional secondary datatype tokens.
  *
  * The function consumes the primary datatype token and, if a secondary
  * (like "long" "long" or "long" "double") token is present, consumes it
  * too and returns both via out parameters.
  */
 void parser_get_datatype_tokens(struct token **datatype_token, struct token **datatype_secondary_token)
 {
     *datatype_token = token_next();
     struct token *next_token = token_peek_next();
     if (token_is_primitive_keyword(next_token))
     {
         *datatype_secondary_token = next_token;
         token_next();
     }
 }
 
 /**
  * @brief Given a string like "struct"/"union"/primitive decide expected type.
  */
 int parser_datatype_expected_for_type_string(const char *str)
 {
     int type = DATA_TYPE_EXPECT_PRIMITIVE;
     if (S_EQ(str, "union"))
     {
         type = DATA_TYPE_EXPECT_UNION;
     }
     else if (S_EQ(str, "struct"))
     {
         type = DATA_TYPE_EXPECT_STRUCT;
     }
 
     return type;
 }
 
 /**
  * @brief Return a (deterministic) pseudo-random type index for synthetic names.
  */
 int parser_get_random_type_index()
 {
     static int x = 0;
     x++;
     return x;
 }
 
 /**
  * @brief Build and return a synthetic token naming an anonymous struct/union type.
  *
  * The returned token is heap-allocated and should be managed by the caller or
  * the parser's memory ownership model.
  */
 struct token *parser_build_random_type_name()
 {
     char tmp_name[25];
     sprintf(tmp_name, "customtypename_%i", parser_get_random_type_index());
     char *sval = malloc(sizeof(tmp_name));
     strncpy(sval, tmp_name, sizeof(tmp_name));
     struct token *token = calloc(1, sizeof(struct token));
     token->type = TOKEN_TYPE_IDENTIFIER;
     token->sval = sval;
     return token;
 }
 
 /**
  * @brief Count consecutive '*' operators to compute pointer depth.
  *
  * Consumes '*' tokens while present and returns the pointer depth.
  */
 int parser_get_pointer_depth()
 {
     int depth = 0;
     while (token_next_is_operator("*"))
     {
         depth++;
         token_next();
     }
     return depth;
 }
 
 /**
  * @brief Whether secondary datatype tokens are allowed for the expected type.
  */
 bool parser_datatype_is_secondary_allowed(int expected_type)
 {
     return expected_type == DATA_TYPE_EXPECT_PRIMITIVE;
 }
 
 /**
  * @brief Whether a given type string allows a secondary token (like "long").
  */
 bool parser_datatype_is_secondary_allowed_for_type(const char *type)
 {
     return S_EQ(type, "long") || S_EQ(type, "short") || S_EQ(type, "double") || S_EQ(type, "float");
 }
 
 /* Forward declaration for an internal helper used below */
 void parser_datatype_init_type_and_size_for_primitive(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out);
 
 /**
  * @brief If a secondary datatype token exists adjust the size/secondary fields.
  */
 void parser_datatype_adjust_size_for_secondary(struct datatype *datatype, struct token *datatype_secondary_token)
 {
     if (!datatype_secondary_token)
     {
         return;
     }
 
     struct datatype *secondary_data_type = calloc(1, sizeof(struct datatype));
     parser_datatype_init_type_and_size_for_primitive(datatype_secondary_token, NULL, secondary_data_type);
     datatype->size += secondary_data_type->size;
     datatype->secondary = secondary_data_type;
     datatype->flags |= DATATYPE_FLAG_IS_SECONDARY;
 }
 
 /**
  * @brief Initialize primitive datatype kind and size from tokens.
  *
  * This supports primitives like void, char, short, int, long, float, double.
  * On invalid primitive token an internal compiler error is raised.
  */
 void parser_datatype_init_type_and_size_for_primitive(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out)
 {
     if (!parser_datatype_is_secondary_allowed_for_type(datatype_token->sval) && datatype_secondary_token)
     {
         compiler_error(current_process, "Your not allowed a secondary datatype here for the given datatype %s\n", datatype_token->sval);
     }
 
     if (S_EQ(datatype_token->sval, "void"))
     {
         datatype_out->type = DATA_TYPE_VOID;
         datatype_out->size = DATA_SIZE_ZERO;
     }
     else if (S_EQ(datatype_token->sval, "char"))
     {
         datatype_out->type = DATA_TYPE_CHAR;
         datatype_out->size = DATA_SIZE_BYTE;
     }
     else if (S_EQ(datatype_token->sval, "short"))
     {
         datatype_out->type = DATA_TYPE_SHORT;
         datatype_out->size = DATA_SIZE_WORD;
     }
     else if (S_EQ(datatype_token->sval, "int"))
     {
         datatype_out->type = DATA_TYPE_INTEGER;
         datatype_out->size = DATA_SIZE_DWORD;
     }
     else if (S_EQ(datatype_token->sval, "long"))
     {
         datatype_out->type = DATA_TYPE_LONG;
         datatype_out->size = DATA_SIZE_DWORD;
     }
     else if (S_EQ(datatype_token->sval, "float"))
     {
         datatype_out->type = DATA_TYPE_FLOAT;
         datatype_out->size = DATA_SIZE_DWORD;
     }
     else if (S_EQ(datatype_token->sval, "double"))
     {
         datatype_out->size = DATA_TYPE_DOUBLE;
         datatype_out->size = DATA_SIZE_DWORD;
     }
     else
     {
         compiler_error(current_process, "BUG: Invalid primitive datatype\n");
     }
 
     parser_datatype_adjust_size_for_secondary(datatype_out, datatype_secondary_token);
 }
 
 /**
  * @brief Helper: compute size of a struct by resolving its symbol.
  */
 size_t size_of_struct(const char *struct_name)
 {
     struct symbol *sym = symresolver_get_symbol(current_process, struct_name);
     if (!sym)
     {
         return 0;
     }
 
     assert(sym->type == SYMBOL_TYPE_NODE);
     struct node *node = sym->data;
     assert(node->type == NODE_TYPE_STRUCT);
     return node->_struct.body_n->body.size;
 }
 
 /**
  * @brief Helper: compute size of a union by resolving its symbol.
  */
 size_t size_of_union(const char *union_name)
 {
     struct symbol *sym = symresolver_get_symbol(current_process, union_name);
     if (!sym)
     {
         return 0;
     }
 
     assert(sym->type == SYMBOL_TYPE_NODE);
     struct node *node = sym->data;
     assert(node->type == NODE_TYPE_UNION);
     return node->_union.body_n->body.size;
 }
 
 /**
  * @brief Initialize a datatype value given tokens, pointer depth and expected kind.
  *
  * Handles primitive/struct/union expectations and fills the provided
  * datatype_out. On errors compiler_error() is called.
  */
 void parser_datatype_init_type_and_size(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type)
 {
     if (!parser_datatype_is_secondary_allowed(expected_type) && datatype_secondary_token)
     {
         compiler_error(current_process, "You provided an invalid secondary datatype\n");
     }
 
     switch (expected_type)
     {
     case DATA_TYPE_EXPECT_PRIMITIVE:
         parser_datatype_init_type_and_size_for_primitive(datatype_token, datatype_secondary_token, datatype_out);
         break;
 
     case DATA_TYPE_EXPECT_STRUCT:
         datatype_out->type = DATA_TYPE_STRUCT;
         datatype_out->size = size_of_struct(datatype_token->sval);
         datatype_out->struct_node = struct_node_for_name(current_process, datatype_token->sval);
         break;
     case DATA_TYPE_EXPECT_UNION:
         datatype_out->type = DATA_TYPE_UNION;
         datatype_out->size = size_of_union(datatype_token->sval);
         datatype_out->struct_node = union_node_for_name(current_process, datatype_token->sval);
         break;
 
     default:
         compiler_error(current_process, "BUG: Unsupported datatype expectation\n");
     }
 }
 
 /**
  * @brief Higher-level initializer that also sets type_str and warns for long long.
  */
 void parser_datatype_init(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type)
 {
     parser_datatype_init_type_and_size(datatype_token, datatype_secondary_token, datatype_out, pointer_depth, expected_type);
     datatype_out->type_str = datatype_token->sval;
 
     if (S_EQ(datatype_token->sval, "long") && datatype_secondary_token && S_EQ(datatype_secondary_token->sval, "long"))
     {
         compiler_warning(current_process, "Our compiler does not support 64 bit longs, therefore your long long is defaulting to 32 bits\n");
         datatype_out->size = DATA_SIZE_DWORD;
     }
 }
 
 /**
  * @brief Parse a datatype specification and populate the provided datatype.
  *
  * This function handles modifiers, anonymous struct/union names and pointer
  * depth. It sets appropriate flags for anonymous struct/union types.
  */
 void parse_datatype_type(struct datatype *dtype)
 {
     struct token *datatype_token = NULL;
     struct token *datatype_secondary_token = NULL;
     parser_get_datatype_tokens(&datatype_token, &datatype_secondary_token);
     int expected_type = parser_datatype_expected_for_type_string(datatype_token->sval);
     if (datatype_is_struct_or_union_for_name(datatype_token->sval))
     {
         if (token_peek_next()->type == TOKEN_TYPE_IDENTIFIER)
         {
             datatype_token = token_next();
         }
         else
         {
             // This structure has no name, so we need to handle it.
             datatype_token = parser_build_random_type_name();
             dtype->flags |= DATATYPE_FLAG_STRUCT_UNION_NO_NAME;
         }
     }
 
     // int**
     int pointer_depth = parser_get_pointer_depth();
     parser_datatype_init(datatype_token, datatype_secondary_token, dtype, pointer_depth, expected_type);
 }
 
 /**
  * @brief Parse a full datatype including surrounding modifiers.
  */
 void parse_datatype(struct datatype *dtype)
 {
     memset(dtype, 0, sizeof(struct datatype));
     dtype->flags |= DATATYPE_FLAG_IS_SIGNED;
 
     parse_datatype_modifiers(dtype);
     parse_datatype_type(dtype);
     parse_datatype_modifiers(dtype);
 }
 
 /**
  * @brief Check whether a following "int" token is valid for certain datatypes.
  */
 bool parser_is_int_valid_after_datatype(struct datatype *dtype)
 {
     return dtype->type == DATA_TYPE_LONG || dtype->type == DATA_TYPE_FLOAT || dtype->type == DATA_TYPE_DOUBLE;
 }
 
 /**
  * @brief Consume an optional "int" token following a datatype abbreviation.
  *
  * Example: "long int x;" -- this will consume the "int" after "long".
  * If the combination is invalid an error is raised.
  */
 void parser_ignore_int(struct datatype *dtype)
 {
     if (!token_is_keyword(token_peek_next(), "int"))
     {
         // No integer to ignore.
         return;
     }
 
     if (!parser_is_int_valid_after_datatype(dtype))
     {
         compiler_error(current_process, "You provided a secondary \"int\" type however its not supported with this current abbrevation\n");
     }
 
     // Ignore the "int" token
     token_next();
 }
 
 /**
 * @brief Parse a root-level expressionable entity (e.g., statement).
 *
 * Pops the parsed expression node and pushes it back for later use.
 *
 * @param history Parsing context and scope history.
 */
void parse_expressionable_root(struct history *history)
{
    parse_expressionable(history);
    struct node *result_node = node_pop();
    node_push(result_node);
}

/**
 * @brief Private data used by struct fixups.
 *
 * Stores the node that needs its struct type resolved later.
 */
struct datatype_struct_node_fix_private
{
    struct node *node; ///< Node whose struct type must be fixed.
};

/**
 * @brief Fixup handler for struct datatype nodes.
 *
 * Attempts to resolve the struct definition for a variable node that
 * references a struct type not yet parsed.
 *
 * @param fixup Fixup to resolve.
 * @return true if the struct was successfully resolved, false otherwise.
 */
bool datatype_struct_node_fix(struct fixup *fixup)
{
    struct datatype_struct_node_fix_private *private = fixup_private(fixup);
    struct datatype *dtype = &private->node->var.type;
    dtype->type = DATA_TYPE_STRUCT;
    dtype->size = size_of_struct(dtype->type_str);
    dtype->struct_node = struct_node_for_name(current_process, dtype->type_str);
    if (!dtype->struct_node)
    {
        return false;
    }

    return true;
}

/**
 * @brief Cleanup callback for struct node fixups.
 *
 * Frees the private data allocated during variable node creation.
 *
 * @param fixup Fixup to clean up.
 */
void datatype_struct_node_end(struct fixup *fixup)
{
    free(fixup_private(fixup));
}

/**
 * @brief Create a variable node in the AST.
 *
 * Handles struct fixups if the datatype is an unresolved struct.
 *
 * @param dtype       Datatype of the variable.
 * @param name_token  Token containing the variable name (may be NULL).
 * @param value_node  Optional initialization expression node.
 */
void make_variable_node(struct datatype *dtype, struct token *name_token, struct node *value_node)
{
    const char *name_str = NULL;
    if (name_token)
    {
        name_str = name_token->sval;
    }

    node_create(&(struct node){
        .type = NODE_TYPE_VARIABLE,
        .var.name = name_str,
        .var.type = *dtype,
        .var.val = value_node
    });

    struct node *var_node = node_peek_or_null();
    if (var_node->var.type.type == DATA_TYPE_STRUCT && !var_node->var.type.struct_node)
    {
        struct datatype_struct_node_fix_private *private =
            calloc(1, sizeof(struct datatype_struct_node_fix_private));
        private->node = var_node;

        fixup_register(parser_fixup_sys, &(struct fixup_config){
            .fix = datatype_struct_node_fix,
            .end = datatype_struct_node_end,
            .private = private
        });
    }
}

/**
 * @brief Compute the stack offset for a variable in the current scope.
 *
 * Handles stack growth direction (upward/downward) and alignment padding.
 *
 * @param node    Variable node to compute offset for.
 * @param history Parsing context and scope history.
 */
void parser_scope_offset_for_stack(struct node *node, struct history *history)
{
    struct parser_scope_entity *last_entity = parser_scope_last_entity_stop_global_scope();
    bool upward_stack = history->flags & HISTORY_FLAG_IS_UPWARD_STACK;
    int offset = -variable_size(node);

    if (upward_stack)
    {
        size_t stack_addition = function_node_argument_stack_addition(parser_current_function);
        offset = stack_addition;
        if (last_entity)
        {
            offset = datatype_size(&variable_node(last_entity->node)->var.type);
        }
    }

    if (last_entity)
    {
        offset += variable_node(last_entity->node)->var.aoffset;
        if (variable_node_is_primitive(node))
        {
            variable_node(node)->var.padding =
                padding(upward_stack ? offset : -offset, node->var.type.size);
        }
    }
}

/**
 * @brief Compute global variable offset (currently stubbed).
 */
void parser_scope_offset_for_global(struct node *node, struct history *history)
{
    (void)node;
    (void)history;
}

/**
 * @brief Compute the offset for a struct/union field inside its body.
 *
 * Applies alignment and padding rules for struct layout.
 *
 * @param node    Field variable node.
 * @param history Parsing context and scope history.
 */
void parser_scope_offset_for_structure(struct node *node, struct history *history)
{
    int offset = 0;
    struct parser_scope_entity *last_entity = parser_scope_last_entity();
    if (last_entity)
    {
        offset += last_entity->stack_offset + last_entity->node->var.type.size;
        if (variable_node_is_primitive(node))
        {
            node->var.padding = padding(offset, node->var.type.size);
        }

        node->var.aoffset = offset + node->var.padding;
    }
}

/**
 * @brief Dispatch scope offset computation depending on context.
 *
 * Handles global, struct, or stack-local variable placement.
 *
 * @param node    Variable node.
 * @param history Parsing context and scope history.
 */
void parser_scope_offset(struct node *node, struct history *history)
{
    if (history->flags & HISTORY_FLAG_IS_GLOBAL_SCOPE)
    {
        parser_scope_offset_for_global(node, history);
        return;
    }

    if (history->flags & HISTORY_FLAG_INSIDE_STRUCTURE)
    {
        parser_scope_offset_for_structure(node, history);
        return;
    }

    parser_scope_offset_for_stack(node, history);
}

/**
 * @brief Create a variable node and register it into the current scope.
 *
 * Assigns scope offsets, registers into symbol table, and pushes
 * the node onto the AST stack.
 *
 * @param history    Parsing context and scope history.
 * @param dtype      Datatype of the variable.
 * @param name_token Token containing variable name.
 * @param value_node Optional initialization expression.
 */
void make_variable_node_and_register(struct history *history,
                                     struct datatype *dtype,
                                     struct token *name_token,
                                     struct node *value_node)
{
    make_variable_node(dtype, name_token, value_node);
    struct node *var_node = node_pop();

    parser_scope_offset(var_node, history);

    parser_scope_push(
        parser_new_scope_entity(var_node, var_node->var.aoffset, 0),
        var_node->var.type.size
    );

    node_push(var_node);
}

/**
 * @brief Create a VARIABLE_LIST AST node from a vector of variable nodes.
 *
 * This wraps a vector of variable node pointers into a NODE_TYPE_VARIABLE_LIST node.
 *
 * @param var_list_vec Vector containing pointers to struct node objects (variables).
 */
 void make_variable_list_node(struct vector *var_list_vec)
 {
     node_create(&(struct node){.type = NODE_TYPE_VARIABLE_LIST, .var_list.list = var_list_vec});
 }
 
 /**
  * @brief Parse one or more array bracket expressions following an identifier.
  *
  * Supports parsing sequences like: `[10]`, `[][]`, `[expr]`, etc.
  * For each bracket pair:
  *  - If the brackets are empty `[]` the loop breaks (represents unspecified size).
  *  - Otherwise parses an expression for the bracket contents and converts it into a bracket node.
  * The resulting bracket nodes are accumulated into an array_brackets object which is returned.
  *
  * @param history Current parsing history/context (used when parsing expressions).
  * @return pointer to a newly created array_brackets structure describing the parsed brackets.
  */
 struct array_brackets *parse_array_brackets(struct history *history)
 {
     struct array_brackets *brackets = array_brackets_new();
     while (token_next_is_operator("["))
     {
         expect_op("[");
         if (token_is_symbol(token_peek_next(), ']'))
         {
             // Nothing between the brackets -> empty dimension (e.g. int a[];)
             expect_sym(']');
             break;
         }
 
         // Parse the expression inside the brackets, e.g. the size or index expression
         parse_expressionable_root(history);
         expect_sym(']');
 
         // Wrap the parsed expression into a bracket AST node and add it to the list
         struct node *exp_node = node_pop();
         make_bracket_node(exp_node);
 
         struct node *bracket_node = node_pop();
         array_brackets_add(brackets, bracket_node);
     }
 
     return brackets;
 }
 
 /**
  * @brief Parse a variable declaration (including arrays and optional initializer).
  *
  * Handles forms like:
  *   type name;
  *   type name[expr];
  *   type name = expr;
  *   type name[expr] = expr;
  *
  * After parsing possible array brackets and optional initializer, creates and registers
  * the variable node in the current scope.
  *
  * @param dtype      Datatype object describing the base type (modified if array).
  * @param name_token Token containing the variable name (may be NULL for unnamed declarations).
  * @param history    Current parsing history/context (affects scope/offset computation).
  */
 void parse_variable(struct datatype *dtype, struct token *name_token, struct history *history)
 {
     struct node *value_node = NULL;
     /* int a; int b[30]; */
     /* Check for array brackets. */
     struct array_brackets *brackets = NULL;
     if (token_next_is_operator("["))
     {
         brackets = parse_array_brackets(history);
         dtype->array.brackets = brackets;
         dtype->array.size = array_brackets_calculate_size(dtype, brackets);
         dtype->flags |= DATATYPE_FLAG_IS_ARRAY;
     }
 
     /* int c = 50; */
     if (token_next_is_operator("="))
     {
         /* Skip the '=' operator token and parse the initializer expression. */
         token_next();
         parse_expressionable_root(history);
         value_node = node_pop();
     }
 
     /* Create the variable node and register it in the current scope. */
     make_variable_node_and_register(history, dtype, name_token, value_node);
 }
 
 /**
  * @brief Parse a function body (wrapper).
  *
  * Calls parse_body with the INSIDE_FUNCTION_BODY flag set in the history so that
  * the body is treated as a function body (stack accounting, local scopes, etc).
  *
  * @param history Current parsing history/context.
  */
 void parse_function_body(struct history *history)
 {
     parse_body(NULL, history_down(history, history->flags | HISTORY_FLAG_INSIDE_FUNCTION_BODY));
 }
 
 /**
  * @brief Parse a function declaration/definition.
  *
  * Creates a function node, parses its argument list and (optionally) its body.
  * Tracks whether the function returns a struct/union (for argument stack addition).
  * Marks native functions (if found in the symbol resolver) and handles both:
  *   - function prototype (ending with ';')
  *   - function definition (body starting with '{')
  *
  * @param ret_type   Return datatype for the function.
  * @param name_token Token containing the function name.
  * @param history    Current parsing history/context.
  */
 void parse_function(struct datatype *ret_type, struct token *name_token, struct history *history)
 {
     struct vector *arguments_vector = NULL;
     parser_scope_new();
     make_function_node(ret_type, name_token->sval, NULL, NULL);
     struct node *function_node = node_peek();
     parser_current_function = function_node;
 
     /* If returning a struct/union, account for additional stack space required. */
     if (datatype_is_struct_or_union(ret_type))
     {
         function_node->func.args.stack_addition += DATA_SIZE_DWORD;
     }
 
     expect_op("(");
     arguments_vector = parse_function_arguments(history_begin(0));
     expect_sym(')');
 
     function_node->func.args.vector = arguments_vector;
 
     /* Detect and flag native functions known by the symbol resolver. */
     if (symresolver_get_symbol_for_native_function(current_process, name_token->sval))
     {
         function_node->func.flags |= FUNCTION_NODE_FLAG_IS_NATIVE;
     }
 
     /* If a body is present, parse it; otherwise expect a terminating semicolon (prototype). */
     if (token_next_is_symbol('{'))
     {
         parse_function_body(history_begin(0));
         struct node *body_node = node_pop();
         function_node->func.body_n = body_node;
     }
     else
     {
         expect_sym(';');
     }
 
     parser_current_function = NULL;
     parser_scope_finish();
 }
 
 /**
  * @brief Parse a symbol that follows an expression (e.g., compound initializer or label).
  *
  * This function handles:
  *  - A leading '{' meaning an inline body (global scope assumed here).
  *  - A ':' symbol meaning a label.
  *
  * If neither is matched, a compiler error is reported.
  */
 void parse_symbol()
 {
     if (token_next_is_symbol('{'))
     {
         size_t variable_size = 0;
         struct history *history = history_begin(HISTORY_FLAG_IS_GLOBAL_SCOPE);
         parse_body(&variable_size, history);
         struct node *body_node = node_pop();
 
         node_push(body_node);
     }
     else if (token_next_is_symbol(':'))
     {
         parse_label(history_begin(0));
         return;
     }
 
     compiler_error(current_process, "Invalid symbol was provided");
 }
 
 /**
  * @brief Parse a statement.
  *
  * Distinguishes between keyword-starting statements (delegating to parse_keyword),
  * expression statements (parsing an expression and optionally a following symbol),
  * and ensures statements are terminated by ';' when appropriate.
  *
  * @param history Current parsing history/context.
  */
 void parse_statement(struct history *history)
 {
     if (token_peek_next()->type == TOKEN_TYPE_KEYWORD)
     {
         parse_keyword(history);
         return;
     }
 
     /* Parse the expression forming the statement. */
     parse_expressionable_root(history);
 
     /*
      * If next token is a symbol but not a semicolon, it may be a label, initializer
      * block, or other symbol-handled construct.
      */
     if (token_peek_next()->type == TOKEN_TYPE_SYMBOL && !token_is_symbol(token_peek_next(), ';'))
     {
         parse_symbol();
         return;
     }
 
     /* All simple expression-statements must end with a semicolon. */
     expect_sym(';');
 }
 
 /**
  * @brief Append size contributions of a struct/union variable to the running variable size.
  *
  * For a struct/union variable:
  *  - Add the variable_size (which may include nested members).
  *  - If it is not a pointer, attempt to align the running size according to the largest
  *    member of the struct/union body (largest_var_node).
  *
  * @param history         Current parsing history/context.
  * @param _variable_size  Pointer to the running size accumulator to modify.
  * @param node            Variable node representing the struct/union instance.
  */
 void parser_append_size_for_node_struct_union(struct history *history, size_t *_variable_size, struct node *node)
 {
     *_variable_size += variable_size(node);
     if (node->var.type.flags & DATATYPE_FLAG_IS_POINTER)
     {
         return;
     }
 
     struct node *largest_var_node = variable_struct_or_union_body_node(node)->body.largest_var_node;
     if (largest_var_node)
     {
         /* Align the accumulator according to the largest member's size. */
         *_variable_size += align_value(*_variable_size, largest_var_node->var.type.size);
     }
 }
 
 /**
  * @brief Forward declaration for appending node sizes.
  *
  * The real implementation is provided below (or elsewhere in the file). This forward
  * declaration exists so helper functions can be declared/defined in any order.
  */
 void parser_append_size_for_node(struct history *history, size_t *_variable_size, struct node *node);
 
 /**
  * @brief Append the size contributions of all variables in a variable-list vector.
  *
  * Iterates over the vector and calls parser_append_size_for_node for each element.
  *
  * @param history       Current parsing history/context.
  * @param variable_size Pointer to the running size accumulator to modify.
  * @param vec           Vector containing pointers to struct node objects (variables).
  */
 void parser_append_size_for_variable_list(struct history *history, size_t *variable_size, struct vector *vec)
 {
     vector_set_peek_pointer(vec, 0);
     struct node *node = vector_peek_ptr(vec);
     while (node)
     {
         parser_append_size_for_node(history, variable_size, node);
         node = vector_peek_ptr(vec);
     }
 }
 
 /**
  * @brief Append size contribution of a single node (variable or variable-list).
  *
  * - If node is a single VARIABLE:
  *     - If it's a struct/union variable, delegate to parser_append_size_for_node_struct_union.
  *     - Otherwise add its variable_size.
  * - If node is a VARIABLE_LIST, iterate over the list and add each element's size.
  *
  * This function tolerates NULL nodes (no-op).
  *
  * @param history         Current parsing history/context.
  * @param _variable_size  Pointer to the running size accumulator to modify.
  * @param node            Node to inspect and add size for.
  */
 void parser_append_size_for_node(struct history *history, size_t *_variable_size, struct node *node)
 {
     if (!node)
     {
         return;
     }
 
     if (node->type == NODE_TYPE_VARIABLE)
     {
         if (node_is_struct_or_union_variable(node))
         {
             parser_append_size_for_node_struct_union(history, _variable_size, node);
             return;
         }
 
         *_variable_size += variable_size(node);
     }
     else if (node->type == NODE_TYPE_VARIABLE_LIST)
     {
         parser_append_size_for_variable_list(history, _variable_size, node->var_list.list);
     }
 }
 
 /**
 * @brief Finalize a parsed body node: compute size, padding and metadata.
 *
 * This function finalizes layout information for a body (block/struct/union body) by:
 *  - Handling union size semantics (largest member determines union size).
 *  - Summing padding for members/statements.
 *  - Aligning the total size to the largest alignment-eligible member when necessary.
 *  - Recording whether padding occurred and storing resulting size/statements.
 *
 * @param history                       Current parsing history/context (flags indicate union/struct/function etc).
 * @param body_node                     The body node being finalized (will be updated).
 * @param body_vec                      Vector containing the statements/nodes belonging to the body.
 * @param _variable_size                Pointer to running size accumulator for variables (updated).
 * @param largest_align_eligible_var_node Node with the largest align-eligible size (used to align total).
 * @param largest_possible_var_node     Node with the largest possible size (used for unions).
 */
void parser_finalize_body(struct history *history,
    struct node *body_node,
    struct vector *body_vec,
    size_t *_variable_size,
    struct node *largest_align_eligible_var_node,
    struct node *largest_possible_var_node)
{
if (history->flags & HISTORY_FLAG_INSIDE_UNION)
{
if (largest_possible_var_node)
{
/* For unions, the size of the union is the size of its largest member. */
*_variable_size = variable_size(largest_possible_var_node);
}
}

/* Compute cumulative padding required by members in the body. */
int padding = compute_sum_padding(body_vec);
*_variable_size += padding;

/* Align the final size to the largest alignment-eligible member if present. */
if (largest_align_eligible_var_node)
{
*_variable_size = align_value(*_variable_size, largest_align_eligible_var_node->var.type.size);
}

bool padded = padding != 0;

/* Store computed metadata in the body node. */
body_node->body.largest_var_node = largest_align_eligible_var_node;
body_node->body.padded = padded;
body_node->body.size = *_variable_size;
body_node->body.statements = body_vec;
}

/**
* @brief Parse a body consisting of a single statement (no surrounding braces).
*
* Used for constructs that accept a single statement without `{}`.
* Creates a body node, parses the single statement and finalizes the body metadata.
*
* @param variable_size Pointer to accumulator for total variable size in this body.
* @param body_vec      Vector to receive the parsed statement node pointer.
* @param history       Current parsing history/context.
*/
void parse_body_single_statement(size_t *variable_size, struct vector *body_vec, struct history *history)
{
make_body_node(NULL, 0, false, NULL);
struct node *body_node = node_pop();
body_node->binded.owner = parser_current_body;
parser_current_body = body_node;

struct node *stmt_node = NULL;
parse_statement(history_down(history, history->flags));
stmt_node = node_pop();
vector_push(body_vec, &stmt_node);

/* Update running variable size based on the parsed statement. */
parser_append_size_for_node(history, variable_size, stmt_node);

struct node *largest_var_node = NULL;
if (stmt_node->type == NODE_TYPE_VARIABLE)
{
largest_var_node = stmt_node;
}

/* Finalize the body using the single-statement's variable sizing info. */
parser_finalize_body(history, body_node, body_vec, variable_size, largest_var_node, largest_var_node);
parser_current_body = body_node->binded.owner;

node_push(body_node);
}

/**
* @brief Parse a body with multiple statements enclosed in `{ ... }`.
*
* Parses each statement in the block, tracks the largest variable (for alignment/size),
* accumulates statements into body_vec, and finalizes the body metadata.
*
* @param variable_size Pointer to accumulator for total variable size in this body.
* @param body_vec      Vector to receive parsed statement node pointers.
* @param history       Current parsing history/context.
*/
void parse_body_multiple_statements(size_t *variable_size, struct vector *body_vec, struct history *history)
{
/* Create an empty body node and set ownership for nested parsing. */
make_body_node(NULL, 0, false, NULL);
struct node *body_node = node_pop();
body_node->binded.owner = parser_current_body;
parser_current_body = body_node;

struct node *stmt_node = NULL;
struct node *largest_possible_var_node = NULL;
struct node *largest_align_eligible_var_node = NULL;

/* Expect opening '{' for the block. */
expect_sym('{');

while (!token_next_is_symbol('}'))
{
parse_statement(history_down(history, history->flags));
stmt_node = node_pop();

/* If the statement is a variable declaration, track it for sizing/alignment decisions. */
if (stmt_node->type == NODE_TYPE_VARIABLE)
{
if (!largest_possible_var_node ||
(largest_possible_var_node->var.type.size <= stmt_node->var.type.size))
{
largest_possible_var_node = stmt_node;
}

if (variable_node_is_primitive(stmt_node))
{
if (!largest_align_eligible_var_node ||
(largest_align_eligible_var_node->var.type.size <= stmt_node->var.type.size))
{
largest_align_eligible_var_node = stmt_node;
}
}
}

/* Append this statement to the body vector. */
vector_push(body_vec, &stmt_node);

/* Update running variable size in case the statement added variables. */
parser_append_size_for_node(history, variable_size, variable_node_or_list(stmt_node));
}

/* Expect closing '}' for the block. */
expect_sym('}');

/* Finalize body metadata and restore previous body ownership. */
parser_finalize_body(history, body_node, body_vec, variable_size, largest_align_eligible_var_node, largest_possible_var_node);
parser_current_body = body_node->binded.owner;

/* Push the completed body node back onto the node stack. */
node_push(body_node);
}

/**
* @brief Parse a body which may be either a single statement or a braced block.
*
* - Creates a new parsing scope for the body.
* - If the next token is not '{', parse a single statement body.
* - Otherwise parse multiple statements inside braces.
* - Updates the current function's stack size when parsing a function body.
*
* @param variable_size If non-NULL, receives the sum of all variable sizes encountered.
*                      If NULL, a temporary accumulator is used internally.
* @param history       Current parsing history/context.
*/
void parse_body(size_t *variable_size, struct history *history)
{
parser_scope_new();
size_t tmp_size = 0x00;
if (!variable_size)
{
variable_size = &tmp_size;
}

struct vector *body_vec = vector_create(sizeof(struct node *));
if (!token_next_is_symbol('{'))
{
parse_body_single_statement(variable_size, body_vec, history);
parser_scope_finish();
return;
}

/* Parse a block with multiple statements. */
parse_body_multiple_statements(variable_size, body_vec, history);
parser_scope_finish();

/* If this body is inside a function, add variable size to function's stack size. */
if (variable_size)
{
if (history->flags & HISTORY_FLAG_INSIDE_FUNCTION_BODY)
{
parser_current_function->func.stack_size += *variable_size;
}
}
}

/**
* @brief Parse a struct definition without creating a new parsing scope.
*
* Handles both named and anonymous struct definitions, optional trailing variable
* that combines declaration with the struct definition, and forward declarations.
*
* On success the created struct node is pushed onto the node stack.
*
* @param dtype                 Datatype describing the struct (type_str holds the struct name if any).
* @param is_forward_declaration True if this is a forward declaration (no body parsed).
*/
void parse_struct_no_new_scope(struct datatype *dtype, bool is_forward_declaration)
{
struct node *body_node = NULL;
size_t body_variable_size = 0;

if (!is_forward_declaration)
{
parse_body(&body_variable_size, history_begin(HISTORY_FLAG_INSIDE_STRUCTURE));
body_node = node_pop();
}

make_struct_node(dtype->type_str, body_node);
struct node *struct_node = node_pop();

if (body_node)
{
dtype->size = body_node->body.size;
}
dtype->struct_node = struct_node;

/* Optional combined variable: `struct S { ... } varname;` */
if (token_is_identifier(token_peek_next()))
{
struct token *var_name = token_next();
struct_node->flags |= NODE_FLAG_HAS_VARIABLE_COMBINED;

/* If struct had no name (anonymous), transfer the variable name as the type name. */
if (dtype->flags & DATATYPE_FLAG_STRUCT_UNION_NO_NAME)
{
dtype->type_str = var_name->sval;
dtype->flags &= ~DATATYPE_FLAG_STRUCT_UNION_NO_NAME;
struct_node->_struct.name = var_name->sval;
}

make_variable_node_and_register(history_begin(0), dtype, var_name, NULL);
struct_node->_struct.var = node_pop();
}

/* Struct declarations/definitions end with a semicolon. */
expect_sym(';');

/* Push the struct node back on the node stack for further processing. */
node_push(struct_node);
}

/**
* @brief Parse a union definition without creating a new parsing scope.
*
* Similar to parse_struct_no_new_scope but follows union layout semantics. May
* accept a combined variable declaration following the union definition.
*
* @param dtype                 Datatype describing the union (type_str holds the union name if any).
* @param is_forward_declaration True if this is a forward declaration (no body parsed).
*/
void parse_union_no_scope(struct datatype *dtype, bool is_forward_declaration)
{
struct node *body_node = NULL;
size_t body_variable_size = 0;

if (!is_forward_declaration)
{
parse_body(&body_variable_size, history_begin(HISTORY_FLAG_INSIDE_UNION));
body_node = node_pop();
}

make_union_node(dtype->type_str, body_node);
struct node *union_node = node_pop();

if (body_node)
{
dtype->size = body_node->body.size;
}

/* Optional combined variable: `union U { ... } varname;` */
if (token_peek_next()->type == TOKEN_TYPE_IDENTIFIER)
{
struct token *var_name = token_next();
union_node->flags |= NODE_FLAG_HAS_VARIABLE_COMBINED;
make_variable_node_and_register(history_begin(0), dtype, var_name, NULL);
union_node->_union.var = node_pop();
}

expect_sym(';');
node_push(union_node);
}

/**
* @brief Parse a union: handles creating a temporary scope unless it is a forward-declaration.
*
* @param dtype Datatype describing the union.
*/
void parse_union(struct datatype *dtype)
{
bool is_forward_declaration = !token_is_symbol(token_peek_next(), '{');
if (!is_forward_declaration)
{
parser_scope_new();
}

parse_union_no_scope(dtype, is_forward_declaration);

if (!is_forward_declaration)
{
parser_scope_finish();
}
}

/**
* @brief Parse a struct: handles creating a temporary scope unless it is a forward-declaration.
*
* @param dtype Datatype describing the struct.
*/
void parse_struct(struct datatype *dtype)
{
bool is_forward_declaration = !token_is_symbol(token_peek_next(), '{');
if (!is_forward_declaration)
{
parser_scope_new();
}

parse_struct_no_new_scope(dtype, is_forward_declaration);

if (!is_forward_declaration)
{
parser_scope_finish();
}
}

/**
* @brief Dispatch parsing of either a struct or union based on dtype->type.
*
* @param dtype Datatype that must be either DATA_TYPE_STRUCT or DATA_TYPE_UNION.
*/
void parse_struct_or_union(struct datatype *dtype)
{
switch (dtype->type)
{
case DATA_TYPE_STRUCT:
parse_struct(dtype);
break;

case DATA_TYPE_UNION:
parse_union(dtype);
break;

default:
compiler_error(current_process, "COMPILER BUG: The provided datatype is not a structure or union\n");
}
}

/**
* @brief Consume a sequence of dot ('.') tokens.
*
* Used to parse variadic argument ellipsis represented as ... when reading tokens.
*
* @param amount Number of dots to consume.
*/
void token_read_dots(size_t amount)
{
for (size_t i = 0; i < amount; i++)
{
expect_op(".");
}
}

/**
* @brief Parse a full variable declaration (datatype + optional name).
*
* Reads a datatype and then, if a name token is present, consumes it and delegates
* to parse_variable to parse optional arrays/initializer and register the variable.
*
* @param history Current parsing history/context.
*/
void parse_variable_full(struct history *history)
{
struct datatype dtype;
parse_datatype(&dtype);

struct token *name_token = NULL;
if (token_peek_next()->type == TOKEN_TYPE_IDENTIFIER)
{
name_token = token_next();
}
parse_variable(&dtype, name_token, history);
}

/**
* @brief Parse function arguments and return a vector of argument nodes.
*
* - Creates a new parser scope for arguments.
* - Supports variadic ellipsis ("...") by reading three dots and returning.
* - Each parsed argument uses history with the UPWARD_STACK flag so arguments
*   are treated appropriately for stack offsets.
*
* @param history Current parsing history/context.
* @return Vector containing pointers to parsed argument nodes.
*/
struct vector *parse_function_arguments(struct history *history)
{
parser_scope_new();
struct vector *arguments_vec = vector_create(sizeof(struct node *));
while (!token_next_is_symbol(')'))
{
if (token_next_is_operator("."))
{
token_read_dots(3);
parser_scope_finish();
return arguments_vec;
}

parse_variable_full(history_down(history, history->flags | HISTORY_FLAG_IS_UPWARD_STACK));
struct node *argument_node = node_pop();
vector_push(arguments_vec, &argument_node);

if (!token_next_is_operator(","))
{
break;
}

/* Consume comma and continue to next argument. */
token_next();
}

parser_scope_finish();
return arguments_vec;
}

/**
* @brief Handle a forward declaration for structs/unions.
*
* For forward declarations we still call parse_struct to consume the declaration
* tokens (which may be just a name and semicolon).
*
* @param dtype Datatype representing the forward-declared struct/union.
*/
void parse_forward_declaration(struct datatype *dtype)
{
/* Since this is a forward declaration, parse the structure */
parse_struct(dtype);
}


/**
 * @brief Parse either a variable, a function, or a struct/union declaration.
 *
 * This is a high-level entrypoint used when the parser expects a declaration that
 * may be:
 *   - a struct/union definition or forward declaration
 *   - a variable declaration (with arrays/initializers)
 *   - a function declaration/definition
 *
 * It also supports comma-separated variable lists like:
 *   int a, b[10], c = 3;
 *
 * @param history Current parsing history/context.
 */
 void parse_variable_function_or_struct_union(struct history *history)
 {
     struct datatype dtype;
     parse_datatype(&dtype);
 
     /* If we have 'struct/union <name> { ... }' parse the struct/union and
        register it in the symbol resolver. */
     if (datatype_is_struct_or_union(&dtype) && token_next_is_symbol('{'))
     {
         parse_struct_or_union(&dtype);
 
         struct node *su_node = node_pop();
         symresolver_build_for_node(current_process, su_node);
         node_push(su_node);
         return;
     }
 
     /* If this is a forward declaration terminated by ';', handle it */
     if (token_next_is_symbol(';'))
     {
         parse_forward_declaration(&dtype);
         return;
     }
 
     /* Normalize integer abbreviations (e.g., "long int" -> "long") if needed. */
     parser_ignore_int(&dtype);
 
     /* Expect an identifier name for variable or function. */
     struct token *name_token = token_next();
     if (name_token->type != TOKEN_TYPE_IDENTIFIER)
     {
         compiler_error(current_process, "Expecting a valid name for the given variable declaration\n");
     }
 
     /* If next token is '(' then this is a function declaration/definition. */
     if (token_next_is_operator("("))
     {
         parse_function(&dtype, name_token, history);
         return;
     }
 
     /* Otherwise parse variable (arrays/initializer), and possibly multiple variables separated by commas. */
     parse_variable(&dtype, name_token, history);
     if (token_is_operator(token_peek_next(), ","))
     {
         struct vector *var_list = vector_create(sizeof(struct node *));
         /* Pop the variable we just parsed and add it to the list. */
         struct node *var_node = node_pop();
         vector_push(var_list, &var_node);
         while (token_is_operator(token_peek_next(), ","))
         {
             /* Consume comma and parse the next variable with the same base dtype. */
             token_next();
             name_token = token_next();
             parse_variable(&dtype, name_token, history);
             var_node = node_pop();
             vector_push(var_list, &var_node);
         }
 
         make_variable_list_node(var_list);
     }
 
     expect_sym(';');
 }
 
 /* Forward-declaration for parse_if_stmt used by else-if handling. */
 void parse_if_stmt(struct history *history);
 
 /**
  * @brief Parse an `else` block and return its ELSE node.
  *
  * Uses parse_body to parse the body of the else block and wraps it into
  * an ELSE AST node before returning it.
  *
  * @param history Parsing context.
  * @return Pointer to the created ELSE node.
  */
 struct node *parse_else(struct history *history)
 {
     size_t var_size = 0;
     parse_body(&var_size, history);
     struct node *body_node = node_pop();
     make_else_node(body_node);
     return node_pop();
 }
 
 /**
  * @brief Parse either 'else if' or 'else' and return the resulting node.
  *
  * If the next tokens form 'else if', delegates to parse_if_stmt and returns
  * the resulting IF node. Otherwise parses a plain else block.
  *
  * @param history Parsing context.
  * @return Node pointer for the parsed else/else-if (or NULL if no 'else').
  */
 struct node *parse_else_or_else_if(struct history *history)
 {
     struct node *node = NULL;
     if (token_next_is_keyword("else"))
     {
         /* consume "else" */
         token_next();
 
         if (token_next_is_keyword("if"))
         {
             /* This is an 'else if' branch; parse it (note: parse_if_stmt will consume the condition & body). */
             parse_if_stmt(history_down(history, 0));
             node = node_pop();
             return node;
         }
 
         /* Plain else */
         node = parse_else(history_down(history, 0));
     }
     return node;
 }
 
 /**
  * @brief Parse an 'if' statement, including optional else/else-if branches.
  *
  * Grammar outline:
  *   if '(' expression ')' body [ else-if | else ]
  *
  * @param history Parsing context.
  */
 void parse_if_stmt(struct history *history)
 {
     expect_keyword("if");
     expect_op("(");
 
     /* Parse condition expression. */
     parse_expressionable_root(history);
     expect_sym(')');
 
     struct node *cond_node = node_pop();
     size_t var_size = 0;
 
     /* Parse the if-body (either a single statement or a braced block). */
     parse_body(&var_size, history);
     struct node *body_node = node_pop();
 
     /* Create the if node and attach optional else/else-if. */
     make_if_node(cond_node, body_node, parse_else_or_else_if(history));
 }
 
 /**
  * @brief Parse a keyword that uses parentheses around an expression (e.g., switch, while, do-while condition).
  *
  * This helper consumes the keyword, the opening '(', parses the expression (with a fresh history),
  * then expects the closing ')'.
  *
  * @param keyword Name of the keyword to expect and parse (e.g., "switch", "while").
  */
 void parse_keyword_parentheses_expression(const char *keyword)
 {
     expect_keyword(keyword);
     expect_op("(");
     parse_expressionable_root(history_begin(0));
     expect_sym(')');
 }
 
 /**
  * @brief Parse a 'case' label within a switch and register it with the parser.
  *
  * Supports only numeric case expressions in this subset; non-numeric cases will emit a compiler error.
  *
  * @param history Parsing context (used for registering cases).
  */
 void parse_case(struct history *history)
 {
     expect_keyword("case");
     parse_expressionable_root(history);
     struct node *case_exp_node = node_pop();
     expect_sym(':');
     make_case_node(case_exp_node);
 
     if (case_exp_node->type != NODE_TYPE_NUMBER)
     {
         compiler_error(current_process, "We only support numbers in our subset of C at this time\n");
     }
 
     struct node *case_node = node_pop();
     parser_register_case(history, case_node);
 }
 
 /**
  * @brief Parse a switch statement with its cases and body.
  *
  * Registers a new switch context for case tracking, parses the controlling expression,
  * parses the switch body, then produces the SWITCH node with all collected cases.
  *
  * @param history Parsing context.
  */
 void parse_switch(struct history *history)
 {
     struct parser_history_switch _switch = parser_new_switch_statement(history);
     parse_keyword_parentheses_expression("switch");
     struct node *switch_exp_node = node_pop();
     size_t variable_size = 0;
     parse_body(&variable_size, history);
     struct node *body_node = node_pop();
 
     /* Make the switch node using accumulated case data. */
     make_switch_node(switch_exp_node, body_node, _switch.case_data.cases, _switch.case_data.has_default_case);
     parser_end_switch_statement(&_switch);
 }
 
 /**
  * @brief Parse a 'do-while' loop.
  *
  * Grammar: do body while (expr);
  *
  * @param history Parsing context.
  */
 void parse_do_while(struct history *history)
 {
     expect_keyword("do");
     size_t var_size = 0;
     parse_body(&var_size, history);
     struct node *body_node = node_pop();
 
     parse_keyword_parentheses_expression("while");
     struct node *exp_node = node_pop();
     expect_sym(';');
 
     make_do_while_node(body_node, exp_node);
 }
 
 /**
  * @brief Parse a 'while' loop: while (expr) body
  *
  * @param history Parsing context.
  */
 void parse_while(struct history *history)
 {
     parse_keyword_parentheses_expression("while");
     struct node *exp_node = node_pop();
     size_t variable_size = 0;
     parse_body(&variable_size, history);
     struct node *body_node = node_pop();
     make_while_node(exp_node, body_node);
 }
 
 /**
  * @brief Parse the initialization/condition part of a for-loop (the parts separated by semicolons).
  *
  * This helper parses either:
  *   - nothing (a semicolon immediately), or
  *   - an expression followed by a semicolon.
  *
  * @param history Parsing context.
  * @return true if an expression was parsed (and a node is available on the node stack), false if only ';' was found.
  */
 bool parse_for_loop_part(struct history *history)
 {
     if (token_next_is_symbol(';'))
     {
         /* nothing present, consume the semicolon */
         token_next();
         return false;
     }
 
     parse_expressionable_root(history);
     expect_sym(';');
     return true;
 }
 
 /**
  * @brief Parse the looping-expression part (last part) of a for-loop up to the closing ')'.
  *
  * If ')' is found immediately, returns false. Otherwise parses an expression and returns true.
  *
  * @param history Parsing context.
  * @return true if an expression was parsed, false if empty (')' encountered).
  */
 bool parse_for_loop_part_loop(struct history *history)
 {
     if (token_next_is_symbol(')'))
     {
         return false;
     }
 
     parse_expressionable_root(history);
     return true;
 }
 
 /**
  * @brief Parse a full 'for' statement: for (init; cond; loop) body
  *
  * Each component may be omitted per C grammar (empty init/cond/loop). The body is parsed
  * and an appropriate FOR node is created with pointers to the init, cond, loop and body nodes.
  *
  * @param history Parsing context.
  */
 void parse_for_stmt(struct history *history)
 {
     struct node *init_node = NULL;
     struct node *cond_node = NULL;
     struct node *loop_node = NULL;
     struct node *body_node = NULL;
 
     expect_keyword("for");
     expect_op("(");
 
     if (parse_for_loop_part(history))
     {
         init_node = node_pop();
     }
 
     if (parse_for_loop_part(history))
     {
         cond_node = node_pop();
     }
 
     if (parse_for_loop_part_loop(history))
     {
         loop_node = node_pop();
     }
 
     expect_sym(')');
 
     size_t variable_size = 0;
     parse_body(&variable_size, history);
     body_node = node_pop();
     make_for_node(init_node, cond_node, loop_node, body_node);
 }
 
 /**
  * @brief Parse a 'return' statement.
  *
  * Supports both `return;` (no value) and `return expr;`.
  *
  * @param history Parsing context.
  */
 void parse_return(struct history *history)
 {
     expect_keyword("return");
 
     /* No expression: 'return;' */
     if (token_next_is_symbol(';'))
     {
         expect_sym(';');
         make_return_node(NULL);
         return;
     }
 
     /* Expression-return: parse the expression and create the return node. */
     parse_expressionable_root(history);
     struct node *exp_node = node_pop();
     make_return_node(exp_node);
     expect_sym(';');
 }
 
 /**
  * @brief Parse a 'continue' statement and emit node.
  *
  * @param history Parsing context.
  */
 void parse_continue(struct history *history)
 {
     expect_keyword("continue");
     expect_sym(';');
     make_continue_node();
 }
 
 /**
  * @brief Parse a 'break' statement and emit node.
  *
  * @param history Parsing context.
  */
 void parse_break(struct history *history)
 {
     expect_keyword("break");
     expect_sym(';');
     make_break_node();
 }
 
 /**
  * @brief Parse a 'goto' statement.
  *
  * Syntax: goto identifier;
  * The identifier node is parsed and a GOTO node is created referencing it.
  *
  * @param history Parsing context.
  */
 void parse_goto(struct history *history)
 {
     expect_keyword("goto");
     parse_identifier(history_begin(0));
     expect_sym(';');
 
     struct node *label_node = node_pop();
     make_goto_node(label_node);
 }
 
 /**
  * @brief Parse a label: `identifier:`
  *
  * Expects that an identifier is already provided on the node stack (parse_identifier pushed it).
  * Validates that the popped node is an identifier and then creates a LABEL node.
  *
  * @param history Parsing context.
  */
 void parse_label(struct history *history)
 {
     expect_sym(':');
 
     struct node *label_name_node = node_pop();
     if (label_name_node->type != NODE_TYPE_IDENTIFIER)
     {
         compiler_error(current_process, "Expecting an identifier for labels something else was provided");
     }
 
     make_label_node(label_name_node);
 }
 
 /**
  * @brief Parse the ternary (?:) operator inside an expression.
  *
  * Expects the condition node is already on the node stack. Parses the 'true' and 'false'
  * expressionable parts, constructs a ternary node and rewires it into an expression node.
  *
  * @param history Parsing context.
  */
 void parse_for_tenary(struct history *history)
 {
     struct node *condition_node = node_pop();
     expect_op("?");
     parse_expressionable_root(history_down(history, HISTORY_FLAG_PARENTHESES_IS_NOT_A_FUNCTION_CALL));
     struct node *true_result_node = node_pop();
     expect_sym(':');
     parse_expressionable_root(history_down(history, HISTORY_FLAG_PARENTHESES_IS_NOT_A_FUNCTION_CALL));
     struct node *false_result_node = node_pop();
     make_tenary_node(true_result_node, false_result_node);
     struct node *tenary_node = node_pop();
     make_exp_node(condition_node, tenary_node, "?");
 }
 
 /**
  * @brief Parse a keyword token and dispatch to the appropriate parser function.
  *
  * Handles keywords that start declarations (types), control flow (if/for/while/do/switch),
  * and statements like return/break/continue/goto/case.
  *
  * @param history Parsing context.
  */
 void parse_keyword(struct history *history)
 {
     struct token *token = token_peek_next();
 
     /* If keyword is a type or variable modifier, handle declarations. */
     if (is_keyword_variable_modifier(token->sval) || keyword_is_datatype(token->sval))
     {
         parse_variable_function_or_struct_union(history);
         return;
     }
 
     if (S_EQ(token->sval, "break"))
     {
         parse_break(history);
         return;
     }
     else if (S_EQ(token->sval, "continue"))
     {
         parse_continue(history);
         return;
     }
     else if (S_EQ(token->sval, "return"))
     {
         parse_return(history);
         return;
     }
     else if (S_EQ(token->sval, "if"))
     {
         parse_if_stmt(history);
         return;
     }
     else if (S_EQ(token->sval, "for"))
     {
         parse_for_stmt(history);
         return;
     }
     else if (S_EQ(token->sval, "while"))
     {
         parse_while(history);
         return;
     }
     else if (S_EQ(token->sval, "do"))
     {
         parse_do_while(history);
         return;
     }
     else if (S_EQ(token->sval, "switch"))
     {
         parse_switch(history);
         return;
     }
     else if (S_EQ(token->sval, "goto"))
     {
         parse_goto(history);
         return;
     }
     else if (S_EQ(token->sval, "case"))
     {
         parse_case(history);
         return;
     }
 
     compiler_error(current_process, "Invalid keyword\n");
 }
 
 /**
  * @brief Parse a single expressionable token or construct.
  *
  * Attempts to parse a single token as an expressionable unit:
  * - number literal
  * - identifier (which may expand to calls, variables, etc.)
  * - operator/compound expression
  * - keyword (delegated to parse_keyword)
  *
  * Returns 0 if parsing was performed, -1 if there was no token to parse.
  *
  * @param history Parsing context (may be mutated: NODE_FLAG_INSIDE_EXPRESSION is set).
  * @return 0 on success (parsed one unit), -1 if no token available.
  */
 int parse_expressionable_single(struct history *history)
 {
     struct token *token = token_peek_next();
     if (!token)
     {
         return -1;
     }
 
     /* Mark that we're inside an expression so nested parsing can consult the flag. */
     history->flags |= NODE_FLAG_INSIDE_EXPRESSION;
     int res = -1;
     switch (token->type)
     {
     case TOKEN_TYPE_NUMBER:
         parse_single_token_to_node();
         res = 0;
         break;
 
     case TOKEN_TYPE_IDENTIFIER:
         parse_identifier(history);
         res = 0;
         break;
 
     case TOKEN_TYPE_OPERATOR:
         parse_exp(history);
         res = 0;
         break;
 
     case TOKEN_TYPE_KEYWORD:
         parse_keyword(history);
         res = 0;
         break;
     }
     return res;
 }
 
 /**
  * @brief Parse zero-or-more expressionable units until none remain.
  *
  * Calls parse_expressionable_single repeatedly while it returns 0.
  *
  * @param history Parsing context.
  */
 void parse_expressionable(struct history *history)
 {
     while (parse_expressionable_single(history) == 0)
     {
     }
 }
 
 /**
  * @brief Parse a keyword at global scope and push the resulting node.
  *
  * This helper is used when parsing top-level keywords: it delegates to parse_keyword
  * using a fresh global history and then pushes the created node back onto the stack.
  */
 void parse_keyword_for_global()
 {
     parse_keyword(history_begin(0));
     struct node *node = node_pop();
 
     node_push(node);
 }
 
 /**
  * @brief Parse the next global token and dispatch to appropriate handlers.
  *
  * Returns -1 if there is no next token, otherwise returns 0.
  */
 int parse_next()
 {
     struct token *token = token_peek_next();
     if (!token)
     {
         return -1;
     }
 
     int res = 0;
     switch (token->type)
     {
     case TOKEN_TYPE_NUMBER:
     case TOKEN_TYPE_IDENTIFIER:
     case TOKEN_TYPE_STRING:
         parse_expressionable(history_begin(0));
         break;
 
     case TOKEN_TYPE_KEYWORD:
         parse_keyword_for_global();
         break;
 
     case TOKEN_TYPE_SYMBOL:
         parse_symbol();
         break;
     }
     return 0;
 }
 
 /**
  * @brief Top-level parser entrypoint.
  *
  * Initializes parser state, iterates over tokens and builds the AST node tree.
  * After parsing, attempts to resolve pending fixups (e.g., unresolved struct types).
  *
  * @param process Compile process containing tokens and node vectors.
  * @return PARSE_ALL_OK on success (asserts that all fixups resolved).
  */
 int parse(struct compile_process *process)
 {
     scope_create_root(process);
     current_process = process;
     parser_last_token = NULL;
     node_set_vector(process->node_vec, process->node_tree_vec);
     parser_blank_node = node_create(&(struct node){.type = NODE_TYPE_BLANK});
     parser_fixup_sys = fixup_sys_new();
 
     struct node *node = NULL;
     vector_set_peek_pointer(process->token_vec, 0);
     while (parse_next() == 0)
     {
         node = node_peek();
         vector_push(process->node_tree_vec, &node);
     }
 
     assert(fixups_resolve(parser_fixup_sys));
     scope_free_root(process);
     return PARSE_ALL_OK;
 }
 