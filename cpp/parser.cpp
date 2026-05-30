// parser.cpp — C API entry point: parse() delegates to the Parser class.

#include "parser.hpp"

int parse(struct compile_process *process)
{
    Parser parser(process);
    return parser.run();
}
