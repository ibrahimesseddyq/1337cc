#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

static struct compile_process *current_process = nullptr;

// Opens a new code-generation scope.
// flags - reserved scope flags (currently unused; the resolver is not yet wired in).
// Side effect: intended to call into the scope/resolver subsystem once it exists.
void codegen_new_scope(int /*flags*/)
{
#warning "The resolver needs to exist for this to work"
}

// Closes the most recently opened code-generation scope.
// Must be paired with a preceding call to codegen_new_scope().
// Side effect: intended to pop the current scope from the resolver once it exists.
void codegen_finish_scope()
{
#warning "The resolver needs to exist for this to work"
}

// Advances the peek pointer of the current process's node tree vector and returns
// the next top-level AST node, or nullptr when the vector is exhausted.
// Returns a pointer to the next struct node in node_tree_vec, or nullptr at end.
struct node *codegen_node_next()
{
    return static_cast<struct node *>(vector_peek_ptr(current_process->node_tree_vec));
}

// Internal helper: writes a formatted assembly line to stdout and, if an output
// file is open, to current_process->ofile as well. A newline is appended after
// each line automatically.
// ins  - printf-style format string for the assembly instruction or directive.
// args - already-started va_list of arguments corresponding to ins.
// Note: makes a copy of args via va_copy so the list can be consumed twice
//       (once for stdout, once for the file).
static void asm_push_args(const char *ins, va_list args)
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
    va_end(args2);
}

// Emits a single formatted assembly line to both stdout and the output file.
// ins  - printf-style format string for the assembly instruction or directive.
// ...  - variadic arguments matching the format specifiers in ins.
// Side effect: writes to stdout and, if present, to current_process->ofile.
void asm_push(const char *ins, ...)
{
    va_list args;
    va_start(args, ins);
    asm_push_args(ins, args);
    va_end(args);
}

// Maps a data size (in bytes) to the corresponding NASM data-declaration keyword
// (db, dw, dd) and writes it into tmp_buf.
// size    - the size in bytes to look up (use DATA_SIZE_* constants).
// tmp_buf - caller-supplied buffer of at least 256 bytes used to build the string
//           for sizes that do not map to a single keyword (e.g. "times N db").
// Returns a pointer to tmp_buf, which always holds the resulting keyword string
// after the call. The pointer is valid for the lifetime of tmp_buf.
static const char *asm_keyword_for_size(size_t size, char *tmp_buf)
{
    const char *keyword = nullptr;
    switch (size)
    {
    case DATA_SIZE_BYTE:
        keyword = "db";
        break;
    case DATA_SIZE_WORD:
        keyword = "dw";
        break;
    case DATA_SIZE_DWORD:
        keyword = "dd";
        break;
    case DATA_SIZE_DDWORD:
        keyword = "dw";
        break;
    default:
        sprintf(tmp_buf, "times %lld db", (unsigned long long)size);
        return tmp_buf;
    }
    strcpy(tmp_buf, keyword);
    return tmp_buf;
}

// Emits the NASM data-section declaration for a single primitive global variable
// (char, short, int, long, void*). The variable is zero-initialized in the
// output; value initializers are not yet implemented.
// node - a NODE_TYPE_VARIABLE node whose var.type is a primitive data type.
// Side effect: calls asm_push() to emit one assembly line of the form
//              "varname: KEYWORD 0".
static void codegen_generate_global_variable_for_primitive(struct node *node)
{
    char tmp_buf[256];
    if (node->var.val != nullptr)
    {
        // value initializer — not yet implemented
    }
    asm_push("%s: %s 0", node->var.name,
             asm_keyword_for_size(variable_size(node), tmp_buf));
}

// Emits the NASM data-section declaration for a global variable of any supported
// type. First writes a comment line with the type and name, then dispatches to
// the appropriate type-specific generator.
// node - a NODE_TYPE_VARIABLE node representing the global variable.
// Side effect: calls asm_push() to emit a comment and the data declaration.
//              Calls compiler_error() and aborts if the type is float or double,
//              as those are not yet supported.
static void codegen_generate_global_variable(struct node *node)
{
    asm_push("; %s %s", node->var.type.type_str, node->var.name);
    switch (node->var.type.type)
    {
    case DATA_TYPE_VOID:
    case DATA_TYPE_CHAR:
    case DATA_TYPE_SHORT:
    case DATA_TYPE_INTEGER:
    case DATA_TYPE_LONG:
        codegen_generate_global_variable_for_primitive(node);
        break;
    case DATA_TYPE_FLOAT:
    case DATA_TYPE_DOUBLE:
        compiler_error(current_process, "Float and double are not supported yet");
        break;
    default:
        break;
    }
}

// Handles code generation for a single AST node that belongs to the .data section.
// Currently only NODE_TYPE_VARIABLE nodes are acted upon; all other node types
// are silently skipped.
// node - the AST node to process.
// Side effect: may call asm_push() indirectly through codegen_generate_global_variable().
static void codegen_generate_data_section_part(struct node *node)
{
    switch (node->type)
    {
    case NODE_TYPE_VARIABLE:
        codegen_generate_global_variable(node);
        break;
    default:
        break;
    }
}

// Emits the entire NASM .data section by iterating over all top-level AST nodes
// in the current process's node tree vector. Emits a "section .data" header first,
// then processes each node in order.
// Side effect: advances the node_tree_vec peek pointer to the end of the vector.
//              Calls asm_push() for every variable declaration encountered.
static void codegen_generate_data_section()
{
    asm_push("section .data");
    struct node *node = codegen_node_next();
    while (node)
    {
        codegen_generate_data_section_part(node);
        node = codegen_node_next();
    }
}

// Stub: intended to emit NASM code for a single top-level (root) AST node in the
// .text section. Currently a no-op; function-body code generation is not yet
// implemented.
// node - the top-level AST node to compile (unused until implemented).
static void codegen_generate_root_node(struct node * /*node*/)
{
}

// Emits the NASM .text section by iterating over all top-level AST nodes and
// calling codegen_generate_root_node() for each one.
// Side effect: advances the node_tree_vec peek pointer to the end of the vector.
//              Currently only emits the "section .text" header because
//              codegen_generate_root_node() is a stub.
static void codegen_generate_root()
{
    asm_push("section .text");
    struct node *node = nullptr;
    while ((node = codegen_node_next()) != nullptr)
        codegen_generate_root_node(node);
}

// Stub: intended to emit all string literals collected during parsing into the
// .rodata section. Currently a no-op; string emission is not yet implemented.
static void codegen_write_strings()
{
}

// Emits the NASM .rodata section header and then emits all string literals via
// codegen_write_strings(). Currently only writes the section header because
// codegen_write_strings() is a stub.
// Side effect: calls asm_push() to write "section .rodata".
static void codegen_generate_rd()
{
    asm_push("section .rodata");
    codegen_write_strings();
}

// Entry point for the code generation pass. Drives the full pipeline:
//   1. Creates the root scope.
//   2. Emits the .data section (global variable declarations).
//   3. Rewinds the node tree and emits the .text section (function bodies).
//   4. Closes the root scope and emits the .rodata section (string literals).
// process - the compile_process for the current translation unit. Must have a
//            valid node_tree_vec populated by the parser, and optionally an ofile
//            for assembly output.
// Returns 0 on success. On internal errors (e.g. unsupported type),
// compiler_error() is called which does not return.
int codegen(struct compile_process *process)
{
    current_process = process;
    scope_create_root(current_process);
    vector_set_peek_pointer(process->node_tree_vec, 0);

    codegen_new_scope(0);
    codegen_generate_data_section();

    vector_set_peek_pointer(process->node_tree_vec, 0);
    codegen_generate_root();

    codegen_finish_scope();
    codegen_generate_rd();
    return 0;
}
