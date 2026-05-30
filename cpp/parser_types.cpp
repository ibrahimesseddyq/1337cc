// parser_types.cpp — C datatype parsing (modifiers, primitives, structs, arrays).

#include "parser.hpp"

// ===========================================================================
// Datatype parsing
// ===========================================================================

bool Parser::is_variable_modifier_keyword(const char *val)
{
    return S_EQ(val, "unsigned") ||
           S_EQ(val, "signed")   ||
           S_EQ(val, "static")   ||
           S_EQ(val, "const")    ||
           S_EQ(val, "extern")   ||
           S_EQ(val, "__ignore_typecheck__");
}

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

int Parser::datatype_expected_kind(const char *type_str)
{
    if (S_EQ(type_str, "union"))  return DATA_TYPE_EXPECT_UNION;
    if (S_EQ(type_str, "struct")) return DATA_TYPE_EXPECT_STRUCT;
    return DATA_TYPE_EXPECT_PRIMITIVE;
}

int Parser::anon_type_index()
{
    static int counter = 0;
    return ++counter;
}

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

bool Parser::is_secondary_allowed(int expected_kind)
{
    return expected_kind == DATA_TYPE_EXPECT_PRIMITIVE;
}

bool Parser::is_secondary_allowed_for_type(const char *type_str)
{
    return S_EQ(type_str, "long")   ||
           S_EQ(type_str, "short")  ||
           S_EQ(type_str, "double") ||
           S_EQ(type_str, "float");
}

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

void Parser::init_primitive_type(struct token *primary, struct token *secondary,
                                  struct datatype *out)
{
    if (!is_secondary_allowed_for_type(primary->sval) && secondary)
        compiler_error(m_process,
            "Secondary datatype not allowed for '%s'\n", primary->sval);

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

void Parser::init_datatype(struct token *primary, struct token *secondary,
                            struct datatype *out, int pointer_depth,
                            int expected_kind)
{
    if (!is_secondary_allowed(expected_kind) && secondary)
        compiler_error(m_process, "Invalid secondary datatype\n");

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

    out->type_str      = primary->sval;
    out->pointer_depth = pointer_depth;

    if (S_EQ(primary->sval, "long") &&
        secondary && S_EQ(secondary->sval, "long"))
    {
        compiler_warning(m_process,
            "long long is not supported; defaulting to 32-bit long\n");
        out->size = DATA_SIZE_DWORD;
    }
}

void Parser::parse_datatype_type(struct datatype *dtype)
{
    struct token *primary   = nullptr;
    struct token *secondary = nullptr;
    get_datatype_tokens(&primary, &secondary);

    int expected_kind = datatype_expected_kind(primary->sval);

    if (datatype_is_struct_or_union_for_name(primary->sval))
    {
        if (peek_token()->type == TOKEN_TYPE_IDENTIFIER)
        {
            primary = next_token();
        }
        else
        {
            primary = build_anonymous_type_name();
            dtype->flags |= DATATYPE_FLAG_STRUCT_UNION_NO_NAME;
        }
    }

    int pointer_depth = count_pointer_stars();
    init_datatype(primary, secondary, dtype, pointer_depth, expected_kind);
}

void Parser::parse_datatype(struct datatype *dtype)
{
    memset(dtype, 0, sizeof(struct datatype));
    dtype->flags |= DATATYPE_FLAG_IS_SIGNED;

    parse_datatype_modifiers(dtype);
    parse_datatype_type(dtype);
    parse_datatype_modifiers(dtype);
}

bool Parser::is_int_valid_after_datatype(struct datatype *dtype)
{
    return dtype->type == DATA_TYPE_LONG  ||
           dtype->type == DATA_TYPE_FLOAT ||
           dtype->type == DATA_TYPE_DOUBLE;
}

void Parser::ignore_trailing_int(struct datatype *dtype)
{
    if (!token_is_keyword(peek_token(), "int"))
        return;
    if (!is_int_valid_after_datatype(dtype))
        compiler_error(m_process,
            "'int' suffix not valid after this type abbreviation\n");
    next_token();
}

// ===========================================================================
// Array brackets
// ===========================================================================

struct array_brackets *Parser::parse_array_brackets(History h)
{
    struct array_brackets *brackets = array_brackets_new();
    while (peek_is_op("["))
    {
        expect_op("[");

        if (token_is_symbol(peek_token(), ']'))
        {
            expect_sym(']');
            break;
        }

        parse_expressionable_root(h);
        expect_sym(']');

        struct node *exp_node = node_pop();
        make_bracket_node(exp_node);
        struct node *bracket_node = node_pop();
        array_brackets_add(brackets, bracket_node);
    }
    return brackets;
}
