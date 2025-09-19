/**
 * @file compile_process.c
 * @brief Creation and management of the compile process.
 *
 * A compile process (`struct compile_process`) encapsulates the global state
 * required during compilation:
 *  - Input source file (cfile.fp)
 *  - Output file (ofile) if provided
 *  - Symbol resolver state
 *  - Vectors of nodes and syntax trees
 *  - Current line/column position
 *
 * This module also provides the character I/O callbacks for the lexer, allowing
 * it to fetch characters from the source file in a position-tracked manner.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include "compiler.h"
 #include "helpers/vector.h"
 
 /**
  * @brief Create and initialize a new compile process.
  *
  * Opens the input file (source code) for reading and, if provided,
  * the output file for writing. Allocates and initializes the
  * compile_process structure with:
  *  - node vectors
  *  - symbol resolver tables
  *  - file handles
  *  - compiler flags
  *
  * @param filename      Path to input source file.
  * @param filename_out  Path to output file (or NULL if not needed).
  * @param flags         Compiler flags to control behavior.
  * @return Pointer to new compile_process, or NULL on failure (e.g., file open error).
  */
 struct compile_process *compile_process_create(const char *filename, const char *filename_out, int flags)
 {
     FILE *file = fopen(filename, "r");
     if (!file)
     {
         return NULL;
     }
 
     FILE *out_file = NULL;
     if (filename_out)
     {
         out_file = fopen(filename_out, "w");
         if (!out_file)
         {
             return NULL;
         }
     }
 
     struct compile_process* process = calloc(1, sizeof(struct compile_process));
     process->node_vec = vector_create(sizeof(struct node*));
     process->node_tree_vec = vector_create(sizeof(struct node*));
     
     process->flags = flags;
     process->cfile.fp = file;
     process->ofile = out_file;
 
     symresolver_initialize(process);
     symresolver_new_table(process);
     
     return process;
 }
 
 /**
  * @brief Fetch the next character from the source file.
  *
  * This is the main input callback used by the lexer. It increments
  * the compiler's current column counter, retrieves the next character
  * from the input stream, and updates line/column tracking when a newline
  * is encountered.
  *
  * @param lex_process Lexer process containing a pointer to the compile_process.
  * @return Next character from input, or EOF on end of file/error.
  */
 char compile_process_next_char(struct lex_process* lex_process)
 {
     struct compile_process* compiler = lex_process->compiler;
     compiler->pos.col += 1;
     char c = getc(compiler->cfile.fp);
     if (c == '\n')
     {
         compiler->pos.line += 1;
         compiler->pos.col = 1;
     }
 
     return c;
 }
 
 /**
  * @brief Peek at the next character in the input without consuming it.
  *
  * Reads the next character from the input stream, pushes it back
  * with `ungetc()`, and returns it. Does not advance position counters.
  *
  * @param lex_process Lexer process containing the compile_process.
  * @return Next character in input, or EOF if at end of file.
  */
 char compile_process_peek_char(struct lex_process* lex_process)
 {
     struct compile_process* compiler = lex_process->compiler;
     char c = getc(compiler->cfile.fp);
     ungetc(c, compiler->cfile.fp);
     return c;
 }
 
 /**
  * @brief Push a character back onto the input stream.
  *
  * Used by the lexer to undo a read operation. Unlike peek, this allows
  * pushing back an arbitrary character (not necessarily the last one read).
  *
  * @param lex_process Lexer process containing the compile_process.
  * @param c Character to push back into the input stream.
  */
 void compile_process_push_char(struct lex_process* lex_process, char c)
 {
     struct compile_process* compiler = lex_process->compiler;
     ungetc(c, compiler->cfile.fp);
 }
 