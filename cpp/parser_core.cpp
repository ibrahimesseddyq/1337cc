// parser_core.cpp — Parser constructor, run(), token access, and scope helpers.

#include "parser.hpp"

Parser::Parser(struct compile_process *process)
    : m_process(process)
    , m_fixups(fixup_sys_new())
{
}

int Parser::run()
{
    scope_create_root(m_process);
    node_set_vector(m_process->node_vec, m_process->node_tree_vec);

    struct node blank{};
    blank.type  = NODE_TYPE_BLANK;
    m_blank_node = node_create(&blank);
    node_pop();

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
// Token access
// ===========================================================================

void Parser::skip_nl_or_comment(struct token *tok)
{
    while (tok && token_is_nl_or_comment_or_newline_seperator(tok))
    {
        vector_peek(m_process->token_vec);
        tok = static_cast<struct token *>(
            vector_peek_no_increment(m_process->token_vec));
    }
}

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

struct token *Parser::peek_token()
{
    struct token *tok =
        static_cast<struct token *>(
            vector_peek_no_increment(m_process->token_vec));
    skip_nl_or_comment(tok);
    return static_cast<struct token *>(
        vector_peek_no_increment(m_process->token_vec));
}

bool Parser::peek_is_op(const char *op)
{
    return token_is_operator(peek_token(), op);
}

bool Parser::peek_is_keyword(const char *keyword)
{
    return token_is_keyword(peek_token(), keyword);
}

bool Parser::peek_is_symbol(char c)
{
    return token_is_symbol(peek_token(), c);
}

void Parser::expect_op(const char *op)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_OPERATOR || !S_EQ(tok->sval, op))
        compiler_error(m_process,
            "Expected operator '%s' but got something else\n", op);
}

void Parser::expect_sym(char c)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_SYMBOL || tok->cval != c)
        compiler_error(m_process,
            "Expected symbol '%c' but got something else\n", c);
}

void Parser::expect_keyword(const char *keyword)
{
    struct token *tok = next_token();
    if (!tok || tok->type != TOKEN_TYPE_KEYWORD || !S_EQ(tok->sval, keyword))
        compiler_error(m_process,
            "Expected keyword '%s' but got something else\n", keyword);
}

// ===========================================================================
// Scope helpers
// ===========================================================================

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

ScopeEntity *Parser::last_scope_entity()
{
    return static_cast<ScopeEntity *>(scope_last_entity(m_process));
}

ScopeEntity *Parser::last_scope_entity_before_global()
{
    return static_cast<ScopeEntity *>(
        scope_last_entity_stop_at(m_process, m_process->scope.root));
}

void Parser::push_scope_entity(ScopeEntity *entity, size_t size)
{
    scope_push(m_process, entity, size);
}

void Parser::new_scope()
{
    ::scope_new(m_process, 0);
}

void Parser::finish_scope()
{
    ::scope_finish(m_process);
}
