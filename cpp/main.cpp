#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "helpers/vector.hpp"
#include "compiler.hpp"

// Program entry point.
//
// Usage: 1337cc <input.c> [output.ll]
//
// Compiles the given C source file and writes LLVM IR to the output path.
// If no output path is provided, the output file name is derived from the
// input path by replacing its extension with ".ll".
//
// Exit codes:
//   0  — compilation succeeded
//   1  — compilation failed or wrong usage
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <input.c> [output.ll]\n", argv[0]);
        return 1;
    }

    const char *input_path  = argv[1];
    const char *output_path = (argc >= 3) ? argv[2] : nullptr;

    // Derive output path from input if not supplied.
    char derived_out[4096];
    if (!output_path)
    {
        // Replace extension with .ll, or append .ll if no extension found.
        strncpy(derived_out, input_path, sizeof(derived_out) - 4);
        derived_out[sizeof(derived_out) - 1] = '\0';
        char *dot = strrchr(derived_out, '.');
        if (dot)
            strcpy(dot, ".ll");
        else
            strcat(derived_out, ".ll");
        output_path = derived_out;
    }

    int res = compile_file(input_path, output_path, 0);
    if (res == COMPILER_FILE_COMPILED_OK)
    {
        return 0;
    }
    else if (res == COMPILER_FAILED_WITH_ERRORS)
    {
        fprintf(stderr, "Compilation failed: %s\n", input_path);
        return 1;
    }
    else
    {
        fprintf(stderr, "Unknown compiler result: %d\n", res);
        return 1;
    }
}
