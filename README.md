# 1337cc

**1337cc** is a toy/educational **C compiler** written in **C**.  
It implements a basic compilation pipeline (lexing → parsing → semantic work → code generation) and produces an output via its internal codegen.

> Repo description: **1337 C Compiler**

## What’s inside

This repository is organized as a set of compiler stages/modules:

- **Lexing**
  - `lexer.c`, `lex_process.c`, `token.c`
- **Parsing / AST**
  - `parser.c`, `node.c`
- **Semantic / symbol work**
  - `scope.c`, `symresolver.c`, `datatype.c`, `expressionable.c`
- **Code generation**
  - `codegen.c`, `fixup.c`
- **Core / driver**
  - `compiler.c`, `compiler.h`, `cprocess.c`, `main.c`
- **Utilities**
  - `array.c`, `helper.c`
  - `helpers/` (e.g. buffer/vector helpers)
- `build/` contains object outputs (used by the Makefile rules)

## Build

The project is built with `gcc` and a simple Makefile that compiles each module into `./build/*.o` and then links an executable called `main`.

```bash
make
```

Output:
- `./main`

### Rebuild / Clean

```bash
make clean
make re
```

## Run

After building:

```bash
./main
```

> If your `main` expects input files/arguments, run `./main --help` or check `main.c` to see the supported CLI (the repo also contains `test` / `test.c` that may show usage patterns).

## Notes

- This is not a full production C compiler; it’s an educational implementation meant to explore how a compiler is structured.
- The Makefile currently links with `-g` (debug symbols), which is convenient for stepping through the compilation stages in a debugger.

## Author

- GitHub: `ibrahimesseddyq`
