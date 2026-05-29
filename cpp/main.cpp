#include <cstdio>
#include "helpers/vector.hpp"
#include "compiler.hpp"

// Program entry point.  Invokes the full compilation pipeline on the
// hard-coded source file "./test.c" and writes the output to "./test".
// Prints a human-readable status message to stdout depending on whether
// compilation succeeded, failed, or returned an unexpected result code.
//
// Returns 0 in all cases (the exit code does not currently reflect
// compilation success or failure).
int main()
{
    int res = compile_file("./test.c", "./test", 0);
    if (res == COMPILER_FILE_COMPILED_OK)
    {
        printf("everything compiled fine\n");
    }
    else if (res == COMPILER_FAILED_WITH_ERRORS)
    {
        printf("Compile failed\n");
    }
    else
    {
        printf("Unknown response for compile time\n");
    }
    return 0;
}
