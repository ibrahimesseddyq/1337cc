// parser_decl.cpp — Variable, function, struct, and union declaration parsing.

#include "parser.hpp"

// Helper for the static fixup callback — duplicates size_of_struct() logic
// without needing `this`.
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

// ===========================================================================
// Fixup callbacks (static)
// ===========================================================================

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

void Parser::end_struct_type_fixup(struct fixup *fixup)
{
    free(fixup_private(fixup));
}

// ===========================================================================
// Variable creation and offset computation
// ===========================================================================

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

void Parser::compute_offset_for_stack(struct node *var_node, History h)
{
    ScopeEntity *last = last_scope_entity_before_global();
    bool upward = (h.flags & HISTORY_UPWARD_STACK) != 0;
    int offset  = -static_cast<int>(variable_size(var_node));

    if (upward)
    {
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

void Parser::compute_offset_for_global(struct node *var_node, History h)
{
    (void)var_node;
    (void)h;
}

void Parser::compute_offset_for_struct_field(struct node *var_node, History h)
{
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

void Parser::make_variable_list_node(struct vector *var_list_vec)
{
    struct node n{};
    n.type             = NODE_TYPE_VARIABLE_LIST;
    n.var_list.list    = var_list_vec;
    node_create(&n);
}

// ===========================================================================
// Variable declaration parsing
// ===========================================================================

void Parser::parse_variable(struct datatype *dtype, struct token *name_token,
                             History h)
{
    struct node *value_node = nullptr;

    if (peek_is_op("["))
    {
        struct array_brackets *brackets = parse_array_brackets(h);
        dtype->array.brackets = brackets;
        dtype->array.size     = array_brackets_calculate_size(dtype, brackets);
        dtype->flags         |= DATATYPE_FLAG_IS_ARRAY;
    }

    if (peek_is_op("="))
    {
        next_token();
        parse_expressionable_root(h);
        value_node = node_pop();
    }

    register_variable(h, dtype, name_token, value_node);
}

void Parser::parse_variable_full(History h)
{
    struct datatype dtype{};
    parse_datatype(&dtype);

    struct token *name_token = nullptr;
    if (peek_token()->type == TOKEN_TYPE_IDENTIFIER)
        name_token = next_token();

    parse_variable(&dtype, name_token, h);
}

// ===========================================================================
// Function parsing
// ===========================================================================

void Parser::token_read_dots(size_t amount)
{
    for (size_t i = 0; i < amount; i++)
        expect_op(".");
}

struct vector *Parser::parse_function_arguments(History h)
{
    new_scope();
    struct vector *args = vector_create(sizeof(struct node *));

    while (!peek_is_symbol(')'))
    {
        if (peek_is_op("."))
        {
            token_read_dots(3);
            finish_scope();
            return args;
        }

        parse_variable_full(history_down(h, h.flags | HISTORY_UPWARD_STACK));
        struct node *arg = node_pop();
        vector_push(args, &arg);

        if (!peek_is_op(","))
            break;
        next_token();
    }

    finish_scope();
    return args;
}

void Parser::parse_function_body(History h)
{
    parse_body(nullptr,
        history_down(h, h.flags | HISTORY_INSIDE_FUNCTION_BODY));
}

void Parser::parse_function(struct datatype *ret_type,
                             struct token    *name_token,
                             History          h)
{
    new_scope();
    make_function_node(ret_type, name_token->sval, nullptr, nullptr);
    struct node *func_node = node_peek();
    parser_current_function = func_node;

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
        parse_function_body(make_history(0));
        struct node *body_node = node_pop();
        func_node->func.body_n = body_node;
    }
    else
    {
        expect_sym(';');
    }

    parser_current_function = nullptr;
    finish_scope();
}

// ===========================================================================
// Struct / union parsing
// ===========================================================================

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

    if (token_is_identifier(peek_token()))
    {
        struct token *var_name = next_token();
        struct_node->flags |= NODE_FLAG_HAS_VARIABLE_COMBINED;

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

void Parser::parse_struct(struct datatype *dtype)
{
    bool forward = !token_is_symbol(peek_token(), '{');
    if (!forward)
        new_scope();

    parse_struct_no_new_scope(dtype, forward);

    if (!forward)
        finish_scope();
}

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

void Parser::parse_union(struct datatype *dtype)
{
    bool forward = !token_is_symbol(peek_token(), '{');
    if (!forward)
        new_scope();

    parse_union_no_scope(dtype, forward);

    if (!forward)
        finish_scope();
}

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

// ===========================================================================
// Top-level declaration dispatch
// ===========================================================================

void Parser::parse_variable_function_or_struct_union(History h)
{
    struct datatype dtype{};
    parse_datatype(&dtype);

    if (datatype_is_struct_or_union(&dtype) && peek_is_symbol('{'))
    {
        parse_struct_or_union(&dtype);
        struct node *su_node = node_pop();
        symresolver_build_for_node(m_process, su_node);
        node_push(su_node);
        return;
    }

    if (peek_is_symbol(';'))
    {
        parse_struct(&dtype);
        return;
    }

    ignore_trailing_int(&dtype);

    struct token *name_token = next_token();
    if (name_token->type != TOKEN_TYPE_IDENTIFIER)
        compiler_error(m_process, "Expected identifier name for declaration\n");

    if (peek_is_op("("))
    {
        parse_function(&dtype, name_token, h);
        return;
    }

    parse_variable(&dtype, name_token, h);

    if (token_is_operator(peek_token(), ","))
    {
        struct vector *var_list = vector_create(sizeof(struct node *));
        struct node *var_node = node_pop();
        vector_push(var_list, &var_node);

        while (token_is_operator(peek_token(), ","))
        {
            next_token();
            name_token = next_token();
            parse_variable(&dtype, name_token, h);
            var_node = node_pop();
            vector_push(var_list, &var_node);
        }

        make_variable_list_node(var_list);
    }

    expect_sym(';');
}
