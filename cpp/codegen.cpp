// codegen.cpp — C API entry point: codegen() delegates to LLVMCodegen.

#include "codegen.hpp"

int codegen(struct compile_process *process)
{
    LLVMCodegen cg(process);
    return cg.run();
}
