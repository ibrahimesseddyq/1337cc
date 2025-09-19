#include "compiler.h"
#include "helpers/vector.h"
#include <stdarg.h>
#include <stdio.h>


static struct compile_process* current_process = NULL;

void codegen_new_scope(int flags)
{
    #warning "The resolver needs to exist for this to work"
}

void codegen_finish_scope()
{
    #warning "The resolver needs to exist for this to work"
   
}
void asm_push_args(const char* ins, va_list args)
{
    va_list args2;
    va_copy(args2, args);
    vfprintf(stdout, ins, args);

    fprintf(stdout, "\n");
    if (current_process->ofile)
    {
        vfprintf(current_process->ofile, ins, args2);
        fprintf(current_process->ofile, "\n");

    }
}
void asm_push(const char* ins, ...)
{
    va_list args;
    va_start(args, ins);
    asm_push_args(ins, args);
    va_end(args);
}
int codegen(struct compile_process* process)
{
    current_process = process;
    scope_create_root(current_process);
    vector_set_peek_pointer(process->node_tree_vec, 0);
    return 0;
}