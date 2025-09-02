#include "compiler.h"
#include "helpers/vector.h"

static struct compile_process* current_process;
static struct token* parser_last_token;
static struct token* token_next()
{
    struct token* next_token = vector_peek_no_increment(current_process->token_vec);
    parser_ignore_nl_or_comment(next_token);
    current_process->pos = next_token->pos;
    parser_last_token = next_token;
}
int parse_next()
{
    return 0;
}
int parse(struct compile_process* process)
{
    current_process = process;

    struct node* node=NULL;

    vector_set_peek_pointer(process->token_vec, 0);
    while (parse_next())
    {
        node = node_peek();
        vector_push(process->node_tree_vec, &node);
    }
    return PARSE_ALL_OK;
}