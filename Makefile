CXX      = g++

# LLVM flags (requires the 'llvm' package with development headers).
# Falls back gracefully if llvm-config is not found.
LLVM_CXXFLAGS := $(shell llvm-config --cxxflags 2>/dev/null)
LLVM_LDFLAGS  := $(shell llvm-config --ldflags --libs core --system-libs 2>/dev/null)

# Strip flags that conflict with our build or that LLVM headers trip over.
# Remove -Wpedantic / -fpermissive (LLVM headers are not pedantic-clean).
# Remove -DNDEBUG so assertions stay on in debug builds.
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I./ \
           $(filter-out -Wpedantic -DNDEBUG, $(LLVM_CXXFLAGS))

SRCS = cpp/main.cpp cpp/compiler.cpp cpp/cprocess.cpp cpp/token.cpp \
       cpp/lex_process.cpp cpp/lexer.cpp \
       cpp/node.cpp cpp/scope.cpp cpp/symresolver.cpp cpp/datatype.cpp \
       cpp/array.cpp cpp/helper.cpp cpp/fixup.cpp cpp/expressionable.cpp \
       cpp/codegen.cpp cpp/parser.cpp \
       cpp/helpers/vector.cpp cpp/helpers/buffer.cpp

OBJS = $(patsubst %.cpp, ./build/%.o, $(SRCS))

all: $(OBJS)
	$(CXX) $(CXXFLAGS) -o 1337cc $(OBJS) $(LLVM_LDFLAGS)

./build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: all
	bash test/run_tests.sh

clean:
	rm -f 1337cc
	rm -rf ./build

re: clean all
