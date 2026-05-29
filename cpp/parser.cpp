/**
 * parser.cpp — C++17 refactor of the compiler frontend parser.
 *
 * This file transforms a token stream (produced by the lexer) into an abstract
 * syntax tree (AST).  All parser state that was previously module-global C
 * variables lives inside the `Parser` class.  `History` is a lightweight value
 * type passed by copy so that nested calls get a snapshot of the caller's
 * context without any heap allocation.
 *
 * Reading guide
 * -------------
 *   1. `History` helpers  — understand the context flags before anything else.
 *   2. Token accessors    — how we read from the token stream.
 *   3. Expression parsing — the heart of the parser; includes precedence logic.
 *   4. Type / declarator  — how C types are decoded.
 *   5. Statement / body   — control flow and block parsing.
 *   6. Top-level dispatch — ties it all together.
 *   7. `parse()`          — C entry point called by the driver.
 */

#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Defined in node.cpp; track the function / block we are currently inside.
extern struct node *parser_current_body;
extern struct node *parser_current_function;

// Operator precedence table defined in a separate translation unit.
extern struct expressionable_op_precedence_group op_precedence[TOTAL_OPERATOR_GROUPS];

// ===========================================================================
// § History — parsing context passed by value through recursive calls
// ===========================================================================

/**
 * Flags that describe the syntactic context the parser is currently in.
 * These are OR-ed together in History::flags.
 */
enum HistoryFlags : int
{
    HISTORY_INSIDE_UNION         = 0b00000001,
    HISTORY_UPWARD_STACK         = 0b00000010,
    HISTORY_GLOBAL_SCOPE         = 0b00000100,
    HISTORY_INSIDE_STRUCTURE     = 0b00001000,
    HISTORY_INSIDE_FUNCTION_BODY = 0b00010000,
    HISTORY_IN_SWITCH            = 0b00100000,
    HISTORY_PARENS_NOT_CALL      = 0b01000000,
};

/**
 * Lightweight context record threaded through every parse call.
 *
 * Because History is a plain struct copied by value, each recursive
 * descent frame has its own private copy.  No heap allocation needed.
 *
 * The switch-case bookkeeping (switch_cases / switch_has_default) is only
 * meaningful inside parse_switch(); it is nullptr everywhere else.  The
 * Parser class holds a pointer-to-pointer so parse_case() can register
 * itself without needing to carry the vector through History.
 */
struct History
{
    int flags = 0;

    // These two fields are only populated while parsing a switch body.
    struct vector *switch_cases      = nullptr;
    bool           switch_has_default = false;
};

// Creates a fresh History with the given flags and zeroed switch fields.
static History make_history(int flags)
{
    History h;
    h.flags = flags;
    return h;
}

// Returns a copy of h with flags replaced by the new value.
// All other fields (switch_cases, switch_has_default) are preserved as-is,
// allowing callers to change scope context without losing switch state.
static History history_down(History h, int flags)
{
    h.flags = flags;
    return h;
}

// ===========================================================================
// § ScopeEntity — one declared name tracked in the scope stack
// ===========================================================================

enum
{
    SCOPE_ENTITY_ON_STACK        = 0b00000001,
    SCOPE_ENTITY_STRUCTURE_SCOPE = 0b00000010,
};

/**
 * Represents a single name that has been declared and pushed into scope.
 * The scope stack is managed by the scope_* functions in scope.c.
 */
struct ScopeEntity
{
    int          flags        = 0;
    int          stack_offset = 0;
    struct node *node         = nullptr;
};

// ===========================================================================
// § StructFixupData — payload used when a struct type is forward-referenced
// ===========================================================================

/**
 * When a variable is declared with a struct type that has not been defined yet
 * (forward declaration), we register a fixup.  After the full parse we walk
 * all fixups and try to resolve the missing struct definition.
 */
struct StructFixupData
{
    struct compile_process *process;   // needed to call symresolver
    struct node            *var_node;  // the variable whose type needs patching
};

// ===========================================================================
// § Parser class — hand-written recursive-descent C parser
//
// Design overview
// ---------------
// The Parser drives a single-pass, recursive-descent parse of one C
// translation unit.  Token input is consumed from m_process->token_vec via
// next_token() / peek_token().  AST nodes are allocated by node_create() and
// pushed onto a global working stack; composite nodes are built by popping
// child nodes off that stack and passing them to make_*() factory helpers.
//
// Context is communicated through the History value type (see above).  Each
// recursive call frame receives its own copy of History so that entering a
// nested scope never silently mutates the parent's context.
//
// Operator precedence is enforced after the fact: binary expressions are
// parsed naively right-associatively, then reorder_expression() rotates the
// resulting tree to match the C precedence table stored in op_precedence[].
//
// Forward-referenced struct types are deferred using the fixup system: a
// StructFixupData record is registered in m_fixups and resolved by
// fixups_resolve() at the end of run().
// ===========================================================================

class Parser
{
public:
    explicit Parser(struct compile_process *process);
    int run(); // public entry: parse the entire file, return PARSE_ALL_OK

private:
    struct compile_process *m_process;
    struct fixup_system    *m_fixups;
    struct token           *m_last_token = nullptr;
    struct node            *m_blank_node = nullptr;

    // Points to the cases vector of the innermost switch being parsed.
    // nullptr when we are not inside a switch.
    struct vector **m_switch_cases_ptr = nullptr;

    // -----------------------------------------------------------------------
    // § 1 — Constructor helpers / run()
    // -----------------------------------------------------------------------
    void parse_all();

    // -----------------------------------------------------------------------
    // § 2 — Token access
    // -----------------------------------------------------------------------

    /**
     * Skip whitespace-equivalent tokens (newlines, comments, separators)
     * without advancing the main peek pointer.
     */
    void skip_nl_or_comment(struct token *tok);

    /**
     * Consume and return the next meaningful token.
     * Updates m_last_token and process->pos.
     */
    struct token *next_token();

    /** Peek at the next meaningful token without consuming it. */
    struct token *peek_token();

    bool peek_is_op(const char *op);
    bool peek_is_keyword(const char *keyword);
    bool peek_is_symbol(char c);

    void expect_op(const char *op);
    void expect_sym(char c);
    void expect_keyword(const char *keyword);

    // -----------------------------------------------------------------------
    // § 3 — Scope helpers
    // -----------------------------------------------------------------------
    ScopeEntity *new_scope_entity(struct node *node, int stack_offset, int flags);
    ScopeEntity *last_scope_entity();
    ScopeEntity *last_scope_entity_before_global();
    void         push_scope_entity(ScopeEntity *entity, size_t size);
    void         new_scope();    // push a new scope level
    void         finish_scope(); // pop the current scope level

    // -----------------------------------------------------------------------
    // § 4 — Operator precedence
    // -----------------------------------------------------------------------

    /**
     * Return the index of op in the global precedence table, and set
     * *group_out to point to that group.  Returns -1 if not found.
     */
    int  op_precedence_index(const char *op,
                             struct expressionable_op_precedence_group **group_out);

    /**
     * True if op_left should be evaluated before op_right.
     * Used to decide whether to rotate the AST.
     */
    bool left_has_priority(const char *op_left, const char *op_right);

    // -----------------------------------------------------------------------
    // § 5 — Expression tree reordering
    // -----------------------------------------------------------------------

    /**
     * Rotate the tree so that a higher-precedence left operator is evaluated
     * before a lower-precedence right operator.
     *
     * Example:  50 * (20 + 120)  →  (50 * 20) + 120
     * when '*' has higher precedence than '+'.
     *
     * Binary operator precedence is enforced by rotating the AST: when we
     * see E(left op E(right_left right_op right_right)), if op has higher
     * precedence than right_op we restructure to E(E(left op right_left)
     * right_op right_right).
     */
    void shift_children_left(struct node *node);

    /**
     * Used when the right child's left operand should become a complete
     * sub-expression with the current left node.
     */
    void move_right_left_to_left(struct node *node);

    /**
     * Walk the expression tree in-place and apply precedence rotations.
     */
    void reorder_expression(struct node **node_out);

    // -----------------------------------------------------------------------
    // § 6 — Expression parsing
    // -----------------------------------------------------------------------
    void parse_expressionable(History h);
    void parse_expressionable_root(History h);
    int  parse_expressionable_single(History h);

    // Dispatch: decide which kind of expression to parse based on the next
    // operator token.
    int  parse_exp(History h);

    // Parse a normal binary operation (not (, [, ?, ,).
    void parse_exp_normal(History h);

    // If the next token is an operator keep going (used after ( ) or [ ]).
    void parse_additional_exp();

    void parse_parenthesized_expression(History h);
    void parse_array_access(History h);
    void parse_comma_expression(History h);
    void parse_cast_expression();
    void parse_ternary_expression(History h);
    void parse_identifier(History h);
    void parse_single_token_to_node();

    // -----------------------------------------------------------------------
    // § 7 — Datatype parsing
    // -----------------------------------------------------------------------
    void parse_datatype(struct datatype *dtype);
    void parse_datatype_modifiers(struct datatype *dtype);
    void parse_datatype_type(struct datatype *dtype);

    void get_datatype_tokens(struct token **primary,
                             struct token **secondary);

    int  count_pointer_stars();

    int  datatype_expected_kind(const char *type_str);

    /**
     * Synthetic index used to generate unique names for anonymous struct/union
     * types.  Each call increments and returns a counter.
     */
    int  anon_type_index();

    /**
     * Build a freshly heap-allocated token whose sval is a unique synthetic
     * name like "customtypename_3".  Used for anonymous struct/union types.
     */
    struct token *build_anonymous_type_name();

    void init_primitive_type(struct token *primary, struct token *secondary,
                             struct datatype *out);

    void adjust_size_for_secondary(struct datatype *dtype,
                                   struct token *secondary);

    void init_datatype(struct token *primary, struct token *secondary,
                       struct datatype *out, int pointer_depth,
                       int expected_kind);

    size_t size_of_struct(const char *name);
    size_t size_of_union(const char *name);

    bool is_secondary_allowed(int expected_kind);
    bool is_secondary_allowed_for_type(const char *type_str);
    bool is_int_valid_after_datatype(struct datatype *dtype);

    void ignore_trailing_int(struct datatype *dtype);

    // -----------------------------------------------------------------------
    // § 8 — Array brackets
    // -----------------------------------------------------------------------
    struct array_brackets *parse_array_brackets(History h);

    // -----------------------------------------------------------------------
    // § 9 — Variable / function / struct / union
    // -----------------------------------------------------------------------
    void make_variable_node(struct datatype *dtype, struct token *name_token,
                            struct node *value_node);
    void register_variable(History h, struct datatype *dtype,
                           struct token *name_token,
                           struct node *value_node);
    void compute_variable_offset(struct node *var_node, History h);
    void compute_offset_for_stack(struct node *var_node, History h);
    void compute_offset_for_global(struct node *var_node, History h);
    void compute_offset_for_struct_field(struct node *var_node, History h);

    void parse_variable(struct datatype *dtype, struct token *name_token,
                        History h);
    void parse_variable_full(History h);

    void parse_function(struct datatype *ret_type, struct token *name_token,
                        History h);
    void parse_function_body(History h);
    struct vector *parse_function_arguments(History h);

    void parse_struct_body(History h);
    void parse_struct(struct datatype *dtype);
    void parse_struct_no_new_scope(struct datatype *dtype,
                                   bool is_forward_declaration);

    void parse_union_body(History h);
    void parse_union(struct datatype *dtype);
    void parse_union_no_scope(struct datatype *dtype,
                              bool is_forward_declaration);

    void parse_struct_or_union(struct datatype *dtype);

    void parse_variable_function_or_struct_union(History h);

    void token_read_dots(size_t amount);

    void make_variable_list_node(struct vector *var_list_vec);

    // -----------------------------------------------------------------------
    // § 10 — Body / size accounting
    // -----------------------------------------------------------------------
    void parse_body(size_t *variable_size, History h);
    void parse_body_single_statement(size_t *variable_size,
                                     struct vector *body_vec, History h);
    void parse_body_multiple_statements(size_t *variable_size,
                                        struct vector *body_vec, History h);
    void finalize_body(History h, struct node *body_node,
                       struct vector *body_vec, size_t *variable_size,
                       struct node *largest_align_eligible,
                       struct node *largest_possible);

    size_t node_variable_size(struct node *node);
    size_t variable_list_total_size(struct vector *vec, History h);
    void   struct_or_union_var_size(History h, size_t *accum,
                                    struct node *node);
    void   append_size_for_node(History h, size_t *accum, struct node *node);
    void   append_size_for_variable_list(History h, size_t *accum,
                                         struct vector *vec);

    // -----------------------------------------------------------------------
    // § 11 — Statements
    // -----------------------------------------------------------------------
    void parse_statement(History h);
    void parse_symbol_token(History h);
    void parse_label(History h);

    // -----------------------------------------------------------------------
    // § 12 — Control flow
    // -----------------------------------------------------------------------
    void          parse_if_stmt(History h);
    struct node  *parse_else_block(History h);
    struct node  *parse_else_or_else_if(History h);
    void          parse_while(History h);
    void          parse_do_while(History h);
    void          parse_for_stmt(History h);
    bool          parse_for_loop_part(History h);
    bool          parse_for_loop_part_loop(History h);
    void          parse_switch(History h);
    void          parse_case(History h);
    void          parse_keyword_with_parens_expr(const char *keyword);

    // -----------------------------------------------------------------------
    // § 13 — Other keyword statements
    // -----------------------------------------------------------------------
    void parse_return(History h);
    void parse_continue(History h);
    void parse_break(History h);
    void parse_goto(History h);

    // -----------------------------------------------------------------------
    // § 14 — Top-level dispatch
    // -----------------------------------------------------------------------
    void parse_keyword(History h);
    void parse_keyword_for_global();
    int  parse_next();

    // -----------------------------------------------------------------------
    // § 15 — Static helpers (no state needed)
    // -----------------------------------------------------------------------
    static bool is_variable_modifier_keyword(const char *val);

    // -----------------------------------------------------------------------
    // § 16 — Fixup callbacks (static, access state via fixup_private())
    // -----------------------------------------------------------------------
    static bool fix_struct_type(struct fixup *fixup);
    static void end_struct_type_fixup(struct fixup *fixup);
};

// ===========================================================================
// Constructor / run()
// ===========================================================================

// Initialises the Parser for the given compilation unit.
// Allocates a fresh fixup system to hold deferred struct-type resolutions.
Parser::Parser(struct compile_process *process)
    : m_process(process)
    , m_fixups(fixup_sys_new())
{
}

// Parses the entire translation unit represented by m_process.
// Sets up the root scope, allocates the blank-node sentinel, and drives the
// main parse loop.  After all tokens are consumed, attempts to resolve every
// registered fixup (forward-referenced struct types).  Frees the root scope
// and returns PARSE_ALL_OK on success; aborts via assert if any fixup cannot
// be resolved.
int Parser::run()
{
    scope_create_root(m_process);
    node_set_vector(m_process->node_vec, m_process->node_tree_vec);

    // The blank node is used as a placeholder when parentheses contain no
    // expression — it will never appear in the final tree.
    struct node blank{};
    blank.type  = NODE_TYPE_BLANK;
    m_blank_node = node_create(&blank);
    // node_create pushed it; remove from the live-node stack so it doesn't
    // pollute later pops.
    node_pop();
    // Push it back so it is heap-allocated and reusable as a sentinel.
    // (We keep the pointer; we do not re-push to the working stack.)

    m_last_token = nullptr;
    vector_set_peek_pointer(m_process->token_vec, 0);

    struct node *node = nullptr;
    while (parse_next() == 0)
    {
        node = node_peek();
        vector_push(m_process->node_tree_vec, &node);
    }

    assert(fixups_resolve(m_fixups));
    scope_free_root(m_process);
    return PARSE_ALL_OK;
}

// ===========================================================================
// § Token access
// ===========================================================================

// Advances the token vector past any whitespace-equivalent tokens (newlines,
// comments, newline-separator tokens) starting at tok.
// tok — the first token to inspect; subsequent peek calls pull the next one.
// Does not return a value; the caller re-peeks after the call.
void Parser::skip_nl_or_comment(struct token *tok)
{
    while (tok && token_is_nl_or_comment_or_newline_seperator(tok))
    {
        vector_peek(m_process->token_vec);
        tok = static_cast<struct token *>(
            vector_peek_no_increment(m_process->token_vec));
    }
}

// Consumes and returns the next meaningful token from the token stream,
// skipping over any whitespace or comment tokens first.
// As a side-effect, updates m_process->pos to the consumed token's source
// position and records the token in m_last_token for error reporting.
// Returns nullptr when the stream is exhausted.
struct token *Parser::next_token()
{
    struct token *tok =
        static_cast<struct token *>(
            vector_peek_no_increment(m_process->token_vec));
    skip_nl_or_comment(tok);
    tok = static_cast<struct token *>(
        vector_peek_no_increment(m_process->token_vec));
    if (tok)
        m_process->pos = tok->pos;
    m_last_token = tok;
    return static_cast<struct token *>(vector_peek(m_process->token_vec));
}

// Returns the next meaningful token without consuming it.
// Whitespace and comment tokens are skipped in-place so the returned token
// is always an actionable one (number, identifier, operator, keyword, symbol).
// Returns nullptr when the stream is exhausted.
struct token *Parser::peek_token()
{
    struct token *tok =
        static_cast<struct token *>(
            vector_peek_no_increment(m_process->token_vec));
    skip_nl_or_comment(tok);
    return static_cast<struct token *>(
        vector_peek_no_increment(m_process->token_vec));
}

// Returns true if the next token is an operator whose string value equals op.
// Does not consume the token.
bool Parser::peek_is_op(const char *op)
{
    return token_is_operator(peek_token(), op);
}

// Returns true if the next token is the keyword whose string value equals keyword.
// Does not consume the token.
bool Parser::peek_is_keyword(const char *keyword)
{
    return token_is_keyword(peek_token(), keyword);
}

// Returns true if the next token is a symbol character equal to c.
// Does not consume the token.
bool Parser::peek_is_symbol(char c)
{
    return token_is_symbol(peek_token(), c);
}

// Consumes the next token and asserts it is an operator equal to op.
// Calls compiler_error and does not return if the assertion fails.
void Parser::expect_op(const char *op)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_OPERATOR || !S_EQ(tok->sval, op))
        compiler_error(m_process,
            "Expected operator '%s' but got something else\n", op);
}

// Consumes the next token and asserts it is the symbol character c.
// Calls compiler_error and does not return if the assertion fails.
void Parser::expect_sym(char c)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_SYMBOL || tok->cval != c)
        compiler_error(m_process,
            "Expected symbol '%c' but got something else\n", c);
}

// Consumes the next token and asserts it is the keyword equal to keyword.
// Calls compiler_error and does not return if the assertion fails.
void Parser::expect_keyword(const char *keyword)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_KEYWORD || !S_EQ(tok->sval, keyword))
        compiler_error(m_process,
            "Expected keyword '%s' but got something else\n", keyword);
}

// ===========================================================================
// § Scope helpers
// ===========================================================================

// Allocates and initialises a new ScopeEntity for the given node.
// node         — the AST variable node being entered into scope.
// stack_offset — the byte offset relative to the frame pointer (negative
//                for downward-growing local variables, positive for arguments).
// flags        — bitfield of SCOPE_ENTITY_* values.
// Returns a heap-allocated ScopeEntity; ownership passes to the scope stack.
ScopeEntity *Parser::new_scope_entity(struct node *node, int stack_offset,
                                       int flags)
{
    ScopeEntity *entity =
        static_cast<ScopeEntity *>(calloc(1, sizeof(ScopeEntity)));
    entity->node         = node;
    entity->stack_offset = stack_offset;
    entity->flags        = flags;
    return entity;
}

// Returns the most-recently pushed ScopeEntity in the current scope chain,
// or nullptr if no entity has been pushed yet.
ScopeEntity *Parser::last_scope_entity()
{
    return static_cast<ScopeEntity *>(scope_last_entity(m_process));
}

// Returns the most-recently pushed ScopeEntity, stopping the search before
// the root (global) scope.  Used when computing local-variable offsets so
// that global declarations do not contaminate local layout.
// Returns nullptr if no local entity exists.
ScopeEntity *Parser::last_scope_entity_before_global()
{
    return static_cast<ScopeEntity *>(
        scope_last_entity_stop_at(m_process, m_process->scope.root));
}

// Pushes entity into the current scope, associating it with a storage size
// of size bytes.  The scope system uses size for alignment bookkeeping.
void Parser::push_scope_entity(ScopeEntity *entity, size_t size)
{
    scope_push(m_process, entity, size);
}

// Opens a new lexical scope level.  Called at the start of every function
// body, struct/union body, and compound statement block.
void Parser::new_scope()
{
    ::scope_new(m_process, 0);
}

// Closes the current lexical scope level.  Must be paired with new_scope().
void Parser::finish_scope()
{
    ::scope_finish(m_process);
}

// ===========================================================================
// § Operator precedence
// ===========================================================================

// Searches the global op_precedence[] table for the operator string op.
// op        — the operator string to look up (e.g. "+", "<<", "==").
// group_out — set to a pointer to the matching precedence group on success,
//             or nullptr when op is not found.
// Returns the group index (0 = highest precedence) on success, -1 if the
// operator is not in the table.
int Parser::op_precedence_index(const char *op,
    struct expressionable_op_precedence_group **group_out)
{
    *group_out = nullptr;
    for (int i = 0; i < TOTAL_OPERATOR_GROUPS; i++)
    {
        for (int b = 0; op_precedence[i].operators[b]; b++)
        {
            if (S_EQ(op, op_precedence[i].operators[b]))
            {
                *group_out = &op_precedence[i];
                return i;
            }
        }
    }
    return -1;
}

// Returns true if op_left should bind its operands before op_right does.
// This is the core test used by reorder_expression() to decide whether a
// tree rotation is needed.
//
// Rules:
//   - Identical operators: no priority (preserves right-associativity of the
//     naive parse, which is correct for assignment chains like a=b=c).
//   - Right-to-left associative operators (e.g. assignment): left never has
//     priority, so the right subtree is evaluated first as expected.
//   - Otherwise: a lower index in op_precedence[] means higher precedence, so
//     op_left has priority when its index is less than or equal to op_right's.
bool Parser::left_has_priority(const char *op_left, const char *op_right)
{
    // Identical operators: respect associativity, not precedence distance.
    if (S_EQ(op_left, op_right))
        return false;

    struct expressionable_op_precedence_group *group_left  = nullptr;
    struct expressionable_op_precedence_group *group_right = nullptr;
    int prec_left  = op_precedence_index(op_left,  &group_left);
    int prec_right = op_precedence_index(op_right, &group_right);

    if (group_left && group_left->associtivity == ASSOCIATIVITY_RIGHT_TO_LEFT)
        return false;

    // Lower index means higher precedence in our table.
    return prec_left <= prec_right;
}

// ===========================================================================
// § Expression tree reordering
// ===========================================================================

// Performs a single left rotation on a binary expression node.
// Before the call the tree looks like:
//   node = E(left  op  E(rl  right_op  rr))
// After the call it becomes:
//   node = E(E(left  op  rl)  right_op  rr)
//
// This is used when the outer operator (op) has higher precedence than the
// inner right operator (right_op) — we group the tighter sub-expression
// E(left op rl) first.
//
// node — must be a NODE_TYPE_EXPRESSION whose right child is also an
//         expression.  Asserts both conditions; the call is a no-op otherwise.
// Side effect: pushes a new expression node for E(left op rl) via
// make_exp_node, then immediately pops it and wires it into node.
void Parser::shift_children_left(struct node *node)
{
    // node is  E(left op E(rl right_op rr))
    // We want  E(E(left op rl) right_op rr)
    assert(node->type == NODE_TYPE_EXPRESSION);
    assert(node->exp.right->type == NODE_TYPE_EXPRESSION);

    const char  *right_op    = node->exp.right->exp.op;
    struct node *new_el      = node->exp.left;
    struct node *new_er      = node->exp.right->exp.left;

    make_exp_node(new_el, new_er, node->exp.op);
    struct node *new_left_sub = node_pop();
    struct node *new_right    = node->exp.right->exp.right;

    node->exp.left  = new_left_sub;
    node->exp.right = new_right;
    node->exp.op    = right_op;
}

// Combines node's left child and the left grandchild of the right subtree
// into a single completed sub-expression, then promotes the right subtree.
//
// Before:  node = E(left  op  E(rl  right_op  rr))
// After:   node = E(E(left op rl)  right_op  rr)
//
// Compared with shift_children_left this helper is used in structural fixup
// cases (array subscript followed by assignment, or call followed by comma)
// where the standard precedence rotation does not apply but the tree shape
// still needs adjusting for correct codegen.
void Parser::move_right_left_to_left(struct node *node)
{
    // node is  E(left op E(rl right_op rr))
    // Form a completed sub-expression E(left op rl) and make it the new left.
    make_exp_node(node->exp.left, node->exp.right->exp.left, node->exp.op);
    struct node *completed = node_pop();

    const char *new_op  = node->exp.right->exp.op;
    node->exp.left  = completed;
    node->exp.right = node->exp.right->exp.right;
    node->exp.op    = new_op;
}

// Walks an expression tree top-down and applies precedence-based rotations
// so that operators with higher precedence bind their operands more tightly.
//
// Algorithm overview
// ------------------
// The naive recursive-descent parser always builds right-heavy trees.  For
// "a + b * c" it produces E(a + E(b * c)).  That is actually correct because
// '*' has higher precedence (lower index) than '+', so no rotation is needed.
// For "a * b + c" it produces E(a * E(b + c)).  Here '*' (higher precedence)
// is the outer operator, but '+' (lower precedence) is inside the right
// subtree — the tree shape is wrong.  We must rotate it to E(E(a * b) + c).
//
// Step 1: if the left child is a plain value and the right child is an
// expression E(rl right_op rr), and if left_has_priority(op, right_op) is
// true, call shift_children_left to rotate, then recurse on both subtrees.
//
// Step 2: structural fixups — certain combinator patterns (array subscript
// followed by assignment, or a function call followed by a comma expression)
// always need the left-promote shape regardless of precedence; those are
// handled by move_right_left_to_left.
//
// node_out — in/out pointer to the root of the subtree to reorder.  The
//            pointer itself is not changed (the tree is mutated in-place),
//            but *node_out may conceptually represent a different root after
//            recursive calls.
void Parser::reorder_expression(struct node **node_out)
{
    struct node *node = *node_out;
    if (node->type != NODE_TYPE_EXPRESSION)
        return;

    // Nothing to reorder if neither child is itself an expression.
    if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
        node->exp.right &&
        node->exp.right->type != NODE_TYPE_EXPRESSION)
        return;

    // Case: left is a simple value, right is an expression subtree.
    // Example: 50 * E(20 + 120) — rotate if '*' binds tighter than '+'.
    if (node->exp.left->type != NODE_TYPE_EXPRESSION &&
        node->exp.right &&
        node->exp.right->type == NODE_TYPE_EXPRESSION)
    {
        const char *right_op = node->exp.right->exp.op;
        if (left_has_priority(node->exp.op, right_op))
        {
            shift_children_left(node);
            reorder_expression(&node->exp.left);
            reorder_expression(&node->exp.right);
        }
    }

    // Structural fixups: array access, assignments, and comma after call.
    if ((is_array_node(node->exp.left) || is_node_assignment(node->exp.right)) ||
        (node_is_expression(node->exp.left, "()") &&
         node_is_expression(node->exp.right, ",")))
    {
        move_right_left_to_left(node);
    }
}

// ===========================================================================
// § Expression parsing
// ===========================================================================

// Repeatedly calls parse_expressionable_single until it signals that no more
// expression units are available (returns non-zero).  Each successful call
// pushes a node or expression onto the working stack.
// h — current parsing context; the INSIDE_EXPRESSION flag is set inside
//     parse_expressionable_single before dispatching.
void Parser::parse_expressionable(History h)
{
    while (parse_expressionable_single(h) == 0)
        ; // keep consuming expressionable units
}

// Parses a complete expression, normalises the working stack, and leaves
// exactly one result node on top of the stack.
// The normalisation step (pop then push) ensures that any intermediate nodes
// pushed by sub-calls are reduced to a single root before returning.
// h — parsing context forwarded to parse_expressionable.
void Parser::parse_expressionable_root(History h)
{
    // Parse one complete expression, then normalise the stack:
    // pop the top node and push it back so it sits cleanly as a result.
    parse_expressionable(h);
    struct node *result = node_pop();
    node_push(result);
}

// Attempts to parse a single expressionable unit (a number, identifier,
// operator expression, or keyword expression) based on the next token type.
// Sets the INSIDE_EXPRESSION flag in h before dispatching so that sub-parsers
// know they are inside an expression context.
// h — parsing context; the INSIDE_EXPRESSION flag is OR-ed in before use.
// Returns 0 if a unit was successfully parsed and pushed, -1 if the next
// token cannot start an expression (signals the caller to stop looping).
int Parser::parse_expressionable_single(History h)
{
    struct token *tok = peek_token();
    if (!tok)
        return -1;

    // Signal that we are inside an expression so sub-parsers can consult this.
    h.flags |= NODE_FLAG_INSIDE_EXPRESSION;

    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
        parse_single_token_to_node();
        return 0;

    case TOKEN_TYPE_IDENTIFIER:
        parse_identifier(h);
        return 0;

    case TOKEN_TYPE_OPERATOR:
        parse_exp(h);
        return 0;

    case TOKEN_TYPE_KEYWORD:
        parse_keyword(h);
        return 0;

    default:
        return -1;
    }
}

// Consumes the current token (which must be a number, identifier, or string
// literal) and creates the corresponding leaf AST node, pushing it onto the
// working stack.
// NODE_TYPE_NUMBER   — node.llnum is set from tok->llnum.
// NODE_TYPE_IDENTIFIER — node.sval is set from tok->sval.
// NODE_TYPE_STRING   — node.sval is set from tok->sval.
// Calls compiler_error if the token type is none of the above.
void Parser::parse_single_token_to_node()
{
    struct token *tok = next_token();
    struct node  *n   = nullptr;
    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
    {
        struct node tmp{};
        tmp.type  = NODE_TYPE_NUMBER;
        tmp.llnum = tok->llnum;
        n = node_create(&tmp);
        break;
    }
    case TOKEN_TYPE_IDENTIFIER:
    {
        struct node tmp{};
        tmp.type = NODE_TYPE_IDENTIFIER;
        tmp.sval = tok->sval;
        n = node_create(&tmp);
        break;
    }
    case TOKEN_TYPE_STRING:
    {
        struct node tmp{};
        tmp.type = NODE_TYPE_STRING;
        tmp.sval = tok->sval;
        n = node_create(&tmp);
        break;
    }
    default:
        compiler_error(m_process,
            "Token type cannot be converted to a single AST node\n");
    }
    (void)n;
}

// Parses a single identifier token and pushes a NODE_TYPE_IDENTIFIER node.
// Asserts that the current peek token is indeed an identifier before
// delegating to parse_single_token_to_node.
// h — parsing context (currently unused but kept for interface uniformity).
void Parser::parse_identifier(History h)
{
    assert(peek_token()->type == TOKEN_TYPE_IDENTIFIER);
    parse_single_token_to_node();
}

// Dispatches operator-led expression parsing based on the operator character.
// The next token must be an operator.  Routes to the appropriate sub-parser:
//   "("  — parenthesised expression or function call (parse_parenthesized_expression)
//   "["  — array subscript (parse_array_access)
//   "?"  — ternary conditional (parse_ternary_expression)
//   ","  — comma expression (parse_comma_expression)
//   else — normal binary operator (parse_exp_normal)
// h — parsing context forwarded to all sub-parsers.
// Always returns 0.
int Parser::parse_exp(History h)
{
    struct token *tok = peek_token();

    if (S_EQ(tok->sval, "("))
    {
        parse_parenthesized_expression(h);
    }
    else if (S_EQ(tok->sval, "["))
    {
        parse_array_access(h);
    }
    else if (S_EQ(tok->sval, "?"))
    {
        parse_ternary_expression(h);
    }
    else if (S_EQ(tok->sval, ","))
    {
        parse_comma_expression(h);
    }
    else
    {
        parse_exp_normal(h);
    }
    return 0;
}

// Parses a standard binary operation of the form: <left> <op> <right>.
//
// Algorithm
// ---------
// At entry the left operand node is expected to already be on the working
// stack (pushed there by the previous call in the parse_expressionable loop).
// This method peeks the operator, verifies a left operand exists, consumes
// the operator, pops the left operand, parses the right-hand side recursively
// via parse_expressionable, then calls make_exp_node to create a binary
// expression node.  Finally, reorder_expression is applied to the new node to
// enforce correct operator precedence before pushing the result.
//
// h — parsing context; h.flags is forwarded unchanged to the RHS parse.
// Side effects: pops the left operand node, pushes the completed binary
// expression node (after any necessary precedence rotation).
void Parser::parse_exp_normal(History h)
{
    // Peek the operator but do not consume it yet — we need to check that
    // there is actually a left operand on the stack.
    struct token *op_tok  = peek_token();
    const char   *op      = op_tok->sval;
    struct node  *node_left = node_peek_expressionable_or_null();
    if (!node_left)
        return;

    // Consume the operator token.
    next_token();

    // Remove the left node from the working stack; it will be combined below.
    node_pop();
    node_left->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    // Parse the right-hand side, then reclaim it.
    parse_expressionable(history_down(h, h.flags));
    struct node *node_right = node_pop();
    node_right->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    // Build the binary expression node.
    make_exp_node(node_left, node_right, op);
    struct node *exp_node = node_pop();

    // Enforce operator precedence by rotating the tree.
    reorder_expression(&exp_node);
    node_push(exp_node);
}

// After parsing a sub-expression delimited by ')' or ']', continues parsing
// if the immediately following token is an operator.  This handles patterns
// like "(a + b) * c" where the multiplication follows the closing paren.
// Uses a fresh history with no flags so that the additional expression does
// not inherit any special context from the outer call.
void Parser::parse_additional_exp()
{
    // After parsing a sub-expression (e.g. after ')' or ']'), if the very
    // next token is an operator we continue expression parsing.
    if (peek_token()->type == TOKEN_TYPE_OPERATOR)
        parse_expressionable(make_history(0));
}

// Parses either a cast expression "(type)operand", a function call
// "callee(args)", or a plain grouped expression "(expr)".
//
// Consumes the opening '(' first.  Then:
//   - If the next token is a keyword, delegates to parse_cast_expression
//     (which expects the type and closing ')' itself).
//   - Otherwise checks whether a value node sits on the stack just before
//     the '(' — if so, the expression is a function call: the callee node
//     is popped, the argument list is parsed, and a call node
//     E(callee, "()"  args_node) is built.
//   - If the content is empty (immediately followed by ')'), uses the blank
//     sentinel node so no real child is attached.
// After the ')' is consumed, calls parse_additional_exp to keep going if an
// operator immediately follows.
// h — parsing context forwarded to inner expression parsing.
void Parser::parse_parenthesized_expression(History h)
{
    expect_op("(");

    // If the next token is a keyword, this is a cast expression: (int)x
    if (peek_token()->type == TOKEN_TYPE_KEYWORD)
    {
        parse_cast_expression();
        return;
    }

    // Check whether a value node sits just before the '(' — if so this is a
    // function call: foo(args).
    struct node *left_node = nullptr;
    struct node *tmp_node  = node_peek_or_null();
    if (tmp_node && node_is_value_type(tmp_node))
    {
        left_node = tmp_node;
        node_pop();
    }

    // Parse the contents of the parentheses (may be empty).
    struct node *exp_node = m_blank_node;
    if (!peek_is_symbol(')'))
    {
        parse_expressionable_root(make_history(0));
        exp_node = node_pop();
    }
    expect_sym(')');

    make_exp_parentheses_node(exp_node);

    if (left_node)
    {
        // Combine: left_node ( inner_exp )
        struct node *parens_node = node_pop();
        make_exp_node(left_node, parens_node, "()");
    }

    parse_additional_exp();
}

// Parses an array subscript expression of the form: <array>[<index>].
// Expects the opening '[' as the current next token.
// If a value node is already on the working stack (the array base), pops it
// to use as the left operand, parses the index expression inside the brackets,
// wraps the index in a bracket node, then recombines as E(base "[]" bracket).
// If no base is on the stack the bracket node is left as a standalone result.
// h — parsing context forwarded to the index expression parse.
void Parser::parse_array_access(History h)
{
    struct node *left_node = node_peek_or_null();
    if (left_node)
        node_pop();

    expect_op("[");
    parse_expressionable_root(h);
    expect_sym(']');

    struct node *exp_node = node_pop();
    make_bracket_node(exp_node);

    if (left_node)
    {
        struct node *bracket_node = node_pop();
        make_exp_node(left_node, bracket_node, "[]");
    }
}

// Parses a comma expression of the form: <left>, <right>.
// Consumes the ',' operator token, pops the left operand already on the
// stack, parses the right operand, and pushes a binary expression node with
// op = ",".
// h — parsing context forwarded to the right-hand parse.
void Parser::parse_comma_expression(History h)
{
    // Consume the ',' operator.
    next_token();
    struct node *left_node = node_pop();
    parse_expressionable_root(h);
    struct node *right_node = node_pop();
    make_exp_node(left_node, right_node, ",");
}

// Parses a C-style cast expression: (type) operand.
// Assumes the opening '(' has already been consumed by the caller.
// Reads the datatype, consumes the closing ')', parses the operand expression,
// then calls make_cast_node to create a cast AST node and push it.
void Parser::parse_cast_expression()
{
    // The '(' was already consumed by parse_parenthesized_expression.
    // We just need the type and the ')'.
    struct datatype dtype{};
    parse_datatype(&dtype);
    expect_sym(')');

    parse_expressionable(make_history(0));
    struct node *operand = node_pop();
    make_cast_node(&dtype, operand);
}

// Parses a ternary conditional expression: <condition> ? <true> : <false>.
// Assumes the condition node is already on the working stack when called.
// Pops the condition, consumes '?', parses both arms with the
// PARENS_NOT_CALL flag so that parenthesised sub-expressions inside the arms
// are not misidentified as function calls, then builds a ternary node and
// wraps it in E(cond "?" ternary_node).
// h — outer parsing context; PARENS_NOT_CALL is overlaid for the arm parses.
void Parser::parse_ternary_expression(History h)
{
    // The condition node is already on the working stack.
    struct node *cond_node = node_pop();
    expect_op("?");

    // Parse true and false arms with PARENS_NOT_CALL so that parenthesised
    // sub-expressions are not mistaken for function calls.
    parse_expressionable_root(
        history_down(h, HISTORY_PARENS_NOT_CALL));
    struct node *true_node = node_pop();

    expect_sym(':');

    parse_expressionable_root(
        history_down(h, HISTORY_PARENS_NOT_CALL));
    struct node *false_node = node_pop();

    make_tenary_node(true_node, false_node);
    struct node *ternary_node = node_pop();
    make_exp_node(cond_node, ternary_node, "?");
}

// ===========================================================================
// § Datatype parsing
// ===========================================================================

// Returns true if val is a storage-class or type-qualifier keyword that may
// appear before (or after) the main type keyword in a C declaration.
// Recognised keywords: unsigned, signed, static, const, extern,
//                      __ignore_typecheck__ (compiler extension).
bool Parser::is_variable_modifier_keyword(const char *val)
{
    return S_EQ(val, "unsigned") ||
           S_EQ(val, "signed")   ||
           S_EQ(val, "static")   ||
           S_EQ(val, "const")    ||
           S_EQ(val, "extern")   ||
           S_EQ(val, "__ignore_typecheck__");
}

// Consumes all leading modifier keywords (signed, unsigned, static, const,
// extern, __ignore_typecheck__) and sets the corresponding flags on dtype.
// Stops as soon as a non-modifier keyword or a non-keyword token is seen.
// dtype — the datatype record to update; only the flags field is modified.
void Parser::parse_datatype_modifiers(struct datatype *dtype)
{
    struct token *tok = peek_token();
    while (tok && tok->type == TOKEN_TYPE_KEYWORD)
    {
        if (!is_variable_modifier_keyword(tok->sval))
            break;

        if (S_EQ(tok->sval, "signed"))
            dtype->flags |= DATATYPE_FLAG_IS_SIGNED;
        else if (S_EQ(tok->sval, "unsigned"))
            dtype->flags &= ~DATATYPE_FLAG_IS_SIGNED;
        else if (S_EQ(tok->sval, "static"))
            dtype->flags |= DATATYPE_FLAG_IS_STATIC;
        else if (S_EQ(tok->sval, "const"))
            dtype->flags |= DATATYPE_FLAG_IS_CONST;
        else if (S_EQ(tok->sval, "extern"))
            dtype->flags |= DATATYPE_FLAG_IS_EXTERN;
        else if (S_EQ(tok->sval, "__ignore_typecheck__"))
            dtype->flags |= DATATYPE_FLAG_IGNORE_TYPE_CHECKING;

        next_token();
        tok = peek_token();
    }
}

// Reads the primary type token and, if applicable, the secondary type token.
// primary   — receives the first type keyword or identifier (e.g. "int",
//             "long", "struct").
// secondary — receives a second primitive keyword when one immediately
//             follows (e.g. the "int" in "long int"), or nullptr otherwise.
// Both tokens are consumed from the stream.
void Parser::get_datatype_tokens(struct token **primary,
                                  struct token **secondary)
{
    *primary   = next_token();
    *secondary = nullptr;
    struct token *nxt = peek_token();
    if (token_is_primitive_keyword(nxt))
    {
        *secondary = nxt;
        next_token();
    }
}

// Maps a type keyword string to the expected kind constant used by init_datatype.
// "union"  -> DATA_TYPE_EXPECT_UNION
// "struct" -> DATA_TYPE_EXPECT_STRUCT
// anything else -> DATA_TYPE_EXPECT_PRIMITIVE
int Parser::datatype_expected_kind(const char *type_str)
{
    if (S_EQ(type_str, "union"))  return DATA_TYPE_EXPECT_UNION;
    if (S_EQ(type_str, "struct")) return DATA_TYPE_EXPECT_STRUCT;
    return DATA_TYPE_EXPECT_PRIMITIVE;
}

// Returns a monotonically increasing counter value, starting at 1.
// Each call returns the next integer; used to generate unique suffixes for
// anonymous struct/union type names.
int Parser::anon_type_index()
{
    static int counter = 0;
    return ++counter;
}

// Allocates and returns a synthetic identifier token whose sval is a unique
// name of the form "customtypename_N", where N is the next value returned by
// anon_type_index().  Used to give anonymous struct/union types an internal
// name so they can be stored in the symbol table.
// The returned token and its sval string are heap-allocated; ownership passes
// to the caller (typically stored in dtype->type_str).
struct token *Parser::build_anonymous_type_name()
{
    char buf[32];
    snprintf(buf, sizeof(buf), "customtypename_%i", anon_type_index());
    char *sval = static_cast<char *>(malloc(sizeof(buf)));
    strncpy(sval, buf, sizeof(buf));

    struct token *tok = static_cast<struct token *>(
        calloc(1, sizeof(struct token)));
    tok->type = TOKEN_TYPE_IDENTIFIER;
    tok->sval = sval;
    return tok;
}

// Consumes consecutive '*' operator tokens and returns the pointer depth.
// For "int **p" the call sees "**" and returns 2, advancing past both stars.
// Returns 0 if the next token is not '*'.
int Parser::count_pointer_stars()
{
    int depth = 0;
    while (peek_is_op("*"))
    {
        depth++;
        next_token();
    }
    return depth;
}

// Returns true if a secondary type token is syntactically valid for the given
// expected kind.  Secondary tokens are only allowed for primitive types (e.g.
// "long int", "unsigned short"); struct and union cannot have secondaries.
bool Parser::is_secondary_allowed(int expected_kind)
{
    return expected_kind == DATA_TYPE_EXPECT_PRIMITIVE;
}

// Returns true if the primary type named type_str permits a following secondary
// primitive keyword.  Only long, short, double, and float may have one.
bool Parser::is_secondary_allowed_for_type(const char *type_str)
{
    return S_EQ(type_str, "long")   ||
           S_EQ(type_str, "short")  ||
           S_EQ(type_str, "double") ||
           S_EQ(type_str, "float");
}

// Applies the size contribution of a secondary type token to dtype.
// For "long double", the secondary "double" adds its own size to dtype->size
// and the combined type is flagged with DATATYPE_FLAG_IS_SECONDARY.
// If secondary is nullptr the function is a no-op.
// dtype     — the primary datatype record to update.
// secondary — the secondary token (e.g. "int" after "long"), or nullptr.
void Parser::adjust_size_for_secondary(struct datatype *dtype,
                                        struct token *secondary)
{
    if (!secondary)
        return;

    struct datatype *sec_dtype =
        static_cast<struct datatype *>(calloc(1, sizeof(struct datatype)));
    init_primitive_type(secondary, nullptr, sec_dtype);
    dtype->size     += sec_dtype->size;
    dtype->secondary = sec_dtype;
    dtype->flags    |= DATATYPE_FLAG_IS_SECONDARY;
}

// Fills in the type, size, and optional secondary of out based on the primary
// (and optional secondary) token.  Only handles primitive C types.
// primary   — the main type keyword token (void, char, short, int, long, etc.).
// secondary — a following primitive keyword token, or nullptr.
// out       — the datatype record to initialise.
// Calls compiler_error if primary->sval is not a recognised primitive keyword,
// or if secondary is non-null for a type that does not allow it.
void Parser::init_primitive_type(struct token *primary, struct token *secondary,
                                  struct datatype *out)
{
    if (!is_secondary_allowed_for_type(primary->sval) && secondary)
    {
        compiler_error(m_process,
            "Secondary datatype not allowed for '%s'\n", primary->sval);
    }

    if (S_EQ(primary->sval, "void"))
    {
        out->type = DATA_TYPE_VOID;
        out->size = DATA_SIZE_ZERO;
    }
    else if (S_EQ(primary->sval, "char"))
    {
        out->type = DATA_TYPE_CHAR;
        out->size = DATA_SIZE_BYTE;
    }
    else if (S_EQ(primary->sval, "short"))
    {
        out->type = DATA_TYPE_SHORT;
        out->size = DATA_SIZE_WORD;
    }
    else if (S_EQ(primary->sval, "int"))
    {
        out->type = DATA_TYPE_INTEGER;
        out->size = DATA_SIZE_DWORD;
    }
    else if (S_EQ(primary->sval, "long"))
    {
        out->type = DATA_TYPE_LONG;
        out->size = DATA_SIZE_DWORD;
    }
    else if (S_EQ(primary->sval, "float"))
    {
        out->type = DATA_TYPE_FLOAT;
        out->size = DATA_SIZE_DWORD;
    }
    else if (S_EQ(primary->sval, "double"))
    {
        out->type = DATA_TYPE_DOUBLE;
        out->size = DATA_SIZE_DWORD;
    }
    else
    {
        compiler_error(m_process, "Invalid primitive datatype\n");
    }

    adjust_size_for_secondary(out, secondary);
}

// Looks up the struct named name in the symbol table and returns its body size.
// Returns 0 if the symbol cannot be found (typically a forward declaration that
// will be resolved by the fixup system later).
// Asserts that the symbol is a NODE_TYPE_STRUCT node.
size_t Parser::size_of_struct(const char *name)
{
    struct symbol *sym = symresolver_get_symbol(m_process, name);
    if (!sym)
        return 0;
    assert(sym->type == SYMBOL_TYPE_NODE);
    struct node *n = static_cast<struct node *>(sym->data);
    assert(n->type == NODE_TYPE_STRUCT);
    return n->_struct.body_n->body.size;
}

// Looks up the union named name in the symbol table and returns its body size.
// Returns 0 if the symbol cannot be found.
// Asserts that the symbol is a NODE_TYPE_UNION node.
size_t Parser::size_of_union(const char *name)
{
    struct symbol *sym = symresolver_get_symbol(m_process, name);
    if (!sym)
        return 0;
    assert(sym->type == SYMBOL_TYPE_NODE);
    struct node *n = static_cast<struct node *>(sym->data);
    assert(n->type == NODE_TYPE_UNION);
    return n->_union.body_n->body.size;
}

// Fully initialises a datatype record from parsed token information.
// primary       — the resolved type name token (after struct/union keyword is
//                 stripped, or the primitive keyword itself).
// secondary     — optional second primitive keyword (e.g. "int" in "long int").
// out           — the datatype record to fill in.
// pointer_depth — number of leading '*' tokens already consumed; stored in
//                 out->type (indirectly via the type flags, set by callers).
// expected_kind — DATA_TYPE_EXPECT_PRIMITIVE, DATA_TYPE_EXPECT_STRUCT, or
//                 DATA_TYPE_EXPECT_UNION; controls which init branch is taken.
// Issues a compiler_warning and clamps to 32-bit for "long long" since the
// compiler does not support 64-bit integers natively.
void Parser::init_datatype(struct token *primary, struct token *secondary,
                            struct datatype *out, int pointer_depth,
                            int expected_kind)
{
    if (!is_secondary_allowed(expected_kind) && secondary)
    {
        compiler_error(m_process, "Invalid secondary datatype\n");
    }

    switch (expected_kind)
    {
    case DATA_TYPE_EXPECT_PRIMITIVE:
        init_primitive_type(primary, secondary, out);
        break;

    case DATA_TYPE_EXPECT_STRUCT:
        out->type        = DATA_TYPE_STRUCT;
        out->size        = size_of_struct(primary->sval);
        out->struct_node = struct_node_for_name(m_process, primary->sval);
        break;

    case DATA_TYPE_EXPECT_UNION:
        out->type        = DATA_TYPE_UNION;
        out->size        = size_of_union(primary->sval);
        out->struct_node = union_node_for_name(m_process, primary->sval);
        break;

    default:
        compiler_error(m_process, "Unsupported datatype expectation\n");
    }

    out->type_str = primary->sval;

    // "long long" is not natively supported; warn and clamp to 32 bits.
    if (S_EQ(primary->sval, "long") &&
        secondary && S_EQ(secondary->sval, "long"))
    {
        compiler_warning(m_process,
            "long long is not supported; defaulting to 32-bit long\n");
        out->size = DATA_SIZE_DWORD;
    }
}

// Parses the main type portion of a declaration (after any leading modifiers).
// Reads the primary (and optional secondary) tokens, determines whether the
// type is a struct, union, or primitive, resolves anonymous struct/union names,
// counts trailing pointer stars, and calls init_datatype to fill in dtype.
// dtype — output record; type, size, type_str, struct_node, and flags are set.
void Parser::parse_datatype_type(struct datatype *dtype)
{
    struct token *primary   = nullptr;
    struct token *secondary = nullptr;
    get_datatype_tokens(&primary, &secondary);

    int expected_kind = datatype_expected_kind(primary->sval);

    if (datatype_is_struct_or_union_for_name(primary->sval))
    {
        // The actual type name follows the keyword "struct" / "union".
        if (peek_token()->type == TOKEN_TYPE_IDENTIFIER)
        {
            primary = next_token();
        }
        else
        {
            // Anonymous struct/union — synthesise a unique name.
            primary = build_anonymous_type_name();
            dtype->flags |= DATATYPE_FLAG_STRUCT_UNION_NO_NAME;
        }
    }

    int pointer_depth = count_pointer_stars();
    init_datatype(primary, secondary, dtype, pointer_depth, expected_kind);
}

// Parses a complete C type specifier, including leading and trailing modifiers.
// Sequence: [modifiers] type_keyword [modifiers]
// Zero-initialises dtype and sets DATATYPE_FLAG_IS_SIGNED (C default for
// integers) before delegating to parse_datatype_modifiers and parse_datatype_type.
// dtype — output record; fully populated after the call.
void Parser::parse_datatype(struct datatype *dtype)
{
    memset(dtype, 0, sizeof(struct datatype));
    // C integers are signed by default.
    dtype->flags |= DATATYPE_FLAG_IS_SIGNED;

    parse_datatype_modifiers(dtype);
    parse_datatype_type(dtype);
    // Trailing modifiers are also legal: "int const *p"
    parse_datatype_modifiers(dtype);
}

// Returns true if the keyword "int" may legally follow dtype in a declaration.
// Only long, float, and double allow a trailing "int" (e.g. "long int").
bool Parser::is_int_valid_after_datatype(struct datatype *dtype)
{
    return dtype->type == DATA_TYPE_LONG  ||
           dtype->type == DATA_TYPE_FLOAT ||
           dtype->type == DATA_TYPE_DOUBLE;
}

// If the next token is the keyword "int" and dtype is a type that allows it
// (long, float, double), consumes and discards the redundant "int".
// Calls compiler_error if "int" appears after an incompatible type.
// dtype — the type already parsed; used to decide whether to accept "int".
void Parser::ignore_trailing_int(struct datatype *dtype)
{
    // "long int x" — consume and discard the redundant "int".
    if (!token_is_keyword(peek_token(), "int"))
        return;
    if (!is_int_valid_after_datatype(dtype))
    {
        compiler_error(m_process,
            "'int' suffix not valid after this type abbreviation\n");
    }
    next_token();
}

// ===========================================================================
// § Array brackets
// ===========================================================================

// Parses one or more array dimension specifiers of the form [expr] or [].
// Consumes tokens until no more '[' are present.  Each dimension is parsed as
// an expression, wrapped in a bracket node, and appended to the returned
// array_brackets structure.  Empty brackets (int a[]) halt dimension parsing
// immediately after consuming the '[]'.
// h — parsing context forwarded to the dimension expression parse.
// Returns a heap-allocated array_brackets structure owned by the caller.
struct array_brackets *Parser::parse_array_brackets(History h)
{
    struct array_brackets *brackets = array_brackets_new();
    while (peek_is_op("["))
    {
        expect_op("[");

        if (token_is_symbol(peek_token(), ']'))
        {
            // Empty brackets: int a[];  — unspecified size dimension.
            expect_sym(']');
            break;
        }

        // Parse the dimension expression, e.g. the '10' in int a[10].
        parse_expressionable_root(h);
        expect_sym(']');

        struct node *exp_node = node_pop();
        make_bracket_node(exp_node);
        struct node *bracket_node = node_pop();
        array_brackets_add(brackets, bracket_node);
    }
    return brackets;
}

// ===========================================================================
// § Variable / function / struct / union
// ===========================================================================

// Helper used by the static fixup callback below.  Static methods have no
// `this`, so we can't call Parser::size_of_struct directly.  This free
// function duplicates the symbol-lookup logic.
static size_t lookup_struct_size(struct compile_process *process,
                                  const char *name)
{
    struct symbol *sym = symresolver_get_symbol(process, name);
    if (!sym) return 0;
    assert(sym->type == SYMBOL_TYPE_NODE);
    struct node *n = static_cast<struct node *>(sym->data);
    assert(n->type == NODE_TYPE_STRUCT);
    return n->_struct.body_n->body.size;
}

// --- Fixup callbacks (static) ---

// Fixup callback invoked by fixups_resolve() at end-of-parse to patch a
// variable node whose struct type was forward-referenced at declaration time.
// Looks up the struct name in the symbol table, fills in the type, size, and
// struct_node fields of the variable's datatype.
// fixup — the registered fixup; its private data is a StructFixupData*.
// Returns true if the struct definition was found and the patch succeeded,
// false if the struct is still undefined (parse error).
bool Parser::fix_struct_type(struct fixup *fixup)
{
    StructFixupData *data =
        static_cast<StructFixupData *>(fixup_private(fixup));
    struct datatype *dtype = &data->var_node->var.type;
    dtype->type        = DATA_TYPE_STRUCT;
    dtype->size        = lookup_struct_size(data->process, dtype->type_str);
    dtype->struct_node = struct_node_for_name(data->process, dtype->type_str);
    return dtype->struct_node != nullptr;
}

// Cleanup callback invoked by the fixup system after fix_struct_type has run.
// Frees the StructFixupData heap allocation associated with the fixup.
// fixup — the fixup whose private data should be freed.
void Parser::end_struct_type_fixup(struct fixup *fixup)
{
    free(fixup_private(fixup));
}

// Creates a NODE_TYPE_VARIABLE node and pushes it onto the working stack.
// If the variable's struct type is not yet defined (struct_node is nullptr),
// registers a StructFixupData fixup so the missing information can be filled
// in by fixups_resolve() at the end of the parse.
// dtype      — the fully-parsed datatype for this variable.
// name_token — the identifier token giving the variable's name, or nullptr
//              for unnamed struct/union members.
// value_node — the initialiser expression node, or nullptr if no initialiser.
void Parser::make_variable_node(struct datatype *dtype,
                                 struct token    *name_token,
                                 struct node     *value_node)
{
    const char *name = name_token ? name_token->sval : nullptr;

    struct node n{};
    n.type      = NODE_TYPE_VARIABLE;
    n.var.name  = name;
    n.var.type  = *dtype;
    n.var.val   = value_node;
    node_create(&n);

    struct node *var_node = node_peek_or_null();

    // If we reference a struct type that has not been defined yet, register
    // a fixup so we can patch it once the definition is parsed.
    if (var_node->var.type.type == DATA_TYPE_STRUCT &&
        !var_node->var.type.struct_node)
    {
        StructFixupData *data =
            static_cast<StructFixupData *>(calloc(1, sizeof(StructFixupData)));
        data->process  = m_process;
        data->var_node = var_node;

        struct fixup_config cfg{};
        cfg.fix          = fix_struct_type;
        cfg.end          = end_struct_type_fixup;
        cfg.private_data = data;
        fixup_register(m_fixups, &cfg);
    }
}

// Computes and stores the stack offset for a local variable.
//
// In the default (downward) stack layout, variables live at negative offsets
// from the frame pointer.  The offset for a new variable is calculated by
// taking the previously recorded offset of the most-recently declared local
// (last_scope_entity_before_global) and subtracting the new variable's size.
//
// When HISTORY_UPWARD_STACK is set (function argument layout), variables grow
// upward instead.  The base offset is taken from the function's recorded
// argument stack addition, or from the previous argument's offset if one exists.
//
// Padding is inserted when required to satisfy the type's alignment.
//
// var_node — the variable node whose var.aoffset and var.padding are set.
// h        — must contain h.flags so that HISTORY_UPWARD_STACK is visible.
void Parser::compute_offset_for_stack(struct node *var_node, History h)
{
    // A negative offset means this variable lives below the stack frame
    // pointer (the normal downward-growing stack direction).
    ScopeEntity *last = last_scope_entity_before_global();
    bool upward = (h.flags & HISTORY_UPWARD_STACK) != 0;
    int offset  = -static_cast<int>(variable_size(var_node));

    if (upward)
    {
        // Arguments grow upward from the return address.
        size_t stack_addition =
            function_node_argument_stack_addition(parser_current_function);
        offset = static_cast<int>(stack_addition);
        if (last)
        {
            offset = static_cast<int>(
                datatype_size(&variable_node(last->node)->var.type));
        }
    }

    if (last)
    {
        offset += variable_node(last->node)->var.aoffset;
        if (variable_node_is_primitive(var_node))
        {
            variable_node(var_node)->var.padding = padding(
                upward ? offset : -offset,
                static_cast<int>(var_node->var.type.size));
        }
    }
}

// Computes the offset for a global-scope variable.
// Global variables do not require stack offsets — their addresses are resolved
// by the linker — so this function is intentionally a no-op.
// var_node — the variable node (unused).
// h        — parsing context (unused).
void Parser::compute_offset_for_global(struct node *var_node, History h)
{
    // Global variables do not need stack offsets.
    (void)var_node;
    (void)h;
}

// Computes and stores the byte offset of a struct/union field relative to the
// start of its enclosing aggregate.
// Fields are laid out sequentially: the new field's offset is the previous
// field's offset plus the previous field's size.  Padding is inserted if
// necessary to satisfy the field type's natural alignment.
// The computed offset is written into var_node->var.aoffset, and padding into
// var_node->var.padding.
// var_node — the struct field node to update.
// h        — parsing context (currently unused beyond being passed in).
void Parser::compute_offset_for_struct_field(struct node *var_node, History h)
{
    // Fields are laid out sequentially after the previous field.
    // Padding is inserted to satisfy alignment requirements.
    int offset = 0;
    ScopeEntity *last = last_scope_entity();
    if (last)
    {
        offset += last->stack_offset +
                  static_cast<int>(last->node->var.type.size);
        if (variable_node_is_primitive(var_node))
        {
            var_node->var.padding = padding(offset,
                static_cast<int>(var_node->var.type.size));
        }
        var_node->var.aoffset = offset + var_node->var.padding;
    }
}

// Dispatches to the appropriate offset computation helper based on the current
// parsing context encoded in h.flags:
//   HISTORY_GLOBAL_SCOPE    -> compute_offset_for_global (no-op)
//   HISTORY_INSIDE_STRUCTURE -> compute_offset_for_struct_field (field layout)
//   otherwise               -> compute_offset_for_stack (local/argument layout)
// var_node — the variable node whose offset fields will be filled in.
// h        — parsing context used to select the dispatch branch.
void Parser::compute_variable_offset(struct node *var_node, History h)
{
    if (h.flags & HISTORY_GLOBAL_SCOPE)
    {
        compute_offset_for_global(var_node, h);
        return;
    }
    if (h.flags & HISTORY_INSIDE_STRUCTURE)
    {
        compute_offset_for_struct_field(var_node, h);
        return;
    }
    compute_offset_for_stack(var_node, h);
}

// Creates a variable node, computes its storage offset, registers it in the
// current scope, and pushes it onto the working stack.
// This is the single entry point that coordinates make_variable_node,
// compute_variable_offset, and push_scope_entity.
// h          — parsing context; controls offset computation strategy.
// dtype      — the fully-parsed datatype.
// name_token — the identifier token, or nullptr for anonymous members.
// value_node — the initialiser, or nullptr.
void Parser::register_variable(History h, struct datatype *dtype,
                                struct token *name_token,
                                struct node  *value_node)
{
    make_variable_node(dtype, name_token, value_node);
    struct node *var_node = node_pop();

    compute_variable_offset(var_node, h);

    push_scope_entity(
        new_scope_entity(var_node, var_node->var.aoffset, 0),
        var_node->var.type.size);

    node_push(var_node);
}

// Creates a NODE_TYPE_VARIABLE_LIST node that wraps a vector of variable nodes
// declared in a comma-separated list (e.g. "int a, b, c;") and pushes it.
// var_list_vec — a vector of struct node* pointers, one per declared variable.
void Parser::make_variable_list_node(struct vector *var_list_vec)
{
    struct node n{};
    n.type             = NODE_TYPE_VARIABLE_LIST;
    n.var_list.list    = var_list_vec;
    node_create(&n);
}

// Parses the remainder of a variable declaration after the type and name have
// already been read.  Handles optional array dimensions and an optional
// initialiser expression, then calls register_variable to create and push the
// final variable node.
// dtype      — the type for this variable (may be mutated to add array info).
// name_token — the identifier token for this variable's name.
// h          — parsing context forwarded to sub-parses and register_variable.
void Parser::parse_variable(struct datatype *dtype, struct token *name_token,
                             History h)
{
    struct node *value_node = nullptr;

    // Check for array dimensions: int a[10][20]
    if (peek_is_op("["))
    {
        struct array_brackets *brackets = parse_array_brackets(h);
        dtype->array.brackets = brackets;
        dtype->array.size     = array_brackets_calculate_size(dtype, brackets);
        dtype->flags         |= DATATYPE_FLAG_IS_ARRAY;
    }

    // Optional initializer: int x = 42;
    if (peek_is_op("="))
    {
        next_token(); // consume '='
        parse_expressionable_root(h);
        value_node = node_pop();
    }

    register_variable(h, dtype, name_token, value_node);
}

// Parses a complete variable declaration: type [name].
// Reads the datatype, then reads the optional identifier name token, and
// delegates to parse_variable to handle dimensions, initialisers, and
// registration.
// h — parsing context forwarded to all sub-calls.
void Parser::parse_variable_full(History h)
{
    struct datatype dtype{};
    parse_datatype(&dtype);

    struct token *name_token = nullptr;
    if (peek_token()->type == TOKEN_TYPE_IDENTIFIER)
        name_token = next_token();

    parse_variable(&dtype, name_token, h);
}

// Consumes exactly amount '.' operator tokens from the stream.
// Used when parsing a variadic argument marker "..." (three consecutive dots).
// amount — the number of '.' tokens to expect and consume.
void Parser::token_read_dots(size_t amount)
{
    for (size_t i = 0; i < amount; i++)
        expect_op(".");
}

// Parses the argument list of a function declaration or definition.
// Opens a new scope for the argument names, then reads comma-separated variable
// declarations until ')' is reached.  Variadic functions ("...") are detected
// by a leading '.' — the three dots are consumed and parsing stops immediately,
// returning the arguments gathered so far.  Each argument is parsed with the
// HISTORY_UPWARD_STACK flag so that its stack offset is computed as a positive
// offset above the frame pointer (arguments are passed by the caller).
// h — outer parsing context whose flags are augmented with UPWARD_STACK.
// Returns a heap-allocated vector of struct node* (one per argument).
struct vector *Parser::parse_function_arguments(History h)
{
    new_scope();
    struct vector *args = vector_create(sizeof(struct node *));

    while (!peek_is_symbol(')'))
    {
        // Variadic "..." — read three dots and stop.
        if (peek_is_op("."))
        {
            token_read_dots(3);
            finish_scope();
            return args;
        }

        // Arguments are pushed above the saved return address (upward stack).
        parse_variable_full(history_down(h, h.flags | HISTORY_UPWARD_STACK));
        struct node *arg = node_pop();
        vector_push(args, &arg);

        if (!peek_is_op(","))
            break;
        next_token(); // consume ','
    }

    finish_scope();
    return args;
}

// Parses the body of a function definition as a compound statement.
// Delegates to parse_body with the HISTORY_INSIDE_FUNCTION_BODY flag added
// so that parse_body can accumulate local variable sizes into the function's
// stack_size field.
// h — outer parsing context; INSIDE_FUNCTION_BODY is OR-ed in before the call.
void Parser::parse_function_body(History h)
{
    parse_body(nullptr,
        history_down(h, h.flags | HISTORY_INSIDE_FUNCTION_BODY));
}

// Parses a complete function declaration or definition.
// Opens a new scope, creates the function node skeleton, parses the argument
// list, then either parses the body (full definition) or consumes a ';'
// (prototype).  If the function name is known to the native-function resolver,
// marks the node with FUNCTION_NODE_FLAG_IS_NATIVE.  Returns-by-struct/union
// functions get an extra hidden-pointer argument accounted for in stack_addition.
// ret_type   — the return type already parsed by the caller.
// name_token — the identifier token for the function name.
// h          — parsing context (currently unused after scope is opened).
void Parser::parse_function(struct datatype *ret_type,
                             struct token    *name_token,
                             History          h)
{
    new_scope();
    make_function_node(ret_type, name_token->sval, nullptr, nullptr);
    struct node *func_node = node_peek();
    parser_current_function = func_node;

    // If the function returns a struct/union by value, the caller passes a
    // hidden pointer argument — account for the extra stack slot.
    if (datatype_is_struct_or_union(ret_type))
        func_node->func.args.stack_addition += DATA_SIZE_DWORD;

    expect_op("(");
    struct vector *args = parse_function_arguments(make_history(0));
    expect_sym(')');

    func_node->func.args.vector = args;

    if (symresolver_get_symbol_for_native_function(m_process, name_token->sval))
        func_node->func.flags |= FUNCTION_NODE_FLAG_IS_NATIVE;

    if (peek_is_symbol('{'))
    {
        // Full definition — parse the body.
        parse_function_body(make_history(0));
        struct node *body_node = node_pop();
        func_node->func.body_n = body_node;
    }
    else
    {
        // Prototype — just eat the semicolon.
        expect_sym(';');
    }

    parser_current_function = nullptr;
    finish_scope();
}

// --- Struct / union ---

// Parses a struct definition body (and optional combined variable declaration)
// without opening a new scope — the caller is responsible for scope management.
// If is_forward_declaration is true, no body is expected and the struct node
// is created with a null body (used for "struct S;" forward declarations).
// After parsing the body, if an identifier immediately follows the closing '}'
// it is treated as a combined variable declaration: "struct S { ... } var;".
// Anonymous structs are renamed to the combined variable's name.
// A trailing ';' is always consumed.
// Pushes the completed struct node onto the working stack.
// dtype               — datatype record being built; size and struct_node are
//                       updated here.
// is_forward_declaration — true if this is a forward declaration with no body.
void Parser::parse_struct_no_new_scope(struct datatype *dtype,
                                        bool is_forward_declaration)
{
    struct node *body_node       = nullptr;
    size_t       body_var_size   = 0;

    if (!is_forward_declaration)
    {
        parse_body(&body_var_size, make_history(HISTORY_INSIDE_STRUCTURE));
        body_node = node_pop();
    }

    make_struct_node(dtype->type_str, body_node);
    struct node *struct_node = node_pop();

    if (body_node)
        dtype->size = body_node->body.size;
    dtype->struct_node = struct_node;

    // Combined declaration: struct S { ... } var_name;
    if (token_is_identifier(peek_token()))
    {
        struct token *var_name = next_token();
        struct_node->flags |= NODE_FLAG_HAS_VARIABLE_COMBINED;

        // Anonymous struct: give it the variable's name.
        if (dtype->flags & DATATYPE_FLAG_STRUCT_UNION_NO_NAME)
        {
            dtype->type_str = var_name->sval;
            dtype->flags   &= ~DATATYPE_FLAG_STRUCT_UNION_NO_NAME;
            struct_node->_struct.name = var_name->sval;
        }

        register_variable(make_history(0), dtype, var_name, nullptr);
        struct_node->_struct.var = node_pop();
    }

    expect_sym(';');
    node_push(struct_node);
}

// Parses a struct definition or forward declaration.
// Determines whether the next token is '{' (full definition) or not (forward
// declaration), manages a new scope for full definitions, and delegates to
// parse_struct_no_new_scope.
// dtype — datatype record being constructed for this struct type.
void Parser::parse_struct(struct datatype *dtype)
{
    bool forward = !token_is_symbol(peek_token(), '{');
    if (!forward)
        new_scope();

    parse_struct_no_new_scope(dtype, forward);

    if (!forward)
        finish_scope();
}

// Parses a union definition body (and optional combined variable declaration)
// without opening a new scope — the caller manages the scope.
// Mirrors parse_struct_no_new_scope but uses HISTORY_INSIDE_UNION context and
// calls make_union_node instead of make_struct_node.
// dtype               — datatype record being built.
// is_forward_declaration — true if no body follows.
void Parser::parse_union_no_scope(struct datatype *dtype,
                                   bool is_forward_declaration)
{
    struct node *body_node     = nullptr;
    size_t       body_var_size = 0;

    if (!is_forward_declaration)
    {
        parse_body(&body_var_size, make_history(HISTORY_INSIDE_UNION));
        body_node = node_pop();
    }

    make_union_node(dtype->type_str, body_node);
    struct node *union_node = node_pop();

    if (body_node)
        dtype->size = body_node->body.size;

    // Combined declaration: union U { ... } var_name;
    if (peek_token()->type == TOKEN_TYPE_IDENTIFIER)
    {
        struct token *var_name = next_token();
        union_node->flags |= NODE_FLAG_HAS_VARIABLE_COMBINED;
        register_variable(make_history(0), dtype, var_name, nullptr);
        union_node->_union.var = node_pop();
    }

    expect_sym(';');
    node_push(union_node);
}

// Parses a union definition or forward declaration.
// Mirrors parse_struct but uses parse_union_no_scope and the union scope flag.
// dtype — datatype record being constructed for this union type.
void Parser::parse_union(struct datatype *dtype)
{
    bool forward = !token_is_symbol(peek_token(), '{');
    if (!forward)
        new_scope();

    parse_union_no_scope(dtype, forward);

    if (!forward)
        finish_scope();
}

// Dispatches to parse_struct or parse_union based on dtype->type.
// Calls compiler_error if dtype->type is neither DATA_TYPE_STRUCT nor
// DATA_TYPE_UNION.
// dtype — the partially-filled datatype record; its type field selects the
//         dispatch branch.
void Parser::parse_struct_or_union(struct datatype *dtype)
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
        compiler_error(m_process,
            "Datatype is neither a struct nor a union\n");
    }
}

// Parses a declaration that may be a variable, a function, a struct, or a
// union definition, depending on what follows the type specifier.
//
// Decision tree:
//   1. Parse the leading datatype.
//   2. If struct/union AND next is '{' — parse struct/union definition, add to
//      symbol table, push node, return.
//   3. If next is ';' — forward declaration of a struct (parse_struct), return.
//   4. Consume optional trailing "int" suffix (e.g. "long int").
//   5. Read the identifier name (required at this point).
//   6. If next is '(' — function declaration/definition.
//   7. Otherwise — variable declaration, possibly followed by a comma-separated
//      list of additional names with the same type.
//
// h — parsing context forwarded to all sub-parsers and to register_variable.
void Parser::parse_variable_function_or_struct_union(History h)
{
    struct datatype dtype{};
    parse_datatype(&dtype);

    // struct S { ... } or union U { ... } — definition.
    if (datatype_is_struct_or_union(&dtype) && peek_is_symbol('{'))
    {
        parse_struct_or_union(&dtype);
        struct node *su_node = node_pop();
        symresolver_build_for_node(m_process, su_node);
        node_push(su_node);
        return;
    }

    // Forward declaration: "struct S;" with no body.
    if (peek_is_symbol(';'))
    {
        parse_struct(&dtype);
        return;
    }

    // Consume optional "int" abbreviation suffix (e.g. "long int").
    ignore_trailing_int(&dtype);

    // Expect the identifier name.
    struct token *name_token = next_token();
    if (name_token->type != TOKEN_TYPE_IDENTIFIER)
    {
        compiler_error(m_process,
            "Expected identifier name for declaration\n");
    }

    // Function declaration/definition.
    if (peek_is_op("("))
    {
        parse_function(&dtype, name_token, h);
        return;
    }

    // Variable (possibly comma-separated list).
    parse_variable(&dtype, name_token, h);

    if (token_is_operator(peek_token(), ","))
    {
        // Collect comma-separated variables into a variable-list node.
        struct vector *var_list = vector_create(sizeof(struct node *));
        struct node *var_node = node_pop();
        vector_push(var_list, &var_node);

        while (token_is_operator(peek_token(), ","))
        {
            next_token(); // consume ','
            name_token = next_token();
            parse_variable(&dtype, name_token, h);
            var_node = node_pop();
            vector_push(var_list, &var_node);
        }

        make_variable_list_node(var_list);
    }

    expect_sym(';');
}

// ===========================================================================
// § Body / size accounting
// ===========================================================================

// Accumulates the total storage size of a struct or union variable into accum.
// For struct variables that are not pointers, also adds alignment padding so
// that the aggregate is properly aligned when embedded inside another struct —
// this is done by aligning *accum to the size of the largest primitive member
// in the nested struct's body.
// h      — parsing context (passed through for context-aware decisions).
// accum  — running byte-size total; updated in place.
// node   — a NODE_TYPE_VARIABLE node whose type is a struct or union.
void Parser::struct_or_union_var_size(History h, size_t *accum,
                                       struct node *node)
{
    *accum += variable_size(node);
    if (node->var.type.flags & DATATYPE_FLAG_IS_POINTER)
        return;

    struct node *largest =
        variable_struct_or_union_body_node(node)->body.largest_var_node;
    if (largest)
    {
        // Align to the largest primitive member so that the struct is properly
        // padded when embedded inside another struct.
        *accum += align_value(static_cast<int>(*accum),
                              static_cast<int>(largest->var.type.size));
    }
}

// Iterates over a vector of variable nodes and accumulates their total size
// into accum by calling append_size_for_node on each element.
// Used when a NODE_TYPE_VARIABLE_LIST node (comma-declared variables) is
// encountered in a body.
// h      — parsing context forwarded to append_size_for_node.
// accum  — running byte-size total; updated in place.
// vec    — vector of struct node* containing the variables in the list.
void Parser::append_size_for_variable_list(History h, size_t *accum,
                                            struct vector *vec)
{
    vector_set_peek_pointer(vec, 0);
    struct node *node = vector_peek_ptr_typed<struct node>(vec);
    while (node)
    {
        append_size_for_node(h, accum, node);
        node = vector_peek_ptr_typed<struct node>(vec);
    }
}

// Adds the storage contribution of a single statement node to accum.
// Handles three cases:
//   NODE_TYPE_VARIABLE      — if the variable is a struct/union, delegates to
//                             struct_or_union_var_size; otherwise adds the raw
//                             variable_size.
//   NODE_TYPE_VARIABLE_LIST — recurses via append_size_for_variable_list.
//   anything else           — no-op (control-flow nodes have no storage).
// h      — parsing context forwarded to struct_or_union_var_size.
// accum  — running byte-size total; updated in place.
// node   — the statement node to measure.
void Parser::append_size_for_node(History h, size_t *accum, struct node *node)
{
    if (!node)
        return;

    if (node->type == NODE_TYPE_VARIABLE)
    {
        if (node_is_struct_or_union_variable(node))
        {
            struct_or_union_var_size(h, accum, node);
            return;
        }
        *accum += variable_size(node);
    }
    else if (node->type == NODE_TYPE_VARIABLE_LIST)
    {
        append_size_for_variable_list(h, accum, node->var_list.list);
    }
}

// Finalises a body node after all its statements have been parsed.
//
// For union bodies, overrides *variable_size with the size of the largest
// member (largest_possible), because a union is as large as its biggest field.
//
// Then computes aggregate padding (compute_sum_padding) and adds it to
// *variable_size.
//
// If largest_align_eligible is non-null, aligns the total size up to a
// multiple of that member's type size — this ensures that an array of structs
// has each element correctly aligned.
//
// Finally, populates body_node with the computed values and the statement vector.
//
// h                    — context; HISTORY_INSIDE_UNION controls union sizing.
// body_node            — the body AST node to update.
// body_vec             — the vector of statement nodes for this body.
// variable_size        — in/out: total bytes of local variables; updated here.
// largest_align_eligible — the largest primitive variable node, used for
//                         tail-alignment of the struct/body.
// largest_possible     — the largest variable node overall (may be a struct),
//                        used for union sizing.
void Parser::finalize_body(History h, struct node *body_node,
                            struct vector *body_vec, size_t *variable_size,
                            struct node *largest_align_eligible,
                            struct node *largest_possible)
{
    if (h.flags & HISTORY_INSIDE_UNION)
    {
        // A union is as large as its biggest member.
        if (largest_possible)
            *variable_size = ::variable_size(largest_possible);
    }

    int pad = compute_sum_padding(body_vec);
    *variable_size += static_cast<size_t>(pad);

    if (largest_align_eligible)
    {
        *variable_size = static_cast<size_t>(
            align_value(static_cast<int>(*variable_size),
                        static_cast<int>(largest_align_eligible->var.type.size)));
    }

    body_node->body.largest_var_node = largest_align_eligible;
    body_node->body.padded           = (pad != 0);
    body_node->body.size             = *variable_size;
    body_node->body.statements       = body_vec;
}

// Parses a single-statement body (a body without surrounding braces).
// Creates a temporary body node, sets it as the current body, parses exactly
// one statement, accumulates its size, and calls finalize_body.  The previous
// body is restored in parser_current_body via the binded.owner link.
// Pushes the completed body node onto the working stack.
// variable_size — in/out: accumulates the byte size of any variables declared
//                in the single statement.
// body_vec      — the vector to push the parsed statement into.
// h             — parsing context forwarded to parse_statement and finalize_body.
void Parser::parse_body_single_statement(size_t *variable_size,
                                          struct vector *body_vec,
                                          History h)
{
    make_body_node(nullptr, 0, false, nullptr);
    struct node *body_node = node_pop();
    body_node->binded.owner = parser_current_body;
    parser_current_body     = body_node;

    parse_statement(history_down(h, h.flags));
    struct node *stmt_node = node_pop();
    vector_push(body_vec, &stmt_node);
    append_size_for_node(h, variable_size, stmt_node);

    struct node *largest = nullptr;
    if (stmt_node->type == NODE_TYPE_VARIABLE)
        largest = stmt_node;

    finalize_body(h, body_node, body_vec, variable_size, largest, largest);
    parser_current_body = body_node->binded.owner;
    node_push(body_node);
}

// Parses a brace-enclosed block body: { statements... }.
// Creates a temporary body node, consumes '{', loops calling parse_statement
// until '}', tracking the largest-possible and largest-alignment-eligible
// variable nodes as statements are collected.  After consuming '}', calls
// finalize_body to compute padding and total size.  Restores parser_current_body
// and pushes the completed body node.
// variable_size — in/out: accumulates total local variable bytes.
// body_vec      — the vector to push each statement node into.
// h             — parsing context forwarded throughout.
void Parser::parse_body_multiple_statements(size_t *variable_size,
                                             struct vector *body_vec,
                                             History h)
{
    make_body_node(nullptr, 0, false, nullptr);
    struct node *body_node = node_pop();
    body_node->binded.owner = parser_current_body;
    parser_current_body     = body_node;

    struct node *largest_possible        = nullptr;
    struct node *largest_align_eligible  = nullptr;

    expect_sym('{');

    while (!peek_is_symbol('}'))
    {
        parse_statement(history_down(h, h.flags));
        struct node *stmt_node = node_pop();

        if (stmt_node->type == NODE_TYPE_VARIABLE)
        {
            if (!largest_possible ||
                largest_possible->var.type.size <= stmt_node->var.type.size)
            {
                largest_possible = stmt_node;
            }

            if (variable_node_is_primitive(stmt_node))
            {
                if (!largest_align_eligible ||
                    largest_align_eligible->var.type.size <= stmt_node->var.type.size)
                {
                    largest_align_eligible = stmt_node;
                }
            }
        }

        vector_push(body_vec, &stmt_node);
        append_size_for_node(h, variable_size,
                              variable_node_or_list(stmt_node));
    }

    expect_sym('}');

    finalize_body(h, body_node, body_vec, variable_size,
                  largest_align_eligible, largest_possible);
    parser_current_body = body_node->binded.owner;
    node_push(body_node);
}

// Entry point for parsing any body (function body, struct body, if/while/for
// body).  Opens a new scope, then dispatches to the brace or brace-less
// variant based on whether the next token is '{'.  After parsing, accumulates
// the local variable size into parser_current_function->func.stack_size when
// inside a function body.
// variable_size — out: receives the total bytes of local variables; may be
//                nullptr (a local is used in that case).
// h             — parsing context; INSIDE_FUNCTION_BODY flag triggers the
//                 stack_size accumulation.
void Parser::parse_body(size_t *variable_size, History h)
{
    new_scope();

    size_t tmp_size = 0;
    if (!variable_size)
        variable_size = &tmp_size;

    struct vector *body_vec = vector_create(sizeof(struct node *));

    if (!peek_is_symbol('{'))
    {
        parse_body_single_statement(variable_size, body_vec, h);
        finish_scope();
        return;
    }

    parse_body_multiple_statements(variable_size, body_vec, h);
    finish_scope();

    // Accumulate local variable space into the enclosing function's frame.
    if (h.flags & HISTORY_INSIDE_FUNCTION_BODY)
        parser_current_function->func.stack_size += *variable_size;
}

// ===========================================================================
// § Statements
// ===========================================================================

// Parses a label statement of the form: <identifier>:
// Assumes the identifier node has already been pushed onto the working stack
// by the caller (via parse_expressionable_root).  Consumes ':', pops the
// identifier node, asserts it is NODE_TYPE_IDENTIFIER, and pushes a label node.
// h — parsing context (currently unused).
void Parser::parse_label(History h)
{
    expect_sym(':');
    struct node *label_name_node = node_pop();
    if (label_name_node->type != NODE_TYPE_IDENTIFIER)
    {
        compiler_error(m_process,
            "Expected an identifier for label, got something else\n");
    }
    make_label_node(label_name_node);
}

// Handles statements that begin with a symbol token ('{' or ':').
// '{' — parsed as a bare compound block (global-scope compound literal).
// ':' — parsed as a label (the preceding identifier is already on the stack).
// Any other symbol token causes a compiler_error.
// h — parsing context forwarded to sub-parsers.
void Parser::parse_symbol_token(History h)
{
    if (peek_is_symbol('{'))
    {
        // A bare '{' at non-function scope is treated as a compound literal.
        size_t var_size = 0;
        parse_body(&var_size, make_history(HISTORY_GLOBAL_SCOPE));
        struct node *body_node = node_pop();
        node_push(body_node);
        return;
    }

    if (peek_is_symbol(':'))
    {
        parse_label(make_history(0));
        return;
    }

    compiler_error(m_process, "Unexpected symbol token\n");
}

// Parses a single C statement.
// If the next token is a keyword, delegates to parse_keyword (which handles
// control flow and declarations).  Otherwise parses an expression statement
// via parse_expressionable_root.  After parsing, if a non-semicolon symbol
// follows (e.g. ':' for a label, '{' for a bare block), delegates to
// parse_symbol_token.  Otherwise consumes the required ';'.
// h — parsing context forwarded to all sub-parses.
void Parser::parse_statement(History h)
{
    if (peek_token()->type == TOKEN_TYPE_KEYWORD)
    {
        parse_keyword(h);
        return;
    }

    parse_expressionable_root(h);

    // If a non-semicolon symbol follows, it might be a label ':' or '{'.
    struct token *tok = peek_token();
    if (tok->type == TOKEN_TYPE_SYMBOL && !token_is_symbol(tok, ';'))
    {
        parse_symbol_token(h);
        return;
    }

    expect_sym(';');
}

// ===========================================================================
// § Control flow
// ===========================================================================

// Parses a keyword followed by a parenthesised expression: keyword ( expr ).
// Used by parse_while and parse_switch where the structure is identical.
// Consumes keyword, '(', the expression, and ')'.  Leaves the expression node
// on the working stack.
// keyword — the expected keyword string (e.g. "while", "switch").
void Parser::parse_keyword_with_parens_expr(const char *keyword)
{
    expect_keyword(keyword);
    expect_op("(");
    parse_expressionable_root(make_history(0));
    expect_sym(')');
}

// Parses the body of an else clause and returns the resulting else node.
// Calls parse_body to handle either a brace-enclosed or single-statement body,
// then calls make_else_node and pops the result.
// h — parsing context forwarded to parse_body.
// Returns the heap-allocated else node (not on the working stack).
struct node *Parser::parse_else_block(History h)
{
    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();
    make_else_node(body_node);
    return node_pop();
}

// Peeks at the next token to determine whether an else or else-if clause
// follows an if statement, and parses it if present.
// If "else" is not the next keyword, returns nullptr immediately.
// If "else" is followed by "if", recursively calls parse_if_stmt to handle
// the else-if chain and returns the resulting node.
// Otherwise calls parse_else_block for a plain else body.
// h — parsing context forwarded to sub-parsers.
// Returns the else / else-if node, or nullptr if no else clause was present.
struct node *Parser::parse_else_or_else_if(History h)
{
    if (!peek_is_keyword("else"))
        return nullptr;

    next_token(); // consume "else"

    if (peek_is_keyword("if"))
    {
        // else-if chain: parse recursively.
        parse_if_stmt(history_down(h, 0));
        return node_pop();
    }

    return parse_else_block(history_down(h, 0));
}

// Parses an if statement: if ( expr ) body [else body].
// Consumes "if", the condition in parentheses, the then-body, and any
// immediately following else or else-if clause.  Calls make_if_node with
// the condition, body, and optional else node.  The resulting node is pushed
// by make_if_node.
// h — parsing context forwarded to parse_expressionable_root and parse_body.
void Parser::parse_if_stmt(History h)
{
    expect_keyword("if");
    expect_op("(");
    parse_expressionable_root(h);
    expect_sym(')');

    struct node *cond_node = node_pop();
    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_if_node(cond_node, body_node, parse_else_or_else_if(h));
}

// Parses a while loop: while ( expr ) body.
// Uses parse_keyword_with_parens_expr to consume the keyword and condition,
// then parses the body.  Calls make_while_node with the condition and body.
// h — parsing context forwarded throughout.
void Parser::parse_while(History h)
{
    parse_keyword_with_parens_expr("while");
    struct node *exp_node = node_pop();

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_while_node(exp_node, body_node);
}

// Parses a do-while loop: do body while ( expr );
// Parses the body first (the 'do' keyword is consumed explicitly), then the
// while condition via parse_keyword_with_parens_expr, then the closing ';'.
// Calls make_do_while_node with the body and condition.
// h — parsing context forwarded to parse_body and the condition parse.
void Parser::parse_do_while(History h)
{
    expect_keyword("do");

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    parse_keyword_with_parens_expr("while");
    struct node *exp_node = node_pop();
    expect_sym(';');

    make_do_while_node(body_node, exp_node);
}

// Parses one of the three semicolon-delimited components of a for-loop header
// (initialiser or condition): for ( [init] ; [cond] ; loop ).
// If the next token is ';', consumes it and returns false (empty component).
// Otherwise parses an expression, consumes the mandatory ';', and returns true.
// When true is returned the parsed expression node is on the working stack.
// h — parsing context forwarded to parse_expressionable_root.
bool Parser::parse_for_loop_part(History h)
{
    // Returns false if only a semicolon was present (empty component).
    if (peek_is_symbol(';'))
    {
        next_token(); // consume ';'
        return false;
    }
    parse_expressionable_root(h);
    expect_sym(';');
    return true;
}

// Parses the loop-increment expression (third component) of a for-loop header.
// Unlike parse_for_loop_part, this component is terminated by ')' rather than
// ';', and the ')' is NOT consumed here (the caller does it).
// Returns false if the expression is empty (next token is ')'), true otherwise.
// When true is returned the parsed expression node is on the working stack.
// h — parsing context forwarded to parse_expressionable_root.
bool Parser::parse_for_loop_part_loop(History h)
{
    // The loop expression ends at ')' rather than ';'.
    if (peek_is_symbol(')'))
        return false;
    parse_expressionable_root(h);
    return true;
}

// Parses a for statement: for ( [init] ; [cond] ; [loop] ) body.
// Each of the three header components is optional; nullptr is stored for any
// missing component.  Delegates component parsing to parse_for_loop_part and
// parse_for_loop_part_loop.  Calls make_for_node with the four sub-nodes.
// h — parsing context forwarded throughout.
void Parser::parse_for_stmt(History h)
{
    struct node *init_node = nullptr;
    struct node *cond_node = nullptr;
    struct node *loop_node = nullptr;
    struct node *body_node = nullptr;

    expect_keyword("for");
    expect_op("(");

    if (parse_for_loop_part(h))
        init_node = node_pop();

    if (parse_for_loop_part(h))
        cond_node = node_pop();

    if (parse_for_loop_part_loop(h))
        loop_node = node_pop();

    expect_sym(')');

    size_t var_size = 0;
    parse_body(&var_size, h);
    body_node = node_pop();

    make_for_node(init_node, cond_node, loop_node, body_node);
}

// Parses a case label: case <constant-expr> :
// Consumes "case", parses the constant expression, consumes ':', and creates
// a case node.  Only numeric constants are supported; calls compiler_error if
// the expression is not NODE_TYPE_NUMBER.
// Registers the case index in the enclosing switch's case vector via
// m_switch_cases_ptr so that the switch node can later enumerate its cases.
// h — parsing context forwarded to parse_expressionable_root.
void Parser::parse_case(History h)
{
    expect_keyword("case");
    parse_expressionable_root(h);
    struct node *case_exp = node_pop();
    expect_sym(':');
    make_case_node(case_exp);

    if (case_exp->type != NODE_TYPE_NUMBER)
    {
        compiler_error(m_process,
            "Only numeric constants are supported in case labels\n");
    }

    struct node *case_node = node_pop();

    // Register the case in the enclosing switch's case vector.
    if (m_switch_cases_ptr && *m_switch_cases_ptr)
    {
        struct parsed_switch_case sc;
        sc.index = static_cast<int>(case_node->stmt._case.exp->llnum);
        vector_push(*m_switch_cases_ptr, &sc);
    }
}

// Parses a switch statement: switch ( expr ) body.
// Creates a local cases vector and exposes it via m_switch_cases_ptr so that
// parse_case() called during body parsing can register case indices.  Saves
// and restores the previous m_switch_cases_ptr to support nested switches.
// After parsing the body calls make_switch_node with the expression, body,
// and accumulated cases vector.
// h — parsing context forwarded to parse_keyword_with_parens_expr and
//     parse_body.
void Parser::parse_switch(History h)
{
    // Create a local vector to accumulate case indices for this switch.
    struct vector *cases  = vector_create(sizeof(struct parsed_switch_case));
    bool has_default      = false;

    // Expose the vector to parse_case() via the class-level pointer.
    struct vector **saved_ptr = m_switch_cases_ptr;
    m_switch_cases_ptr        = &cases;

    parse_keyword_with_parens_expr("switch");
    struct node *exp_node = node_pop();

    size_t var_size = 0;
    parse_body(&var_size, h);
    struct node *body_node = node_pop();

    make_switch_node(exp_node, body_node, cases, has_default);

    // Restore previous switch context (supports nested switches).
    m_switch_cases_ptr = saved_ptr;
}

// ===========================================================================
// § Other keyword statements
// ===========================================================================

// Parses a return statement: return [expr] ;
// If the next token is ';', creates a return node with a null expression (bare
// return).  Otherwise parses an expression, creates a return node with it,
// and consumes the trailing ';'.
// Calls make_return_node to build and push the AST node.
// h — parsing context forwarded to parse_expressionable_root.
void Parser::parse_return(History h)
{
    expect_keyword("return");

    if (peek_is_symbol(';'))
    {
        // Bare return.
        expect_sym(';');
        make_return_node(nullptr);
        return;
    }

    parse_expressionable_root(h);
    struct node *exp_node = node_pop();
    make_return_node(exp_node);
    expect_sym(';');
}

// Parses a continue statement: continue ;
// Consumes "continue" and ';', then calls make_continue_node to push the node.
// h — parsing context (unused; kept for interface uniformity).
void Parser::parse_continue(History h)
{
    expect_keyword("continue");
    expect_sym(';');
    make_continue_node();
}

// Parses a break statement: break ;
// Consumes "break" and ';', then calls make_break_node to push the node.
// h — parsing context (unused; kept for interface uniformity).
void Parser::parse_break(History h)
{
    expect_keyword("break");
    expect_sym(';');
    make_break_node();
}

// Parses a goto statement: goto <identifier> ;
// Consumes "goto", parses the target label as an identifier node, consumes
// ';', pops the identifier node, and calls make_goto_node to push the node.
// h — parsing context forwarded to parse_identifier.
void Parser::parse_goto(History h)
{
    expect_keyword("goto");
    parse_identifier(make_history(0));
    expect_sym(';');
    struct node *label_node = node_pop();
    make_goto_node(label_node);
}

// ===========================================================================
// § Top-level dispatch
// ===========================================================================

// Dispatches keyword-introduced constructs to the appropriate sub-parser.
// If the keyword is a type specifier or modifier (is_variable_modifier_keyword
// or keyword_is_datatype), delegates to parse_variable_function_or_struct_union.
// Otherwise matches the keyword against the set of control-flow and statement
// keywords and calls the corresponding parse_* method.  Calls compiler_error
// for any unknown or unhandled keyword.
// h — parsing context forwarded to every sub-parser.
void Parser::parse_keyword(History h)
{
    struct token *tok = peek_token();

    // Type specifiers and modifiers introduce declarations.
    if (is_variable_modifier_keyword(tok->sval) ||
        keyword_is_datatype(tok->sval))
    {
        parse_variable_function_or_struct_union(h);
        return;
    }

    if      (S_EQ(tok->sval, "break"))    { parse_break(h);   return; }
    else if (S_EQ(tok->sval, "continue")) { parse_continue(h);return; }
    else if (S_EQ(tok->sval, "return"))   { parse_return(h);  return; }
    else if (S_EQ(tok->sval, "if"))       { parse_if_stmt(h); return; }
    else if (S_EQ(tok->sval, "for"))      { parse_for_stmt(h);return; }
    else if (S_EQ(tok->sval, "while"))    { parse_while(h);   return; }
    else if (S_EQ(tok->sval, "do"))       { parse_do_while(h);return; }
    else if (S_EQ(tok->sval, "switch"))   { parse_switch(h);  return; }
    else if (S_EQ(tok->sval, "goto"))     { parse_goto(h);    return; }
    else if (S_EQ(tok->sval, "case"))     { parse_case(h);    return; }

    compiler_error(m_process, "Unknown or unsupported keyword\n");
}

// Parses a single keyword-introduced top-level declaration or statement using
// a fresh history (global scope context), then pops and re-pushes the resulting
// node so it sits cleanly as the top-of-stack result for the caller.
void Parser::parse_keyword_for_global()
{
    parse_keyword(make_history(0));
    struct node *node = node_pop();
    node_push(node);
}

// Peeks at the next token and dispatches to the appropriate parser for one
// complete top-level construct.
// TOKEN_TYPE_NUMBER / IDENTIFIER / STRING — expression (e.g. a global constant
//     expression or standalone string literal).
// TOKEN_TYPE_KEYWORD — declaration or control construct at global scope.
// TOKEN_TYPE_SYMBOL  — compound literal or label at global scope.
// Returns 0 after a successful parse, -1 if the token stream is exhausted
// (signals the caller's while loop to stop).
int Parser::parse_next()
{
    struct token *tok = peek_token();
    if (!tok)
        return -1;

    switch (tok->type)
    {
    case TOKEN_TYPE_NUMBER:
    case TOKEN_TYPE_IDENTIFIER:
    case TOKEN_TYPE_STRING:
        parse_expressionable(make_history(0));
        break;

    case TOKEN_TYPE_KEYWORD:
        parse_keyword_for_global();
        break;

    case TOKEN_TYPE_SYMBOL:
        parse_symbol_token(make_history(0));
        break;

    default:
        break;
    }
    return 0;
}

// ===========================================================================
// § C API — the entry point called by the compiler driver
// ===========================================================================

/**
 * parse() — top-level parser entry called from the driver after lexing.
 *
 * Creates a Parser instance, runs it, and returns PARSE_ALL_OK on success.
 */
int parse(struct compile_process *process)
{
    Parser parser(process);
    return parser.run();
}
