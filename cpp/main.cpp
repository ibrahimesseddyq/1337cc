#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "helpers/vector.hpp"
#include "compiler.hpp"

// Program entry point.
//
// Usage: 1337cc <input.c> [-o output]
//
// Compiles the given C source file and produces a native binary.
// If -o is omitted the output name is derived from the input by stripping
// the file extension (e.g. foo.c → foo).
//
// Exit codes:
//   0  — compilation succeeded
//   1  — compilation failed or wrong usage
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <input.c> [-o output]\n", argv[0]);
        return 1;
    }

    const char *input_path  = argv[1];
    const char *output_path = nullptr;

    for (int i = 2; i < argc - 1; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            output_path = argv[i + 1];
            break;
        }
    }

    // Derive binary name from input path if -o was not given.
    char derived_out[4096];
    if (!output_path)
    {
        strncpy(derived_out, input_path, sizeof(derived_out) - 1);
        derived_out[sizeof(derived_out) - 1] = '\0';
        char *dot = strrchr(derived_out, '.');
        if (dot)
            *dot = '\0';
        output_path = derived_out;
    }

    // Emit LLVM IR to a private temp file.
    char tmp_ll[64];
    snprintf(tmp_ll, sizeof(tmp_ll), "/tmp/1337cc_%d.ll", (int)getpid());

    int res = compile_file(input_path, tmp_ll, 0);
    if (res != COMPILER_FILE_COMPILED_OK)
    {
        unlink(tmp_ll);
        if (res == COMPILER_FAILED_WITH_ERRORS)
            fprintf(stderr, "Compilation failed: %s\n", input_path);
        else
            fprintf(stderr, "Unknown compiler result: %d\n", res);
        return 1;
    }

    // Assemble via llc then link via gcc, both silently.
    char tmp_s[64];
    snprintf(tmp_s, sizeof(tmp_s), "/tmp/1337cc_%d.s", (int)getpid());

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "llc --relocation-model=pic %s -o %s >/dev/null 2>&1"
             " && gcc %s -o %s >/dev/null 2>&1",
             tmp_ll, tmp_s, tmp_s, output_path);
    int link_res = system(cmd);
    unlink(tmp_ll);
    unlink(tmp_s);

    if (link_res != 0)
    {
        fprintf(stderr, "Linking failed: %s\n", input_path);
        return 1;
    }

    return 0;
}
