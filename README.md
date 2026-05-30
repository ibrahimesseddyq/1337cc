# 1337cc

A hand-written C compiler in C++17 that produces native binaries via an LLVM IR backend.

The pipeline is classical: source text → tokens → AST → LLVM IR → object code → executable. Every stage is implemented from scratch except the final assembly/linking step, which delegates to `llc` and `gcc`.

---

## Table of Contents

1. [Architecture](#architecture)
2. [Building](#building)
3. [Usage](#usage)
4. [Testing](#testing)
5. [Contributing](#contributing)
6. [AI Usage](#ai-usage)

---

## Architecture

```
source.c
   │
   ▼
┌─────────┐   characters   ┌────────────┐   tokens   ┌────────┐   AST
│ cprocess│ ─────────────► │   lexer    │ ──────────► │ parser │ ──────►
│  (I/O)  │                │lex_process │             │        │
└─────────┘                └────────────┘             └────────┘
                                                           │
                                                           ▼
                                                    ┌────────────┐
                                                    │symresolver │  (symbol table)
                                                    └────────────┘
                                                           │
                                                           ▼
                                                    ┌────────────┐
                                                    │  codegen   │  (LLVM IR via C++ API)
                                                    └────────────┘
                                                           │
                                                    /tmp/1337cc_<pid>.ll
                                                           │
                                                    llc ──►│◄── gcc
                                                           │
                                                           ▼
                                                       binary
```

### Source files

| File | Responsibility |
|---|---|
| `main.cpp` | CLI entry point; orchestrates compile → assemble → link |
| `compiler.cpp` | Top-level driver: opens files, calls lex → parse → codegen |
| `cprocess.cpp` | `compile_process` lifecycle; character-level I/O callbacks for the lexer |
| `lexer.cpp` | Tokeniser: recognises numbers, strings, identifiers, operators, symbols |
| `lex_process.cpp` | Lexer state machine and position tracking |
| `token.cpp` | Token classification helpers (`token_is_keyword`, `token_is_operator`, …) |
| `parser.cpp` | Recursive-descent parser; builds an AST of `node` structs |
| `node.cpp` | `node_create` / `node_push` / `node_pop`; the node stack used by the parser |
| `datatype.cpp` | C type representation (`struct datatype`), size calculations, pointer depth |
| `scope.cpp` | Lexical scope stack used during parsing |
| `symresolver.cpp` | Symbol table: maps names → `node *` for forward-declaration fixups |
| `expressionable.cpp` | Pratt-style precedence climbing for expressions |
| `array.cpp` | Array-bracket parsing and size arithmetic |
| `fixup.cpp` | Deferred-fixup system for forward references |
| `helper.cpp` | Padding / alignment utilities |
| `codegen.cpp` | LLVM IR backend: walks the AST and emits IR via the LLVM C++ API |
| `helpers/vector.cpp` | Generic growable array (used everywhere as `struct vector`) |
| `helpers/buffer.cpp` | Byte buffer (used by the lexer for token content) |

### Key data structures

**`compile_process`** — lives for the entire compilation unit; holds the source file handle, the token vector, the node tree, the scope stack, and the symbol tables.

**`token`** — tagged union: type tag + position + one of `{char, const char*, unsigned int, unsigned long, unsigned long long}`.

**`node`** — the AST node. A large tagged union covering every construct: expressions, statements, function definitions, variable declarations, control flow, casts, labels, and more. Fields are defined as named inner structs then placed in an anonymous `union` to keep `sizeof(node)` bounded.

**`datatype`** — represents a C type including pointer depth, array brackets, sign, storage qualifiers, and a link to the struct/union node if needed.

**`scope` / `symbol`** — scope is a linked-list of entity vectors; symbols map string names to nodes for declaration resolution.

**`fixup_system`** — a list of callbacks that are retried after the full AST is built, used for forward-declared functions and similar deferred resolution.

### Code generation

`codegen.cpp` uses the **LLVM C++ API** (LLVM 22, opaque-pointer model). For each top-level declaration it:

1. Declares or defines an `llvm::Function` with the right signature.
2. Allocates locals via `alloca` in the function entry block.
3. Walks the body recursively, emitting `llvm::Value*` results for each expression node.
4. Emits control-flow via `llvm::BasicBlock` branching.

The resulting module is printed as human-readable LLVM IR (`.ll` text) to a private temp file. `llc` converts that to native assembly, and `gcc` links it into the final binary.

Supported language features:
- Types: `void`, `char`, `short`, `int`, `long`, pointers
- Globals with constant initializers; string literals
- Function definitions and forward declarations (including variadic like `printf`)
- All standard binary and unary operators, including bitwise and logical
- `if`/`else`, `while`, `do-while`, `for`, `switch`/`case`/`default`
- `break`, `continue`, `return`, `goto`, labels
- Ternary `?:`, type casts, parenthesised expressions

---

## Building

### Dependencies

- `g++` with C++17 support
- LLVM development headers and `llc` (`llvm` package on Arch, `llvm-dev` on Debian/Ubuntu)
- `gcc` (for the final link step)

### Build

```bash
make
```

This compiles all `.cpp` sources in `cpp/` into `build/cpp/*.o` and links the `1337cc` executable.

### Rebuild from scratch

```bash
make re
```

### Clean

```bash
make clean
```

---

## Usage

```
1337cc <input.c> [-o output]
```

`input.c` is the C source file to compile. `-o output` sets the output binary name; if omitted the binary is placed next to the source with the extension stripped (`foo.c` → `foo`).

```bash
# produces ./hello
./1337cc hello.c

# explicit output name
./1337cc hello.c -o /tmp/hello

# run the result
./hello
```

The intermediate LLVM IR and assembly files are written to `/tmp/1337cc_<pid>.{ll,s}` and deleted automatically after each run.

---

## Testing

The test suite lives in `test/`. Each test is a pair of files:

- `test/NN_name.c` — a small C program
- `test/NN_name.expected` — expected stdout

### Run all tests

```bash
make test
```

### Run a single test

```bash
bash test/run_tests.sh 01_hello.c
```

### How it works

For each `*.c` file in `test/`:

1. `./1337cc test/NN_name.c -o /tmp/1337cc_test/NN_name` — compile to binary
2. Run the binary and capture stdout
3. `diff` the output against `NN_name.expected`

### Adding a test

1. Create `test/NN_name.c` with the C source.
2. Create `test/NN_name.expected` with the exact expected stdout.
3. Run `make test` to verify.

The test numbering is purely organisational; any `*.c` file with a matching `.expected` file is picked up automatically.

---

## Contributing

### Getting started

```bash
git clone <repo-url>
cd 1337cc
make
make test   # all 55 tests should pass before you start
```

### Project conventions

**One stage per file.** Each compilation stage has its own `.cpp`/`.hpp`. Cross-stage communication goes through the `compile_process` struct or the node tree — not via global state.

**No silent failures.** Every function that can fail returns an error code (e.g. `COMPILER_FAILED_WITH_ERRORS`, `PARSE_GENERAL_ERROR`). Use `compiler_error()` for unrecoverable errors; it prints position info and exits. Use `compiler_warning()` for recoverable issues.

**Tests are the spec.** A change is only done when `make test` passes in full. New language features require new test cases.

**Keep the pipeline linear.** The stages run in strict order: lex → parse → codegen. No stage should reach back into a prior stage's internals; use the shared data structures (`compile_process`, `token`, `node`) as the interface.

### Adding a language feature

1. **Lexer** (`lexer.cpp`, `token.cpp`): add new token types or keywords if needed.
2. **Parser** (`parser.cpp`, `node.cpp`, `compiler.hpp`): add a new `NODE_TYPE_*` constant, the `make_*_node()` factory, and the parsing rule.
3. **Codegen** (`codegen.cpp`): add a handler in the node dispatch switch that emits the corresponding LLVM IR.
4. **Tests**: add at least one `test/NN_name.{c,expected}` pair that exercises the new feature.

### Submitting changes

- Keep commits focused — one logical change per commit.
- Run `make test` before opening a pull request.
- The PR description should explain *why* the change is needed, not just what it does.

---

## AI Usage

AI was used during the development of this project — specifically to assist with the refactor from C to C++17 and the migration from hand-written x86 assembly output to the LLVM IR backend.

AI-assisted contributions are welcome. That said, every change must be carefully reviewed by the author before submission. Any change that does not meet the project's quality standards — correct behaviour, clean architecture, passing tests — will not be accepted regardless of how it was written. The bar is the same whether the code comes from a human or a model.
