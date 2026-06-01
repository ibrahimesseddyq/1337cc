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
    void          parse_default(History h);
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
