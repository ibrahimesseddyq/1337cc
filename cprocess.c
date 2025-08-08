#include "compiler.h"


char compile_process_next_char(struct  lex_process* lex_process)
{
     struct compile_process* compiler = lex_process->compiler;

     char c =  getc(compiler->cfile.fp);  
     compiler->pos.col += 1;
     if (c == '\n')
     {
        compiler->pos.line +=1 ;
        compiler->pos.col = 0;
     }
     return c;

}

char compile_process_peek_char(struct  lex_process* lex_process)
{
    struct compile_process* compiler = lex_process->compiler;

    char c =  getc(compiler->cfile.fp);  
    ungetc(c, compiler->cfile.fp);
    return c;
}

char compile_process_push_char(struct  lex_process* lex_process, char c)
{
    struct compile_process* compiler = lex_process->compiler;

    ungetc(c, compiler->cfile.fp);
}